/* favorites: see favorites.h. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <backend/gd_item.h>
#include <backend/gd_list.h>

#include "favorites.h"

#ifdef _arch_dreamcast
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fat/fs_fat.h>
#include <sd_savefile.h>

#include "vmu_files.h"
#endif

#define FAV_FILE "OPENMENU.FAV"        /* the memory card file (12 characters is the most a VMS name can have) */
#define FAV_SD_FILE "/sd/OPENMENU/FAVORITES.DAT"
#define FAV_SD_DIR "/sd/OPENMENU"
#define FAV_VERSION 1
#define FAV_CARD_BLOCKS 3               /* header 0x80 + icon 512 + data 8 + 4 * FAV_MAX = 1048 bytes */

static uint32_t hashes[FAV_MAX];
static int hash_count;
static const gd_item* items[FAV_MAX]; /* the hashes that are games of this card */
static int item_count;
static int last_save_ok = 1;
static int card_slot = -1;            /* the memory card that has the file, -1 if none yet */
static uint8_t fork_flags;            /* the spare byte of the header: settings 1.7.1 has no place for, see favorites.h */

/* ---- The file: "OMFV", version, 0, count (u16), then the hashes, all little endian ---- */

static int
fav_pack(uint8_t* out) {
    memcpy(out, "OMFV", 4);
    out[4] = FAV_VERSION;
    out[5] = fork_flags;
    out[6] = (uint8_t)(hash_count & 0xFF);
    out[7] = (uint8_t)(hash_count >> 8);
    for (int i = 0; i < hash_count; i++) {
        for (int b = 0; b < 4; b++) {
            out[8 + i * 4 + b] = (uint8_t)(hashes[i] >> (8 * b));
        }
    }
    return 8 + hash_count * 4;
}

static int
fav_unpack(const uint8_t* in, int size) {
    if (size < 8 || memcmp(in, "OMFV", 4) != 0 || in[4] != FAV_VERSION) {
        return -1;
    }
    int n = in[6] | (in[7] << 8);
    if (n > FAV_MAX || 8 + n * 4 > size) {
        return -1;
    }
    fork_flags = in[5];
    for (int i = 0; i < n; i++) {
        hashes[i] = (uint32_t)in[8 + i * 4] | ((uint32_t)in[9 + i * 4] << 8) | ((uint32_t)in[10 + i * 4] << 16)
                    | ((uint32_t)in[11 + i * 4] << 24);
    }
    hash_count = n;
    return 0;
}

/* ---- On a memory card the data is wrapped as a VMS file: a header (descriptions, one icon) in front ---- */

#define VMS_HDR 0x80
#define VMS_ICON 512
#define VMS_DATA (VMS_HDR + VMS_ICON)

static uint16_t
vms_crc(const uint8_t* buf, int size) {
    int n = 0;
    for (int i = 0; i < size; i++) {
        n ^= buf[i] << 8;
        for (int c = 0; c < 8; c++) {
            n = (n & 0x8000) ? (n << 1) ^ 0x1021 : n << 1;
        }
    }
    return (uint16_t)(n & 0xFFFF);
}

static void
put_text(uint8_t* dst, int len, const char* text) {
    memset(dst, ' ', (size_t)len);
    memcpy(dst, text, strlen(text) < (size_t)len ? strlen(text) : (size_t)len);
}

/* Returns the size of the file written into `out` (at least VMS_DATA + 8 + 4 * FAV_MAX bytes). */
static int
vms_wrap(uint8_t* out) {
    uint8_t payload[8 + 4 * FAV_MAX];
    int len = fav_pack(payload);
    memset(out, 0, VMS_DATA);
    put_text(out + 0x00, 16, "OPENMENU FAVS");
    put_text(out + 0x10, 32, "openMenu favorite games");
    put_text(out + 0x30, 16, "openMenu");
    out[0x40] = 1; /* one icon, not animated, no eyecatch */
    out[0x48] = (uint8_t)(len & 0xFF);
    out[0x49] = (uint8_t)(len >> 8);
    out[0x60] = 0x00; /* palette entry 0: transparent (ARGB4444) */
    out[0x61] = 0x00;
    out[0x62] = 0xD0; /* entry 1: gold 0xFFD0 */
    out[0x63] = 0xFF;
    for (int y = 0; y < 32; y++) { /* a gold diamond */
        for (int x = 0; x < 32; x++) {
            int d = (x < 16 ? 15 - x : x - 16) + (y < 16 ? 15 - y : y - 16);
            if (d <= 14) {
                out[VMS_HDR + (y * 32 + x) / 2] |= (uint8_t)(1 << ((x & 1) ? 0 : 4));
            }
        }
    }
    memcpy(out + VMS_DATA, payload, (size_t)len);
    int total = VMS_DATA + len;
    uint16_t crc = vms_crc(out, total); /* over the whole file with the crc field zero */
    out[0x46] = (uint8_t)(crc & 0xFF);
    out[0x47] = (uint8_t)(crc >> 8);
    return total;
}

static int
vms_unwrap(const uint8_t* in, int size) {
    if (size < VMS_DATA + 8) {
        return -1;
    }
    int len = in[0x48] | (in[0x49] << 8) | (in[0x4A] << 16) | (in[0x4B] << 24);
    if (in[0x40] != 1 || len < 8 || VMS_DATA + len > size) {
        return -1;
    }
    return fav_unpack(in + VMS_DATA, len);
}

/* ---- Storage (the console only) ---- */

#ifdef _arch_dreamcast
static int
sd_load(void) {
    if (!sd_savefile_available()) {
        return -1;
    }
    uint8_t buf[8 + 4 * FAV_MAX];
    int fd = open(FAV_SD_FILE, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    ssize_t n = read(fd, buf, sizeof(buf));
    close(fd);
    return n > 0 ? fav_unpack(buf, (int)n) : -1;
}

static int
card_load(void) {
    for (int slot = 0; slot < 8; slot++) {
        uint8_t* data = NULL;
        int size = 0;
        if (vf_read_named(slot, FAV_FILE, &data, &size) == 0 && data) {
            int rc = vms_unwrap(data, size);
            free(data);
            if (rc == 0) {
                card_slot = slot;
                return 0;
            }
        }
    }
    return -1;
}

static int
sd_save(void) {
    if (!sd_savefile_available()) {
        return -1;
    }
    uint8_t buf[8 + 4 * FAV_MAX];
    int len = fav_pack(buf);
    mkdir(FAV_SD_DIR, 0755); /* fails if it is there already, which is fine */
    int fd = open(FAV_SD_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }
    ssize_t n = write(fd, buf, (size_t)len);
    fs_fat_sync(SD_MOUNT_PATH);
    close(fd);
    return n == len ? 0 : -1;
}

static int
card_save(void) {
    uint8_t file[VMS_DATA + 8 + 4 * FAV_MAX];
    int size = vms_wrap(file);
    if (card_slot >= 0 && vf_write_named(card_slot, FAV_FILE, file, size) == 0) {
        return 0;
    }
    for (int slot = 0; slot < 8; slot++) { /* the first card with room */
        if (vf_present(slot) && vf_free_blocks(slot) >= FAV_CARD_BLOCKS && vf_write_named(slot, FAV_FILE, file, size) == 0) {
            card_slot = slot;
            return 0;
        }
    }
    return -1;
}
#endif

/* ---- Public ---- */

static void
resolve(void) {
    item_count = 0;
    for (int i = 0; i < hash_count; i++) {
        const gd_item* it = list_find_by_hash(hashes[i]);
        if (it) {
            items[item_count++] = it;
        }
    }
}

void
favorites_init(void) {
    hash_count = 0;
    fork_flags = 0;
    card_slot = -1;
#ifdef _arch_dreamcast
    if (sd_load() != 0) {
        hash_count = 0;
        card_load();
    } else {
        card_load(); /* only to learn which card has the file */
        if (card_slot >= 0) {
            /* the SD copy wins: reread it, card_load replaced the list */
            hash_count = 0;
            sd_load();
        }
    }
#endif
    resolve();
}

int
favorites_count(void) {
    return item_count;
}

const gd_item*
favorites_item(int index) {
    return index >= 0 && index < item_count ? items[index] : NULL;
}

static int
index_of(unsigned int h) {
    for (int i = 0; i < hash_count; i++) {
        if (hashes[i] == h) {
            return i;
        }
    }
    return -1;
}

int
favorites_is(const gd_item* item) {
    return item && index_of(gd_item_recent_hash(item)) >= 0;
}

int
favorites_toggle(const gd_item* item) {
    if (!item) {
        return -1;
    }
    unsigned int h = gd_item_recent_hash(item);
    int at = index_of(h);
    int now;
    if (at >= 0) {
        for (int i = at; i + 1 < hash_count; i++) {
            hashes[i] = hashes[i + 1];
        }
        hash_count--;
        now = 0;
    } else {
        if (hash_count >= FAV_MAX) {
            return -1;
        }
        for (int i = hash_count; i > 0; i--) {
            hashes[i] = hashes[i - 1];
        }
        hashes[0] = h;
        hash_count++;
        now = 1;
    }
    resolve();
#ifdef _arch_dreamcast
    int sd = sd_save();
    int card = card_save();
    last_save_ok = sd == 0 || card == 0;
#endif
    return now;
}

int
favorites_save_ok(void) {
    return last_save_ok;
}

unsigned
favorites_flags(void) {
    return fork_flags;
}

void
favorites_set_flags(unsigned flags) {
    if ((flags & 0xFF) == fork_flags) {
        return;
    }
    fork_flags = (uint8_t)flags;
#ifdef _arch_dreamcast
    int sd = sd_save();
    int card = card_save();
    last_save_ok = sd == 0 || card == 0;
#endif
}
