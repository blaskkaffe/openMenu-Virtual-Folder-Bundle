/* vmu_files: see vmu_files.h. */
#include <stdlib.h>
#include <string.h>

#include <dc/maple.h>
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
