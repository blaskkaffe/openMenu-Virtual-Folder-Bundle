#include <string.h>

#include "qr.h"

#define MAX_CODEWORDS 3706 /* version 40 */

static const uint8_t ecc_per_block[41] = {0,  7,  10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28,
                                          28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30};
static const uint8_t num_blocks[41] = {0, 1,  1,  1,  1,  1,  2,  2,  2,  2,  4,  4,  4,  4,  4,  6,  6,  6,  6,  7, 8,
                                       8, 9,  9,  10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25};

static int
raw_modules(int ver) {
    int r = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        int na = ver / 7 + 2;
        r -= (25 * na - 10) * na - 55;
        if (ver >= 7) r -= 36;
    }
    return r;
}

static int
data_codewords(int ver) {
    return raw_modules(ver) / 8 - ecc_per_block[ver] * num_blocks[ver];
}

static uint8_t
gf_mul(uint8_t x, uint8_t y) {
    int z = 0;
    for (int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return (uint8_t)z;
}

static void
rs_remainder(const uint8_t* data, int len, int degree, uint8_t* out) {
    uint8_t gen[30];
    memset(gen, 0, sizeof(gen));
    gen[degree - 1] = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; i++) {
        for (int j = 0; j < degree; j++) {
            gen[j] = gf_mul(gen[j], root);
            if (j + 1 < degree) gen[j] ^= gen[j + 1];
        }
        root = gf_mul(root, 2);
    }
    memset(out, 0, (size_t)degree);
    for (int i = 0; i < len; i++) {
        uint8_t factor = data[i] ^ out[0];
        memmove(out, out + 1, (size_t)degree - 1);
        out[degree - 1] = 0;
        for (int j = 0; j < degree; j++) out[j] ^= gf_mul(gen[j], factor);
    }
}

static uint8_t mod[QR_MAX_SIZE * QR_MAX_SIZE];
static uint8_t func[QR_MAX_SIZE * QR_MAX_SIZE];
static int size;

static void
setf(int x, int y, int dark) {
    if (x < 0 || y < 0 || x >= size || y >= size) return;
    mod[y * size + x] = (uint8_t)dark;
    func[y * size + x] = 1;
}

static void
finder(int cx, int cy) {
    for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            int d = dx < 0 ? -dx : dx, e = dy < 0 ? -dy : dy;
            int dist = d > e ? d : e;
            setf(cx + dx, cy + dy, dist != 2 && dist != 4);
        }
    }
}

static void
alignment(int cx, int cy) {
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int d = dx < 0 ? -dx : dx, e = dy < 0 ? -dy : dy;
            int dist = d > e ? d : e;
            setf(cx + dx, cy + dy, dist != 1);
        }
    }
}

static void
format_bits(int mask) {
    int data = (1 << 3) | mask; /* level L is 01 */
    int rem = data;
    for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    int bits = ((data << 10) | rem) ^ 0x5412;
    for (int i = 0; i <= 5; i++) setf(8, i, (bits >> i) & 1);
    setf(8, 7, (bits >> 6) & 1);
    setf(8, 8, (bits >> 7) & 1);
    setf(7, 8, (bits >> 8) & 1);
    for (int i = 9; i < 15; i++) setf(14 - i, 8, (bits >> i) & 1);
    for (int i = 0; i < 8; i++) setf(size - 1 - i, 8, (bits >> i) & 1);
    for (int i = 8; i < 15; i++) setf(8, size - 15 + i, (bits >> i) & 1);
    setf(8, size - 8, 1);
}

static void
draw_function(int ver) {
    memset(mod, 0, sizeof(mod));
    memset(func, 0, sizeof(func));
    for (int i = 0; i < size; i++) {
        setf(6, i, i % 2 == 0);
        setf(i, 6, i % 2 == 0);
    }
    finder(3, 3);
    finder(size - 4, 3);
    finder(3, size - 4);
    if (ver > 1) {
        int na = ver / 7 + 2;
        int step = ver == 32 ? 26 : (ver * 4 + na * 2 + 1) / (na * 2 - 2) * 2;
        int pos[8];
        pos[0] = 6;
        for (int i = na - 1, p = size - 7; i >= 1; i--, p -= step) pos[i] = p;
        for (int i = 0; i < na; i++) {
            for (int j = 0; j < na; j++) {
                if ((i == 0 && j == 0) || (i == 0 && j == na - 1) || (i == na - 1 && j == 0)) continue;
                alignment(pos[i], pos[j]);
            }
        }
    }
    format_bits(0); /* reserves the area */
    if (ver >= 7) {
        int rem = ver;
        for (int i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        int bits = (ver << 12) | rem;
        for (int i = 0; i < 18; i++) {
            int bit = (bits >> i) & 1, a = size - 11 + i % 3, b = i / 3;
            setf(a, b, bit);
            setf(b, a, bit);
        }
    }
}

static int
mask_bit(int m, int x, int y) {
    switch (m) {
        case 0: return (x + y) % 2 == 0;
        case 1: return y % 2 == 0;
        case 2: return x % 3 == 0;
        case 3: return (x + y) % 3 == 0;
        case 4: return (x / 3 + y / 2) % 2 == 0;
        case 5: return x * y % 2 + x * y % 3 == 0;
        case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
        default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}

static void
apply_mask(int m) {
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (!func[y * size + x] && mask_bit(m, x, y)) mod[y * size + x] ^= 1;
        }
    }
}

static int
penalty(void) {
    int total = 0, dark = 0;
    for (int pass = 0; pass < 2; pass++) { /* rows, then columns */
        for (int a = 0; a < size; a++) {
            int run = 0, prev = -1;
            unsigned hist = 0;
            for (int b = 0; b < size; b++) {
                int v = pass ? mod[b * size + a] : mod[a * size + b];
                if (v == prev) {
                    run++;
                    if (run == 5) total += 3;
                    else if (run > 5) total++;
                } else {
                    prev = v;
                    run = 1;
                }
                hist = ((hist << 1) | (unsigned)v) & 0x7FF;
                if (b >= 10 && (hist == 0x05D || hist == 0x5D0)) total += 40;
            }
        }
    }
    for (int y = 0; y < size - 1; y++) {
        for (int x = 0; x < size - 1; x++) {
            int c = mod[y * size + x];
            if (c == mod[y * size + x + 1] && c == mod[(y + 1) * size + x] && c == mod[(y + 1) * size + x + 1]) total += 3;
        }
    }
    for (int i = 0; i < size * size; i++) dark += mod[i];
    int k = (dark * 20 - size * size * 10);
    if (k < 0) k = -k;
    k = (k + size * size - 1) / (size * size) - 1;
    if (k > 0) total += k * 10;
    return total;
}

int
qr_encode(const uint8_t* data, int len, uint8_t* modules) {
    static uint8_t cw[MAX_CODEWORDS], all[MAX_CODEWORDS];
    int ver = 0;
    for (int v = 1; v <= 40; v++) {
        int cc = v < 10 ? 8 : 16;
        if ((4 + cc + 8 * len + 7) / 8 <= data_codewords(v)) {
            ver = v;
            break;
        }
    }
    if (!ver) return 0;
    int dcw = data_codewords(ver);
    int cc = ver < 10 ? 8 : 16;

    /* the bit stream: mode 0100, length, bytes, terminator, padding */
    memset(cw, 0, sizeof(cw));
    int nbits = 0;
#define PUT(val, n)                                                                                                    \
    do {                                                                                                               \
        for (int bi = (n)-1; bi >= 0; bi--) {                                                                          \
            cw[nbits >> 3] |= (uint8_t)((((val) >> bi) & 1) << (7 - (nbits & 7)));                                     \
            nbits++;                                                                                                   \
        }                                                                                                              \
    } while (0)
    PUT(4, 4);
    PUT(len, cc);
    for (int i = 0; i < len; i++) PUT(data[i], 8);
    int cap = dcw * 8;
    int term = cap - nbits < 4 ? cap - nbits : 4;
    nbits += term;
    nbits = (nbits + 7) & ~7;
    for (int pad = 0xEC; nbits < cap; pad ^= 0xEC ^ 0x11) {
        cw[nbits >> 3] = (uint8_t)pad;
        nbits += 8;
    }

    /* error correction, interleaved */
    int nb = num_blocks[ver], el = ecc_per_block[ver];
    int raw = raw_modules(ver) / 8;
    int nshort = nb - raw % nb, shortlen = raw / nb;
    uint8_t ecc[30 * 1];
    static uint8_t eccs[25][30];
    const uint8_t* dat[25];
    int dlen[25], k = 0;
    for (int i = 0; i < nb; i++) {
        dlen[i] = shortlen - el + (i < nshort ? 0 : 1);
        dat[i] = cw + k;
        k += dlen[i];
        rs_remainder(dat[i], dlen[i], el, ecc);
        memcpy(eccs[i], ecc, (size_t)el);
    }
    int n = 0;
    for (int i = 0; i <= shortlen - el; i++) {
        for (int j = 0; j < nb; j++) {
            if (i < dlen[j]) all[n++] = dat[j][i];
        }
    }
    for (int i = 0; i < el; i++) {
        for (int j = 0; j < nb; j++) all[n++] = eccs[j][i];
    }

    size = ver * 4 + 17;
    draw_function(ver);
    int bit = 0, total = n * 8;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < size; vert++) {
            for (int j = 0; j < 2; j++) {
                int x = right - j;
                int up = ((right + 1) & 2) == 0;
                int y = up ? size - 1 - vert : vert;
                if (!func[y * size + x] && bit < total) {
                    mod[y * size + x] = (all[bit >> 3] >> (7 - (bit & 7))) & 1;
                    bit++;
                }
            }
        }
    }

    int best = 0, best_pen = 1 << 30;
    for (int m = 0; m < 8; m++) {
        apply_mask(m);
        format_bits(m);
        int p = penalty();
        if (p < best_pen) {
            best_pen = p;
            best = m;
        }
        apply_mask(m); /* undo */
    }
    apply_mask(best);
    format_bits(best);
    memcpy(modules, mod, (size_t)size * (size_t)size);
    return size;
}
