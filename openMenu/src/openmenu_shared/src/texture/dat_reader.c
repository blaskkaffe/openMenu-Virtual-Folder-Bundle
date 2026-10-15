/*
 * File: reader.c
 * Project: dat_builder
 * File Created: Tuesday, 8th June 2021 2:47:35 pm
 * Author: Hayden Kowalchuk
 * -----
 * Copyright (c) 2021 Hayden Kowalchuk, Hayden Kowalchuk
 * License: BSD 3-clause "New" or "Revised" License,
 * http://www.opensource.org/licenses/BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uthash.h>

#include <backend/dat_format.h>

/* Define configure constants */
/* only defined when building the binary tool */
#ifdef STANDALONE_BINARY
#define DEBUG
#endif

#ifdef DEBUG
#define DBG_PRINT(...) printf(__VA_ARGS__)
#else
#define DBG_PRINT(...)
#endif

typedef struct bin_item_raw {
    char ID[12];
    uint32_t offset;
} bin_item_raw;

int
DAT_init(dat_file* bin) {
    memset(bin, '\0', sizeof(dat_file));
    return 0;
}

/* Reading and seeking on the open DAT, for the console and the tool alike. Return 0 on success. */
#ifndef STANDALONE_BINARY
#define DAT_READ(h, buf, n) (fs_read((h), (buf), (n)) == (ssize_t)(n) ? 0 : -1)
#define DAT_SEEK(h, off) (fs_seek((h), (off), SEEK_SET) == (off_t)(off) ? 0 : -1)
#define DAT_CLOSE(h) fs_close(h)
#else
#define DAT_READ(h, buf, n) (fread((buf), (n), 1, (h)) == 1 ? 0 : -1)
#define DAT_SEEK(h, off) (fseek((h), (long)(off), SEEK_SET) == 0 ? 0 : -1)
#define DAT_CLOSE(h) fclose(h)
#endif

#define DAT_MAX_CHUNK_SIZE (4u * 1024u * 1024u) /* far more than any picture */
#define DAT_MAX_CHUNKS 100000u

int
DAT_load_parse(dat_file* bin, const char* path) {
#ifndef STANDALONE_BINARY
    file_t bin_fd;
#else
    FILE* bin_fd;
#endif
    bin_header file_header;

#ifdef STANDALONE_BINARY
    bin_fd = fopen(path, "rb");
    const char* filename_safe = path;
#else
    char filename_safe[128];
    snprintf(filename_safe, 127, "/cd/%s", path);

    bin_fd = fs_open(filename_safe, O_RDONLY);
#endif

#ifndef STANDALONE_BINARY
    if (bin_fd == -1)
#else
    if (!bin_fd)
#endif
    {
        printf("DAT:Error Cant read input %s!\n", filename_safe);
        return 1;
    }

    printf("DAT:Open %s (%s)\n", filename_safe, path);

    /* the header, checked before anything in it is used */
    if (DAT_READ(bin_fd, &file_header, sizeof(bin_header)) != 0 || file_header.magic.rich.version != 1
        || file_header.chunk_size == 0 || file_header.chunk_size > DAT_MAX_CHUNK_SIZE || file_header.num_chunks == 0
        || file_header.num_chunks > DAT_MAX_CHUNKS) {
        printf("DAT:Error Incorrect input file format!\n");
        DAT_CLOSE(bin_fd);
        return 1;
    }

    /* setup basic bin file info */
    bin_item* items = malloc(file_header.num_chunks * sizeof(bin_item));
    if (!items) {
        printf("%s no free memory\n", __func__);
        DAT_CLOSE(bin_fd);
        return 1;
    }
    memset(items, 0, file_header.num_chunks * sizeof(bin_item));

    /* the table: every entry must be there; the IDs are NUL terminated here whatever the file holds */
    for (unsigned int i = 0; i < file_header.num_chunks; i++) {
        if (DAT_READ(bin_fd, &items[i], sizeof(bin_item_raw)) != 0) {
            printf("DAT:Error %s: table cut short\n", filename_safe);
            free(items);
            DAT_CLOSE(bin_fd);
            return 1;
        }
        items[i].ID[sizeof(items[i].ID) - 1] = '\0';
    }

    bin->chunk_size = file_header.chunk_size;
    bin->num_chunks = file_header.num_chunks;
    bin->handle = bin_fd;
    bin->items = items;
    bin->hash = NULL;
    for (unsigned int i = 0; i < file_header.num_chunks; i++) {
        bin_item* dup = NULL;
        HASH_FIND_STR(bin->hash, items[i].ID, dup);
        if (!dup) { /* a repeated ID would make the hash table inconsistent: the first one counts */
            HASH_ADD_STR(bin->hash, ID, &items[i]);
        }
    }

    /* Leave our handle in a handy place in case we need to read after */
    DAT_SEEK(bin->handle, items[0].offset * bin->chunk_size);
    return 0;
}

void
DAT_info(const dat_file* bin) {
    DBG_PRINT("DAT:Stats\nChunk Size: %u\nNum Chunks: %u\n\n", bin->chunk_size, bin->num_chunks);
    for (unsigned int i = 0; i < bin->num_chunks; i++) {
        DBG_PRINT("Record[%u] %s at 0x%X\n", bin->items[i].offset, bin->items[i].ID,
                  (unsigned int)(bin->items[i].offset * bin->chunk_size));
    }
    DBG_PRINT("\n");
}

uint32_t
DAT_get_offset_by_ID(const dat_file* bin, const char* ID) {
    const bin_item* item;
    uint32_t ret;

    HASH_FIND_STR(bin->hash, ID, item);
    if (item) {
        ret = item->offset * bin->chunk_size;
    } else {
        ret = 0;
    }

    return ret;
}

uint32_t
DAT_get_index_by_ID(const dat_file* bin, const char* ID) {
    const bin_item* item;
    uint32_t ret;

    HASH_FIND_STR(bin->hash, ID, item);
    if (item) {
        ret = item->offset;
    } else {
        ret = 0xFFFFFFFF;
    }

    return ret;
}

int
DAT_read_file_by_ID(const dat_file* bin, const char* ID, void* buf) {
    uint32_t offset = DAT_get_offset_by_ID(bin, ID);
    if (!offset) {
        return 0;
    }
    /* a seek or read that fails or comes up short is a failure, not a picture */
    return DAT_SEEK(bin->handle, offset) == 0 && DAT_READ(bin->handle, buf, bin->chunk_size) == 0;
}

/* chunk_num counts chunks from the start of the file (chunk 0 holds the header): what the table's offsets are in */
int
DAT_read_file_by_num(const dat_file* bin, uint32_t chunk_num, void* buf) {
    if (chunk_num == 0 || chunk_num > bin->num_chunks + 1u) {
        return 0;
    }
    uint32_t offset = chunk_num * bin->chunk_size;
    return DAT_SEEK(bin->handle, offset) == 0 && DAT_READ(bin->handle, buf, bin->chunk_size) == 0;
}
