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
    int firstblk;  /* where the file starts, and the offset of its header in blocks (for the icon) */
    int hdroff;
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

/* ---- Icons of files ---- */

/* The first icon frame of a file (32x32, ARGB4444) from the header of its VMS data. Returns 0 on
 * success, -1 if the file has no icon or cannot be read. */
int vf_file_icon(int slot, const vf_file* file, unsigned short out[32 * 32]);

/* ---- Look of a card: its icon and colour, which can be changed without formatting ---- */

#define VF_ICON_SHAPES 124 /* the BIOS has 124 icons to choose from */

typedef struct vf_colour {
    const char* name;
    int custom;             /* 0: the standard colour of the console */
    unsigned char bgra[4];  /* blue, green, red, alpha as stored in the card */
} vf_colour;

int vf_colour_count(void);
const vf_colour* vf_colour_get(int index);
unsigned int vf_colour_argb(int index); /* for drawing a swatch (0xAARRGGBB; the standard one is grey) */

/* The BIOS icon `shape` (0..VF_ICON_SHAPES-1) as a 32x32 ARGB4444 picture: set pixels `fg`, others
 * `bg` (both ARGB4444). Reads the icon table of the BIOS ROM. */
void vf_icon_shape_picture(int shape, unsigned short fg, unsigned short bg, unsigned short out[32 * 32]);

/* Current icon shape and colour index of a card (the colour matched to the nearest preset). */
int vf_card_look(int slot, int* shape, int* colour_index);

/* Set the icon and colour of a card. Only the root block and the file ICONDATA_VMS change: no
 * files are touched. Returns 0 on success. */
int vf_set_look(int slot, int shape, int colour_index);

/* Memory reset: erases every file and rebuilds the card with the given icon and colour. */
int vf_format(int slot, int shape, int colour_index);
