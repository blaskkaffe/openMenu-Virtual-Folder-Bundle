/* vmu_files: see vmu_files.h. */
#include <stdlib.h>
#include <string.h>

#include <stdint.h>

#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <dc/vmufs.h>

#include "vmu_files.h"

static maple_device_t*
card(int slot) {
    if (slot < 0 || slot > 7) {
        return NULL;
    }
    maple_device_t* dev = maple_enum_dev(slot / 2, slot % 2 + 1);
    return dev && dev->valid && (dev->info.functions & MAPLE_FUNC_MEMCARD) ? dev : NULL;
}

int
vf_present(int slot) {
    return card(slot) != NULL;
}

int
vf_free_blocks(int slot) {
    maple_device_t* dev = card(slot);
    return dev ? vmufs_free_blocks(dev) : -1;
}

int
vf_list(int slot, vf_file* out, int max) {
    maple_device_t* dev = card(slot);
    vmu_dir_t* dir = NULL;
    int count = 0;
    if (!dev || vmufs_readdir(dev, &dir, &count) < 0) {
        return -1;
    }
    int n = 0;
    for (int i = 0; i < count && n < max; i++) {
        if (dir[i].filetype == 0) {
            continue;
        }
        memcpy(out[n].name, dir[i].filename, 12);
        out[n].name[12] = '\0';
        out[n].blocks = dir[i].filesize;
        out[n].is_game = dir[i].filetype == 0xCC;
        out[n].protect = dir[i].copyprotect == 0xFF;
        out[n].firstblk = dir[i].firstblk;
        out[n].hdroff = dir[i].hdroff;
        memcpy(out[n].time, &dir[i].timestamp, 8);
        n++;
    }
    free(dir);
    return n;
}

static int
has_file(int slot, const char* name, int* game_present) {
    vf_file* list = (vf_file*)malloc(sizeof(vf_file) * VF_MAX_FILES);
    int found = 0;
    if (game_present) {
        *game_present = 0;
    }
    if (!list) {
        return 0;
    }
    int n = vf_list(slot, list, VF_MAX_FILES);
    for (int i = 0; i < n; i++) {
        found |= !strncmp(list[i].name, name, 12);
        if (game_present && list[i].is_game) {
            *game_present = 1;
        }
    }
    free(list);
    return found;
}

int
vf_copy_check(int src, const vf_file* file, int dst) {
    (void)src;
    if (!card(dst)) {
        return VF_ERR_DEST;
    }
    if (file->protect) {
        return VF_ERR_PROTECTED;
    }
    int game_there = 0;
    int exists = has_file(dst, file->name, &game_there);
    if (file->is_game && game_there && !exists) {
        return VF_ERR_GAME;
    }
    int free_blocks = vf_free_blocks(dst);
    if (free_blocks < 0) {
        return VF_ERR_DEST;
    }
    if (!exists && file->blocks > free_blocks) {
        return VF_ERR_FULL;
    }
    return exists ? VF_ERR_EXISTS : VF_OK;
}

int
vf_copy(int src, const vf_file* file, int dst, int overwrite) {
    maple_device_t* from = card(src);
    maple_device_t* to = card(dst);
    if (!from) {
        return VF_ERR_SOURCE;
    }
    if (!to) {
        return VF_ERR_DEST;
    }
    int check = vf_copy_check(src, file, dst);
    if (check != VF_OK && !(check == VF_ERR_EXISTS && overwrite)) {
        return check;
    }
    void* data = NULL;
    int size = 0;
    if (vmufs_read(from, file->name, &data, &size) < 0 || !data) {
        return VF_ERR_SOURCE;
    }
    int flags = VMUFS_OVERWRITE | (file->is_game ? VMUFS_VMUGAME : 0);
    int rc = vmufs_write(to, file->name, data, size, flags);
    free(data);
    return rc < 0 ? VF_ERR_WRITE : VF_OK;
}

int
vf_delete(int slot, const vf_file* file) {
    maple_device_t* dev = card(slot);
    return dev && vmufs_delete(dev, file->name) >= 0 ? 0 : -1;
}

/* ---- Icons of files ---------------------------------------------------------------------------- */

int
vf_file_icon(int slot, const vf_file* file, unsigned short out[32 * 32]) {
    maple_device_t* dev = card(slot);
    if (!dev || !strncmp(file->name, "ICONDATA_VMS", 12)) {
        return -1;
    }
    vmu_root_t root;
    uint8_t block[2 * 512];
    int rc = -1;
    vmufs_mutex_lock();
    if (vmufs_root_read(dev, &root) == 0 && root.fat_size > 0 && root.fat_size <= 4) {
        uint16_t* fat = (uint16_t*)malloc((size_t)root.fat_size * 512);
        if (fat && vmufs_fat_read(dev, &root, fat) == 0) {
            int blk = file->firstblk;
            for (int i = 0; i < file->hdroff && blk < 256; i++) {
                blk = fat[blk];
            }
            int next = blk < 256 ? fat[blk] : 0xFFFA;
            if (blk < 256 && vmu_block_read(dev, (uint16_t)blk, block) == 0) {
                memset(block + 512, 0, 512);
                if (next < 256) {
                    vmu_block_read(dev, (uint16_t)next, block + 512);
                }
                rc = 0;
            }
        }
        free(fat);
    }
    vmufs_mutex_unlock();
    if (rc != 0) {
        return -1;
    }
    /* VMS header: 0x40 icon count, 0x60 palette (16 x ARGB4444), 0x80 the first icon (4 bits a pixel) */
    int icons = block[0x40] | (block[0x41] << 8);
    if (icons < 1) {
        return -1;
    }
    uint16_t palette[16];
    for (int i = 0; i < 16; i++) {
        palette[i] = (uint16_t)(block[0x60 + i * 2] | (block[0x61 + i * 2] << 8));
    }
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            uint8_t b = block[0x80 + (y * 32 + x) / 2];
            out[y * 32 + x] = palette[(x & 1) ? (b & 0x0F) : (b >> 4)];
        }
    }
    return 0;
}

/* ---- Look of a card ------------------------------------------------------------------------------ */

static const vf_colour colours[] = {
    {"Standard", 0, {0, 0, 0, 0}},
    {"Blue", 1, {0xFF, 0x70, 0x20, 0xFF}},
    {"Red", 1, {0x30, 0x30, 0xFF, 0xFF}},
    {"Green", 1, {0x30, 0xD0, 0x40, 0xFF}},
    {"Yellow", 1, {0x30, 0xF0, 0xF0, 0xFF}},
    {"Orange", 1, {0x20, 0x90, 0xFF, 0xFF}},
    {"Purple", 1, {0xD0, 0x40, 0xA0, 0xFF}},
    {"White", 1, {0xF0, 0xF0, 0xF0, 0xFF}},
    {"Black", 1, {0x20, 0x20, 0x20, 0xFF}},
    {"Transparent", 1, {0xFF, 0xFF, 0xFF, 0x60}},
};

int
vf_colour_count(void) {
    return (int)(sizeof(colours) / sizeof(colours[0]));
}

const vf_colour*
vf_colour_get(int index) {
    return &colours[index < 0 || index >= vf_colour_count() ? 0 : index];
}

unsigned int
vf_colour_argb(int index) {
    const vf_colour* c = vf_colour_get(index);
    if (!c->custom) {
        return 0xFFA0A0A0u;
    }
    return ((unsigned)c->bgra[3] << 24) | ((unsigned)c->bgra[2] << 16) | ((unsigned)c->bgra[1] << 8) | c->bgra[0];
}

/* The icon shapes of the BIOS are 129 monochrome 32x32 pictures in its font ROM; shape n is picture n + 5. */
#define ICON_TABLE ((const uint8_t*)(0xA0100020u + 0x7EF30u))

void
vf_icon_shape_picture(int shape, unsigned short fg, unsigned short bg, unsigned short out[32 * 32]) {
    shape = shape < 0 || shape >= VF_ICON_SHAPES ? 0 : shape;
    const uint8_t* g = ICON_TABLE + (shape + 5) * 128;
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            out[y * 32 + x] = (g[y * 4 + x / 8] >> (7 - (x & 7))) & 1 ? fg : bg;
        }
    }
}

int
vf_card_look(int slot, int* shape, int* colour_index) {
    maple_device_t* dev = card(slot);
    vmu_root_t root;
    int rc;
    if (!dev) {
        return -1;
    }
    vmufs_mutex_lock();
    rc = vmufs_root_read(dev, &root);
    vmufs_mutex_unlock();
    if (rc != 0) {
        return -1;
    }
    *shape = root.icon_shape < VF_ICON_SHAPES ? root.icon_shape : 0;
    *colour_index = 0;
    if (root.use_custom) {
        int best = 1 << 30;
        for (int i = 1; i < vf_colour_count(); i++) {
            int d = 0;
            for (int k = 0; k < 4; k++) {
                int e = (int)colours[i].bgra[k] - (int)root.custom_color[k];
                d += e * e;
            }
            if (d < best) {
                best = d;
                *colour_index = i;
            }
        }
    }
    return 0;
}

/* ICONDATA_VMS: the icon the VMU shows on its own screen and the BIOS shows for the card:
 * 0x00 description (16 bytes), 0x10 offset of the monochrome icon, 0x14 offset of the colour icon (0: none),
 * then the 32x32 1 bit icon and the colour icon (16 palette entries, then 32x32 at 4 bits a pixel). The BIOS
 * reads the offsets at 0x10 and 0x14 (vmu_decode_file_header 0x8C023B40). */
static int
write_icondata(maple_device_t* dev, int shape, int colour_index) {
    uint8_t file[0xA0 + 32 + 512];
    memset(file, 0, sizeof(file));
    memcpy(file, "MEMORY CARD     ", 16);
    file[0x10] = 0x20;
    file[0x14] = 0xA0;
    const uint8_t* g = ICON_TABLE + (shape + 5) * 128;
    memcpy(file + 0x20, g, 128);
    const vf_colour* c = vf_colour_get(colour_index);
    uint16_t back = c->custom ? (uint16_t)(0xF000 | ((c->bgra[2] >> 4) << 8) | ((c->bgra[1] >> 4) << 4) | (c->bgra[0] >> 4)) : 0xFAAA;
    uint16_t pal[2] = {back, 0xF000}; /* 0: the card colour, 1: black */
    for (int i = 0; i < 2; i++) {
        file[0xA0 + i * 2] = (uint8_t)(pal[i] & 0xFF);
        file[0xA0 + i * 2 + 1] = (uint8_t)(pal[i] >> 8);
    }
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            int set = (g[y * 4 + x / 8] >> (7 - (x & 7))) & 1;
            uint8_t* b = file + 0xA0 + 32 + (y * 32 + x) / 2;
            *b |= (uint8_t)(set << ((x & 1) ? 0 : 4));
        }
    }
    return vmufs_write(dev, "ICONDATA_VMS", file, (int)sizeof(file), VMUFS_OVERWRITE) < 0 ? -1 : 0;
}

static int
write_root_look(maple_device_t* dev, int shape, int colour_index) {
    vmu_root_t root;
    vmufs_mutex_lock();
    int rc = vmufs_root_read(dev, &root);
    if (rc == 0) {
        const vf_colour* c = vf_colour_get(colour_index);
        root.icon_shape = (uint16_t)shape;
        root.use_custom = (uint8_t)c->custom;
        memcpy(root.custom_color, c->bgra, 4);
        rc = vmufs_root_write(dev, &root);
    }
    vmufs_mutex_unlock();
    return rc;
}

int
vf_set_look(int slot, int shape, int colour_index) {
    maple_device_t* dev = card(slot);
    if (!dev) {
        return -1;
    }
    shape = shape < 0 || shape >= VF_ICON_SHAPES ? 0 : shape;
    if (write_root_look(dev, shape, colour_index) != 0) {
        return -1;
    }
    return write_icondata(dev, shape, colour_index); /* the card keeps working even if this file cannot be written */
}

static int
root_valid(const vmu_root_t* root) {
    for (int i = 0; i < 16; i++) {
        if (root->magic[i] != 0x55) {
            return 0;
        }
    }
    return root->fat_size == 1 && root->dir_size > 0 && root->dir_size <= 20 && root->dir_loc < 256 && root->fat_loc < 256;
}

/* A new root block as a formatted card has it: 256 blocks, FAT at 254, 13 directory blocks from 253, 200 for files. */
static void
root_new(vmu_root_t* root) {
    memset(root, 0, sizeof(*root));
    memset(root->magic, 0x55, sizeof(root->magic));
    uint8_t* r = (uint8_t*)root;
    r[0x40] = 0xFF; /* last block */
    r[0x44] = 0xFF; /* the root block */
    root->fat_loc = 254;
    root->fat_size = 1;
    root->dir_loc = 253;
    root->dir_size = 13;
    root->blk_cnt = 200;
    r[0x52] = 31;   /* save area / VMU game area sizes as the BIOS writes them */
    r[0x54] = 128;
    vmufs_dir_fill_time((vmu_dir_t*)((uint8_t*)root + 0x30 - 16)); /* fills the 8 time bytes at 0x30 */
}

int
vf_format(int slot, int shape, int colour_index) {
    maple_device_t* dev = card(slot);
    vmu_root_t root;
    uint8_t zero[512];
    int rc = -1;
    if (!dev) {
        return -1;
    }
    memset(zero, 0, sizeof(zero));
    vmufs_mutex_lock();
    if (vmufs_root_read(dev, &root) != 0 || !root_valid(&root)) {
        root_new(&root);
        if (vmufs_root_write(dev, &root) != 0) {
            vmufs_mutex_unlock();
            return -1;
        }
    }
    if (root_valid(&root)) {
        uint16_t fat[256];
        for (int i = 0; i < 256; i++) {
            fat[i] = 0xFFFC; /* free */
        }
        fat[255] = 0xFFFA; /* the root block */
        fat[root.fat_loc] = 0xFFFA;
        for (int i = 0; i < root.dir_size; i++) { /* the directory runs downwards from dir_loc */
            int blk = root.dir_loc - i;
            fat[blk] = i == root.dir_size - 1 ? 0xFFFA : (uint16_t)(blk - 1);
        }
        rc = 0;
        for (int i = 0; i < root.dir_size && rc == 0; i++) {
            rc = vmu_block_write(dev, (uint16_t)(root.dir_loc - i), zero);
        }
        if (rc == 0) {
            rc = vmufs_fat_write(dev, &root, fat);
        }
    }
    vmufs_mutex_unlock();
    if (rc != 0) {
        return -1;
    }
    return vf_set_look(slot, shape, colour_index);
}

int
vf_format_raw(int slot, int shape, int custom, const unsigned char bgra[4]) {
    /* the nearest preset carries the colour through vf_format(); the exact colour is written afterwards */
    if (vf_format(slot, shape, 0) != 0) {
        return -1;
    }
    maple_device_t* dev = card(slot);
    vmu_root_t root;
    int rc = -1;
    vmufs_mutex_lock();
    if (dev && vmufs_root_read(dev, &root) == 0) {
        root.use_custom = (uint8_t)(custom != 0);
        memcpy(root.custom_color, bgra, 4);
        rc = vmufs_root_write(dev, &root);
    }
    vmufs_mutex_unlock();
    return rc;
}

/* ---- The File screen's view of a card ------------------------------------------------------------------- */

static unsigned short
mono_px(const uint8_t* bits, int x, int y, unsigned short fg) {
    return (bits[y * 4 + x / 8] >> (7 - (x & 7))) & 1 ? fg : 0xFBC6;
}

int
vf_card_info_read(int slot, vf_card_info* out) {
    maple_device_t* dev = card(slot);
    vmu_root_t root;
    memset(out, 0, sizeof(*out));
    out->colour = 0xFFFFFFFFu;
    if (!dev) {
        return out->status = VF_CARD_NONE;
    }
    vmufs_mutex_lock();
    int rc = vmufs_root_read(dev, &root);
    vmufs_mutex_unlock();
    if (rc != 0) {
        return out->status = VF_CARD_ERROR;
    }
    if (!root_valid(&root)) {
        /* not formatted: the BIOS shows picture 0 in orange and the card in 0xFF2F5F9F */
        const uint8_t* g = ICON_TABLE;
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 32; x++) {
                out->icon[y * 32 + x] = mono_px(g, x, y, 0xFD00);
            }
        }
        out->colour = 0xFF2F5F9Fu;
        return out->status = VF_CARD_UNFORMATTED;
    }
    out->shape = root.icon_shape < 124 ? root.icon_shape : 0;
    if (root.use_custom) {
        out->colour = ((unsigned)root.custom_color[3] << 24) | ((unsigned)root.custom_color[2] << 16) |
                      ((unsigned)root.custom_color[1] << 8) | root.custom_color[0];
    }
    out->total_blocks = root.blk_cnt ? root.blk_cnt : 200;
    out->free_blocks = vmufs_free_blocks(dev);
    if (out->free_blocks < 0) {
        out->free_blocks = 0;
    }
    /* the card's picture: ICONDATA_VMS if there is one, else the root block's icon shape */
    void* data = NULL;
    int size = 0;
    int done = 0;
    if (vmufs_read(dev, "ICONDATA_VMS", &data, &size) == 0 && data && size >= 0x20) {
        const uint8_t* d = (const uint8_t*)data;
        uint32_t mono = d[0x10] | (d[0x11] << 8) | ((uint32_t)d[0x12] << 16) | ((uint32_t)d[0x13] << 24);
        uint32_t colour = d[0x14] | (d[0x15] << 8) | ((uint32_t)d[0x16] << 16) | ((uint32_t)d[0x17] << 24);
        if (colour && colour + 0x20 + 512 <= (uint32_t)size) {
            const uint8_t* pal = d + colour;
            for (int i = 0; i < 1024; i++) {
                uint8_t b = d[colour + 0x20 + i / 2];
                int n = (i & 1) ? (b & 15) : (b >> 4);
                out->icon[i] = (unsigned short)(pal[n * 2] | (pal[n * 2 + 1] << 8));
            }
            done = 1;
        } else if (mono + 128 <= (uint32_t)size) {
            for (int y = 0; y < 32; y++) {
                for (int x = 0; x < 32; x++) {
                    out->icon[y * 32 + x] = mono_px(d + mono, x, y, 0xF225);
                }
            }
            done = 1;
        }
    }
    free(data);
    if (!done) {
        const uint8_t* g = ICON_TABLE + (out->shape + 5) * 128;
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 32; x++) {
                out->icon[y * 32 + x] = mono_px(g, x, y, 0xF225);
            }
        }
    }
    return out->status = VF_CARD_READY;
}

/* Read `count` blocks of a file starting `skip` blocks in. Returns the blocks read. */
static int
read_file_blocks(maple_device_t* dev, int first, int skip, int count, uint8_t* out) {
    vmu_root_t root;
    int got = 0;
    vmufs_mutex_lock();
    if (vmufs_root_read(dev, &root) == 0 && root.fat_size > 0 && root.fat_size <= 4) {
        uint16_t* fat = (uint16_t*)malloc((size_t)root.fat_size * 512);
        if (fat && vmufs_fat_read(dev, &root, fat) == 0) {
            int blk = first;
            for (int i = 0; i < skip && blk < 256; i++) {
                blk = fat[blk];
            }
            while (got < count && blk < 256) {
                if (vmu_block_read(dev, (uint16_t)blk, out + got * 512) != 0) {
                    break;
                }
                got++;
                blk = fat[blk];
            }
        }
        free(fat);
    }
    vmufs_mutex_unlock();
    return got;
}

static void
copy_text(char* dst, const uint8_t* src, int n) {
    memcpy(dst, src, (size_t)n);
    dst[n] = '\0';
    for (int i = 0; i < n; i++) {
        if ((uint8_t)dst[i] < 0x20) {
            dst[i] = ' ';
        }
    }
}

int
vf_file_header(int slot, const vf_file* file, vf_header* out) {
    maple_device_t* dev = card(slot);
    memset(out, 0, sizeof(*out));
    memset(out->vmdesc, ' ', 16);
    memset(out->desc, ' ', 32);
    memset(out->app, ' ', 16);
    if (!dev || !strncmp(file->name, "ICONDATA_VMS", 12)) {
        return -1;
    }
    uint8_t buf[4 * 512];
    int got = read_file_blocks(dev, file->firstblk, file->hdroff, 4, buf);
    if (got < 1) {
        return -1;
    }
    copy_text(out->vmdesc, buf, 16);
    copy_text(out->desc, buf + 0x10, 32);
    copy_text(out->app, buf + 0x30, 16);
    out->icons = buf[0x40] | (buf[0x41] << 8);
    if (out->icons < 0 || out->icons > 3) {
        out->icons = 0; /* as the BIOS: more than 3 frames is not a header it trusts */
    }
    out->speed = (buf[0x42] | (buf[0x43] << 8)) * 2;
    out->eyecatch = buf[0x44] | (buf[0x45] << 8);
    if (out->eyecatch > 3) {
        out->eyecatch = 0;
    }
    for (int i = 0; i < 16; i++) {
        out->palette[i] = (unsigned short)(buf[0x60 + i * 2] | (buf[0x61 + i * 2] << 8));
    }
    int need = 0x80 + out->icons * 512;
    if (need > got * 512) {
        out->icons = (got * 512 - 0x80) / 512;
    }
    memcpy(out->bitmaps, buf + 0x80, (size_t)out->icons * 512);
    return 0;
}

int
vf_file_eyecatch(int slot, const vf_file* file, int type, int icons, unsigned short out[72 * 56]) {
    static const int sizes[4] = {0, 72 * 56 * 2, 512 + 72 * 56, 32 + 72 * 56 / 2};
    maple_device_t* dev = card(slot);
    if (!dev || type < 1 || type > 3) {
        return -1;
    }
    int start = 0x80 + icons * 512, len = sizes[type];
    int blocks = (start + len + 511) / 512;
    uint8_t* buf = (uint8_t*)malloc((size_t)blocks * 512);
    if (!buf) {
        return -1;
    }
    int rc = -1;
    if (read_file_blocks(dev, file->firstblk, file->hdroff, blocks, buf) == blocks) {
        const uint8_t* d = buf + start;
        for (int i = 0; i < 72 * 56; i++) {
            if (type == 1) {
                out[i] = (unsigned short)(d[i * 2] | (d[i * 2 + 1] << 8));
            } else if (type == 2) {
                int n = d[512 + i];
                out[i] = (unsigned short)(d[n * 2] | (d[n * 2 + 1] << 8));
            } else {
                uint8_t b = d[32 + i / 2];
                int n = (i & 1) ? (b & 15) : (b >> 4);
                out[i] = (unsigned short)(d[n * 2] | (d[n * 2 + 1] << 8));
            }
        }
        rc = 0;
    }
    free(buf);
    return rc;
}
