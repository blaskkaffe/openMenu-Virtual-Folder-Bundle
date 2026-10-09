/*
 * vmu_files: the files on the memory cards for the Files screen. A slot is a socket as in the BIOS:
 * 0..7 = ports A..D, two sockets each (A1 A2 B1 B2 C1 C2 D1 D2).
 */
#pragma once

#define VF_MAX_FILES 200

typedef struct vf_file {
    char name[13];
    int blocks;    /* size in 512 byte blocks */
    int is_game;   /* a VMU game (file type 0xCC) */
    int protect;   /* copy protected */
} vf_file;

/* A memory card is in the socket and ready. */
int vf_present(int slot);

/* Free blocks, -1 if the card cannot be read. */
int vf_free_blocks(int slot);

/* Files of a card, in directory order. Returns the count, -1 on failure. */
int vf_list(int slot, vf_file* out, int max);

enum {
    VF_OK = 0,
    VF_ERR_SOURCE,   /* could not read the file */
    VF_ERR_DEST,     /* the destination card is not ready */
    VF_ERR_FULL,     /* not enough free blocks */
    VF_ERR_GAME,     /* a card can hold only one VMU game */
    VF_ERR_EXISTS,   /* a file of that name is there and overwrite was not asked for */
    VF_ERR_WRITE,    /* writing failed */
    VF_ERR_PROTECTED /* the file is copy protected */
};

/* Check a copy before doing it: VF_OK, or VF_ERR_EXISTS (ask about overwriting), _FULL, _GAME, _DEST,
 * _PROTECTED. */
int vf_copy_check(int src, const vf_file* file, int dst);

/* Copy a file from one card to another. */
int vf_copy(int src, const vf_file* file, int dst, int overwrite);

/* Delete a file. Returns 0 on success. */
int vf_delete(int slot, const vf_file* file);
