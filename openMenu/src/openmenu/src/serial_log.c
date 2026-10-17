#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arch/timer.h>
#include <dc/flashrom.h>
#include <kos/dbgio.h>
#include <kos/thread.h>

#include "backend/serial_port.h"
#include "serial_log.h"

#ifndef OPENMENU_BUILD_VERSION
#define OPENMENU_BUILD_VERSION "dev"
#endif
#ifndef OPENMENU_GIT_COMMIT
#define OPENMENU_GIT_COMMIT "unknown"
#endif

#define FLASH_SIZE 0x20000
#define FLASH_LINE 64

static void
put(const char* s) {
    if (!serial_port_is_open()) { /* the port is DreamPi's while online over the serial cable */
        dbgio_write_str(s);
    }
}

void
serial_log_banner(void) {
    char line[160];
    snprintf(line, sizeof(line), "\nOM boot openMenu %s commit %s built %s %s\n", OPENMENU_BUILD_VERSION,
             OPENMENU_GIT_COMMIT, __DATE__, __TIME__);
    put(line);
}

void
serial_log_block(const char* tag, const char* text) {
    char line[200];
    while (*text) {
        const char* nl = strchr(text, '\n');
        size_t n = nl ? (size_t)(nl - text) : strlen(text);
        if (n > sizeof(line) - 16) {
            n = sizeof(line) - 16;
        }
        snprintf(line, sizeof(line), "OM %s %.*s\n", tag, (int)n, text);
        put(line);
        text += n;
        if (*text == '\n') {
            text++;
        }
    }
}

static uint32_t
crc32_update(uint32_t crc, const uint8_t* p, size_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static kthread_t* flash_thd = NULL;
static volatile int flash_progress = -1;

static void*
flash_main(void* param) {
    static const char hex[] = "0123456789abcdef";
    uint8_t buf[FLASH_LINE];
    char line[32 + FLASH_LINE * 2];
    uint32_t crc = 0;
    (void)param;

    put("OM flash begin size 131072 (private: holds the console's ID and ISP logins)\n");
    for (int off = 0; off < FLASH_SIZE; off += FLASH_LINE) {
        if (flashrom_read(off, buf, FLASH_LINE) < 0) {
            memset(buf, 0xEE, sizeof(buf));
            put("OM flash read-error\n");
        }
        crc = crc32_update(crc, buf, FLASH_LINE);
        int n = snprintf(line, sizeof(line), "OM flash %05x ", off);
        for (int i = 0; i < FLASH_LINE; i++) {
            line[n++] = hex[buf[i] >> 4];
            line[n++] = hex[buf[i] & 15];
        }
        line[n++] = '\n';
        line[n] = 0;
        put(line);
        flash_progress = (int)((off + FLASH_LINE) * 100LL / FLASH_SIZE);
        thd_pass(); /* the menu keeps drawing */
    }
    snprintf(line, sizeof(line), "OM flash end crc32 %08lx\n", (unsigned long)crc);
    put(line);
    flash_progress = 101;
    return NULL;
}

int
serial_log_flash_start(void) {
    if (flash_thd != NULL) {
        if (flash_progress <= 100) {
            return -1; /* still sending */
        }
        thd_join(flash_thd, NULL);
        flash_thd = NULL;
    }
    if (serial_port_is_open()) {
        return -1;
    }
    flash_progress = 0;
    flash_thd = thd_create(0, flash_main, NULL);
    if (flash_thd == NULL) {
        flash_progress = -1;
        return -1;
    }
    return 0;
}

int
serial_log_flash_progress(void) {
    return flash_progress;
}
