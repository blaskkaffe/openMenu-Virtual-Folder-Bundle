/*
 * bios_audio: the pieces of playing the BIOS menu sounds that do not touch hardware.
 *
 * The console's boot ROM carries the ARM7 driver of the sound chip plus a bank with
 * the menu's sound effects. Loading it on the console (sound.c) takes four steps:
 * reset the ARM, copy the driver, copy the banks and tell the driver where they are
 * (the "data map"), then send it commands. This module parses the bank container and
 * builds the byte images for the data map and the commands, so those parts can be
 * tested on a PC. Layouts come from the boot ROM decompile notes.
 */
#ifndef BIOS_AUDIO_H
#define BIOS_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sound RAM layout used by the driver */
#define BAUDIO_DRIVER_CODE_OFFSET 0x20u  /* ARM code starts this far into the driver entry */
#define BAUDIO_DRIVER_CODE_SIZE 0x79E0u  /* bytes to copy to sound RAM address 0 */
#define BAUDIO_CMD_BUFFER 0x13200u       /* 32 command slots of 16 bytes */
#define BAUDIO_CMD_SLOTS 32
#define BAUDIO_STATUS 0x13400u           /* byte 0: commands pending */
#define BAUDIO_DATA_MAP 0x14000u

#define BAUDIO_MAX_BLOCKS 16

typedef struct baudio_block {
    char tag[5];         /* "SMPB", "SMSB", "SFOB", "SFPB", "SFPW", "SPSR", NUL terminated */
    uint32_t unit;       /* index within its kind */
    uint32_t ram_addr;   /* where it goes in sound RAM */
    uint32_t reserved;   /* space reserved there */
    uint32_t offset;     /* offset of the data inside the container */
    uint32_t size;       /* bytes of data (0 = work area, nothing to copy) */
} baudio_block;

/* Parse the container's directory (32-byte records at its start). Returns the number of
 * valid blocks, 0 if it does not look like a bank container. */
int baudio_parse_banks(const uint8_t* container, size_t container_size, baudio_block* out, int max);

/* Sound RAM address of the data map pair {address, size} for a block, 0 if the block kind
 * does not take part in the data map (e.g. DSP work RAM stream buffers). */
uint32_t baudio_datamap_addr(const baudio_block* b);

/* The two 32-bit words to store at baudio_datamap_addr(). */
void baudio_datamap_entry(const baudio_block* b, uint32_t words[2]);

/* Host commands are 16 bytes: [0] command, [1] result (written by the driver), [2..] parameters. */
#define BAUDIO_CMD_PLAY 0x01
#define BAUDIO_CMD_MASTER_VOLUME 0x81
#define BAUDIO_CMD_STEREO_MONO 0x8A

void baudio_cmd_play(uint8_t slot[16], int player, int bank_unit, int sequence, int priority);
void baudio_cmd_master_volume(uint8_t slot[16], int volume /* 0..15 */);
void baudio_cmd_stereo(uint8_t slot[16], int mono);

/* UI sound effects = sequences of the first sequence bank */
enum {
    BAUDIO_SFX_CURSOR = 0,
    BAUDIO_SFX_CONFIRM = 1,
    BAUDIO_SFX_CANCEL = 2,
    BAUDIO_SFX_ENTER = 3,
    BAUDIO_SFX_ERROR = 4,
    BAUDIO_SFX_CLOCK = 5,
    BAUDIO_SFX_MESSAGE = 6
};

#ifdef __cplusplus
}
#endif

#endif /* BIOS_AUDIO_H */
