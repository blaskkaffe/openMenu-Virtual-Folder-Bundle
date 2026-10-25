/* serial_vmu: see serial_vmu.h. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <backend/gd_item.h>
#include <backend/gd_list.h>
#include <openmenu_savefile.h>
#include <openmenu_settings.h>

#include "serial_vmu.h"

#ifndef SVMU_HOST_TEST
#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <fat/fs_fat.h>
#define SVMU_SD "/sd"
#define SVMU_EMPTY_VMU "/cd/EMPTY.VMU" /* a BIOS-formatted blank card, on the menu disc */
#endif

#define BLOCKS 256
#define BLOCK_SIZE 512
#define TOTAL_SIZE (BLOCKS * BLOCK_SIZE)
#define SAVES_DIR SVMU_SD "/OPENMENU/SAVES"
#define LASTDISC SVMU_SD "/OPENMENU/LASTDISC.TXT"
#define SLOT 1

enum { ASK_NONE, ASK_NO_SD, ASK_NO_VMU, ASK_FIRST_TIME, ASK_CORRUPT, ASK_WIPE, ASK_RESTORE_FAILED, ASK_BACKUP_FAILED,
       ASK_BACKUP_NO_VMU, ASK_PENDING };
enum { ACT_CANCEL, ACT_PLAIN, ACT_ASIS, ACT_WIPE_ASK, ACT_WIPE, ACT_RETRY, ACT_SKIP, ACT_DISCARD, ACT_BACK, ACT_BACKUP_NOW,
       ACT_OVERWRITE };
enum { MODE_RESTORE, MODE_BACKUP, MODE_WIPE };

static struct {
    svmu_phase phase;
    int mode;
    int ask;
    int ask_return;               /* the question to go back to from "Erase the VMU?" */
    char serial[16];
    char name[128];
    const gd_item* item;
    int device;                   /* 0..7: A1 A2 B1 B2 C1 C2 D1 D2 */
    int block;
    uint8_t* buffer;
    char message[48];
    int count;
    const char* text[4];
    int act[4];
} s;

/* ---- Paths and the files ---- */

static int
device_of_setting(void) {
    return sf_serial_vmu[0] == SERIAL_VMU_OFF ? -1 : (int)(sf_serial_vmu[0] - SERIAL_VMU_A1);
}

int
svmu_slot(void) {
    return device_of_setting();
}

static void
device_name(int device, char* out, size_t size) {
    snprintf(out, size, "%c%d", 'A' + device / 2, device % 2 + 1);
}

static int
ensure_dirs(const char* serial) {
    struct stat st;
    char path[256];
    if (stat(SVMU_SD "/OPENMENU", &st) != 0 && mkdir(SVMU_SD "/OPENMENU", 0755) != 0) {
        return -1;
    }
    if (stat(SAVES_DIR, &st) != 0 && mkdir(SAVES_DIR, 0755) != 0) {
        return -1;
    }
    if (serial) {
        snprintf(path, sizeof(path), "%s/%s", SAVES_DIR, serial);
        if (stat(path, &st) != 0 && mkdir(path, 0755) != 0) {
            return -1;
        }
    }
    return 0;
}

static void
sync_sd(void) {
#ifndef SVMU_HOST_TEST
    fs_fat_sync(SVMU_SD);
#endif
}

static void
slot_path(const char* serial, char* out, size_t size) {
    snprintf(out, size, "%s/%s/SLOT%d.VMU", SAVES_DIR, serial, SLOT);
}

/* 0 = there and the right size, 1 = not there, 2 = the wrong size */
static int
file_status(const char* serial) {
    char path[256];
    struct stat st;
    slot_path(serial, path, sizeof(path));
    if (stat(path, &st) != 0) {
        return 1;
    }
    return st.st_size == TOTAL_SIZE ? 0 : 2;
}

static int
read_image(const char* path, uint8_t* buf) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    ssize_t n = read(fd, buf, TOTAL_SIZE);
    close(fd);
    return n == TOTAL_SIZE ? 0 : -1;
}

static int
write_image(const char* serial, const uint8_t* buf) {
    char path[256];
    if (ensure_dirs(serial) != 0) {
        return -1;
    }
    slot_path(serial, path, sizeof(path));
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }
    ssize_t n = write(fd, buf, TOTAL_SIZE);
    close(fd);
    sync_sd();
    return n == TOTAL_SIZE ? 0 : -1;
}

static void
write_title(const char* serial, const char* title) {
    char path[256];
    if (!title[0] || ensure_dirs(serial) != 0) {
        return;
    }
    snprintf(path, sizeof(path), "%s/%s/TITLE.TXT", SAVES_DIR, serial);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, title, strlen(title));
        close(fd);
        sync_sd();
    }
}

/* LASTDISC.TXT: "<serial>-<slot>", as 1.7.1 writes it (a slot suffix is how its Slots mode tells which image to back up). */
static void
write_lastdisc(const char* serial) {
    char text[32];
    if (ensure_dirs(NULL) != 0) {
        return;
    }
    snprintf(text, sizeof(text), "%s-%d", serial, SLOT);
    int fd = open(LASTDISC, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, text, strlen(text));
        close(fd);
        sync_sd();
    }
}

static void
clear_lastdisc(void) {
    unlink(LASTDISC);
    sync_sd();
}

/* The serial and the slot out of LASTDISC.TXT: "HDR-0000-2" is serial "HDR-0000", slot 2; no suffix means slot 1. */
static int
parse_lastdisc(const char* text, char* serial, size_t size) {
    const char* hyphen = strrchr(text, '-');
    if (hyphen && hyphen[1] >= '1' && hyphen[1] <= '5' && hyphen[2] == '\0') {
        size_t len = (size_t)(hyphen - text);
        len = len < size - 1 ? len : size - 1;
        memcpy(serial, text, len);
        serial[len] = '\0';
        return hyphen[1] - '0';
    }
    snprintf(serial, size, "%s", text);
    return 1;
}

static int
read_lastdisc(char* out, size_t size) {
    int fd = open(LASTDISC, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    ssize_t n = read(fd, out, size - 1);
    close(fd);
    if (n <= 0) {
        return 0;
    }
    out[n] = '\0';
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ')) {
        out[--n] = '\0';
    }
    return n > 0;
}

/* ---- The VMU ---- */

#ifndef SVMU_HOST_TEST
static maple_device_t*
vmu_dev(int device) {
    maple_device_t* dev = maple_enum_dev(device / 2, device % 2 == 0 ? 1 : 2);
    return dev && dev->valid && (dev->info.functions & MAPLE_FUNC_MEMCARD) ? dev : NULL;
}
static int
vmu_read_block(int device, int block, uint8_t* out) {
    maple_device_t* dev = vmu_dev(device);
    return dev && vmu_block_read(dev, (uint16_t)block, out) == MAPLE_EOK ? 0 : -1;
}
static int
vmu_write_block(int device, int block, const uint8_t* in) {
    maple_device_t* dev = vmu_dev(device);
    return dev && vmu_block_write(dev, (uint16_t)block, in) == MAPLE_EOK ? 0 : -1;
}
static int
vmu_present(int device) {
    return vmu_dev(device) != NULL;
}
#else
int vmu_read_block(int device, int block, uint8_t* out);
int vmu_write_block(int device, int block, const uint8_t* in);
int vmu_present(int device);
#endif

/* ---- The questions ---- */

static void
set_ask(int kind, const char* message, int n, const char* t0, int a0, const char* t1, int a1, const char* t2, int a2,
        const char* t3, int a3) {
    s.phase = SVMU_ASK;
    s.ask = kind;
    snprintf(s.message, sizeof(s.message), "%s", message);
    s.count = n;
    s.text[0] = t0; s.act[0] = a0;
    s.text[1] = t1; s.act[1] = a1;
    s.text[2] = t2; s.act[2] = a2;
    s.text[3] = t3; s.act[3] = a3;
}

static void
free_buffer(void) {
    free(s.buffer);
    s.buffer = NULL;
}

static void
ask(int kind) {
    char dev[8];
    char msg[48];
    device_name(s.device, dev, sizeof(dev));
    free_buffer();
    switch (kind) {
        case ASK_NO_SD:
            set_ask(kind, "Serial SD card not found", 2, "Launch without Serial VMU", ACT_PLAIN, "Cancel", ACT_CANCEL, NULL, 0, NULL, 0);
            break;
        case ASK_NO_VMU:
            snprintf(msg, sizeof(msg), "No VMU in slot %s", dev);
            set_ask(kind, msg, 2, "Launch without Serial VMU", ACT_PLAIN, "Cancel", ACT_CANCEL, NULL, 0, NULL, 0);
            break;
        case ASK_FIRST_TIME:
            set_ask(kind, "No Serial VMU for this game yet", 4, "Start with a blank VMU", ACT_WIPE_ASK, "Launch with the VMU as is",
                    ACT_ASIS, "Launch without Serial VMU", ACT_PLAIN, "Cancel", ACT_CANCEL);
            break;
        case ASK_CORRUPT:
            set_ask(kind, "The Serial VMU file is damaged", 3, "Start with a blank VMU", ACT_WIPE_ASK, "Launch with the VMU as is",
                    ACT_ASIS, "Cancel", ACT_CANCEL, NULL, 0);
            break;
        case ASK_WIPE:
            snprintf(msg, sizeof(msg), "Erase the VMU in %s?", dev);
            set_ask(kind, msg, 2, "Yes, erase it", ACT_WIPE, "No", ACT_BACK, NULL, 0, NULL, 0);
            break;
        case ASK_RESTORE_FAILED:
            set_ask(kind, "Could not restore the Serial VMU", 4, "Try again", ACT_RETRY, "Launch with the VMU as is", ACT_ASIS,
                    "Launch without Serial VMU", ACT_PLAIN, "Cancel", ACT_CANCEL);
            break;
        case ASK_BACKUP_FAILED:
            set_ask(kind, "Could not back up the VMU", 3, "Try again", ACT_RETRY, "Skip for now", ACT_SKIP, "Discard the backup",
                    ACT_DISCARD, NULL, 0);
            break;
        case ASK_PENDING:
            set_ask(kind, "The last game's VMU is not backed up", 3, "Back it up now", ACT_BACKUP_NOW, "Overwrite it", ACT_OVERWRITE,
                    "Cancel", ACT_CANCEL, NULL, 0);
            break;
        case ASK_BACKUP_NO_VMU:
            snprintf(msg, sizeof(msg), "No VMU in slot %s to back up", dev);
            set_ask(kind, msg, 3, "Try again", ACT_RETRY, "Skip for now", ACT_SKIP, "Discard the backup", ACT_DISCARD, NULL, 0);
            break;
        default: break;
    }
}

/* ---- Flows ---- */

static void
start_restore_flow(void) {
    free_buffer();
    if (!savefile_sd_available()) {
        ask(ASK_NO_SD);
        return;
    }
    if (!vmu_present(s.device)) {
        ask(ASK_NO_VMU);
        return;
    }
    switch (file_status(s.serial)) {
        case 1: ask(ASK_FIRST_TIME); return;
        case 2: ask(ASK_CORRUPT); return;
        default: break;
    }
    char path[256];
    slot_path(s.serial, path, sizeof(path));
    s.buffer = malloc(TOTAL_SIZE);
    if (!s.buffer || read_image(path, s.buffer) != 0) {
        ask(ASK_RESTORE_FAILED);
        return;
    }
    s.mode = MODE_RESTORE;
    s.block = 0;
    s.phase = SVMU_BUSY;
    snprintf(s.message, sizeof(s.message), "Restoring the Serial VMU");
}

static void
start_backup_flow(void) {
    free_buffer();
    if (!savefile_sd_available()) {
        s.phase = SVMU_CLOSE; /* nothing can be done; the note stays for the next start */
        return;
    }
    if (!vmu_present(s.device)) {
        ask(ASK_BACKUP_NO_VMU);
        return;
    }
    s.buffer = malloc(TOTAL_SIZE);
    if (!s.buffer) {
        ask(ASK_BACKUP_FAILED);
        return;
    }
    s.mode = MODE_BACKUP;
    s.block = 0;
    s.phase = SVMU_BUSY;
    snprintf(s.message, sizeof(s.message), "Backing up the VMU");
}

static void
start_wipe(void) {
    free_buffer();
    s.buffer = malloc(TOTAL_SIZE);
    if (!s.buffer || read_image(SVMU_EMPTY_VMU, s.buffer) != 0) { /* the blank card image is on the menu disc */
        ask(ASK_RESTORE_FAILED);
        return;
    }
    s.mode = MODE_WIPE;
    s.block = 0;
    s.phase = SVMU_BUSY;
    snprintf(s.message, sizeof(s.message), "Erasing the VMU");
}

static void
go_launch(void) {
    free_buffer();
    write_lastdisc(s.serial); /* so the next start backs the VMU up */
    write_title(s.serial, s.name);
    s.phase = SVMU_LAUNCH;
}

/* ---- Public ---- */

int
svmu_applies(const gd_item* item) {
    return item && item->product[0] && strcmp(item->type, "other") != 0 && sf_serial_vmu[0] != SERIAL_VMU_OFF
           && sf_serial_vmu_multislot[0] != SERIAL_VMU_MULTISLOT_ON;
}

int
svmu_slots_unsupported(const gd_item* item) {
    return item && item->product[0] && sf_serial_vmu[0] != SERIAL_VMU_OFF && sf_serial_vmu_multislot[0] == SERIAL_VMU_MULTISLOT_ON;
}

void
svmu_begin_restore(const gd_item* item) {
    memset(&s, 0, sizeof(s));
    s.item = item;
    snprintf(s.serial, sizeof(s.serial), "%s", item->product);
    snprintf(s.name, sizeof(s.name), "%s", item->name);
    s.device = device_of_setting();
    /* A backup left undone (skipped at the last start) would be overwritten by this restore */
    char text[32], pending[16];
    if (savefile_sd_available() && read_lastdisc(text, sizeof(text)) && parse_lastdisc(text, pending, sizeof(pending)) == SLOT) {
        ask(ASK_PENDING);
        return;
    }
    start_restore_flow();
}

int
svmu_begin_backup(void) {
    char text[32];
    if (sf_serial_vmu[0] == SERIAL_VMU_OFF || !savefile_sd_available() || !read_lastdisc(text, sizeof(text))) {
        return 0;
    }
    memset(&s, 0, sizeof(s));
    int slot = parse_lastdisc(text, s.serial, sizeof(s.serial));
    if (slot != SLOT) {
        memset(&s, 0, sizeof(s)); /* 1.7.1 with Slots left it: this menu cannot make that backup, so it stays for 1.7.1 */
        return 0;
    }
    const gd_item* g = list_find_by_product(s.serial);
    snprintf(s.name, sizeof(s.name), "%s", g ? g->name : "");
    s.device = device_of_setting();
    start_backup_flow();
    return s.phase != SVMU_OFF;
}

void
svmu_step(void) {
    if (s.phase != SVMU_BUSY) {
        return;
    }
    if (s.block < BLOCKS) {
        int rc = s.mode == MODE_BACKUP ? vmu_read_block(s.device, s.block, s.buffer + s.block * BLOCK_SIZE)
                                       : vmu_write_block(s.device, s.block, s.buffer + s.block * BLOCK_SIZE);
        if (rc != 0) {
            if (s.mode == MODE_BACKUP) {
                ask(ASK_BACKUP_FAILED);
            } else if (s.mode == MODE_WIPE) {
                go_launch(); /* the wipe did not work: start anyway, as 1.7.1 */
            } else {
                ask(ASK_RESTORE_FAILED);
            }
            return;
        }
        s.block++;
        return;
    }
    if (s.mode == MODE_BACKUP) {
        if (write_image(s.serial, s.buffer) != 0) {
            ask(ASK_BACKUP_FAILED);
            return;
        }
        write_title(s.serial, s.name);
        free_buffer();
        clear_lastdisc();
        s.phase = SVMU_CLOSE;
    } else {
        go_launch();
    }
}

svmu_phase
svmu_phase_get(void) {
    return (svmu_phase)s.phase;
}

const char*
svmu_title(void) {
    return "Serial VMU";
}

const char*
svmu_message(void) {
    return s.message;
}

int
svmu_progress(void) {
    return s.block * 100 / BLOCKS;
}

int
svmu_choice_count(void) {
    return s.phase == SVMU_ASK ? s.count : 0;
}

const char*
svmu_choice_text(int index) {
    return index >= 0 && index < s.count ? s.text[index] : "";
}

void
svmu_choose(int index) {
    if (s.phase != SVMU_ASK || index < 0 || index >= s.count) {
        return;
    }
    int backup = s.ask == ASK_BACKUP_FAILED || s.ask == ASK_BACKUP_NO_VMU;
    switch (s.act[index]) {
        case ACT_CANCEL: free_buffer(); s.phase = SVMU_CLOSE; break;
        case ACT_PLAIN: free_buffer(); s.phase = SVMU_LAUNCH_PLAIN; break;
        case ACT_ASIS: go_launch(); break;
        case ACT_WIPE_ASK: s.ask_return = s.ask; ask(ASK_WIPE); break;
        case ACT_WIPE: start_wipe(); break;
        case ACT_BACK: ask(s.ask_return ? s.ask_return : ASK_FIRST_TIME); break;
        case ACT_RETRY: backup ? start_backup_flow() : start_restore_flow(); break;
        case ACT_BACKUP_NOW: { /* back the last game's VMU up first; the game is started again afterwards */
            char text[32];
            if (read_lastdisc(text, sizeof(text))) {
                parse_lastdisc(text, s.serial, sizeof(s.serial));
                const gd_item* g = list_find_by_product(s.serial);
                snprintf(s.name, sizeof(s.name), "%s", g ? g->name : "");
                start_backup_flow();
            } else {
                s.phase = SVMU_CLOSE;
            }
            break;
        }
        case ACT_OVERWRITE:
            clear_lastdisc();
            start_restore_flow();
            break;
        case ACT_SKIP: free_buffer(); s.phase = SVMU_CLOSE; break;        /* LASTDISC.TXT stays: asked again at the next start */
        case ACT_DISCARD: free_buffer(); clear_lastdisc(); s.phase = SVMU_CLOSE; break;
        default: break;
    }
}

void
svmu_cancel(void) {
    if (s.phase != SVMU_ASK) {
        return;
    }
    if (s.ask == ASK_WIPE) {
        svmu_choose(1); /* No */
    } else if (s.ask == ASK_BACKUP_FAILED || s.ask == ASK_BACKUP_NO_VMU) {
        svmu_choose(1); /* skip for now */
    } else {
        free_buffer();
        s.phase = SVMU_CLOSE;
    }
}

void
svmu_finish(void) {
    free_buffer();
    s.phase = SVMU_OFF;
}
