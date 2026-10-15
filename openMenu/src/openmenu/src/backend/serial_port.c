/*
 * File: serial_port.c
 * Project: openmenu
 * The rear serial port (SCIF) for PPP to DreamPi. Registers as in KOS's own SCIF driver (kernel/arch/dreamcast/
 * hardware/scif.c), which this replaces while open: KOS's driver latches itself off for good after one write timeout
 * (a cable plugged in after boot would never work), and its debug output would land in the PPP stream.
 */

#include <string.h>

#include <arch/irq.h>
#include <arch/timer.h>
#include <dc/fs_dcload.h>
#include <dc/scif.h>
#include <kos/dbgio.h>
#include <kos/thread.h>

#include "backend/serial_port.h"

#define SCIFREG08(x) (*((volatile uint8_t*)(x)))
#define SCIFREG16(x) (*((volatile uint16_t*)(x)))
#define SCSMR2       SCIFREG16(0xffe80000)
#define SCBRR2       SCIFREG08(0xffe80004)
#define SCSCR2       SCIFREG16(0xffe80008)
#define SCFTDR2      SCIFREG08(0xffe8000C)
#define SCFSR2       SCIFREG16(0xffe80010)
#define SCFRDR2      SCIFREG08(0xffe80014)
#define SCFCR2       SCIFREG16(0xffe80018)
#define SCFDR2       SCIFREG16(0xffe8001C)
#define SCSPTR2      SCIFREG16(0xffe80020)
#define SCLSR2       SCIFREG16(0xffe80024)
#define IPRC         SCIFREG16(0xffd0000c)

#define RING_SIZE 8192

static uint8_t ring[RING_SIZE];
static volatile int ring_head = 0, ring_tail = 0, ring_count = 0, ring_paused = 0;
static int port_open = 0;

static void
ring_push(uint8_t c) {
    if (ring_count >= RING_SIZE) {
        return; /* full: the byte is lost, PPP's checksum drops the frame */
    }
    ring[ring_head] = c;
    ring_head = (ring_head + 1) % RING_SIZE;
    ring_count++;
    if (!ring_paused && RING_SIZE - ring_count < 64) {
        ring_paused = 1;
        SCSPTR2 = 0x20; /* hold the peer, as KOS does */
    }
}

static void
drain_fifo(void) {
    while (SCFDR2 & 0x1f) {
        ring_push(SCFRDR2);
    }
    SCFSR2 &= ~0x03; /* RDF, DR */
}

static void
rx_irq(irq_t src, irq_context_t* cxt, void* data) {
    (void)src;
    (void)cxt;
    (void)data;
    drain_fifo();
}

static void
err_irq(irq_t src, irq_context_t* cxt, void* data) {
    (void)src;
    (void)cxt;
    (void)data;
    if (SCFSR2 & 0x9c) { /* ER, BRK, FER, PER: keep what is in the FIFO, clear the flags */
        drain_fifo();
        SCFSR2 &= ~0x9c;
    }
    if (SCLSR2 & 0x01) { /* overrun */
        SCLSR2 = 0;
    }
}

int
serial_port_open(int baud) {
    int i;
    int irq_state;

    if (port_open) {
        return 0;
    }
    if (*DCLOADMAGICADDR == DCLOADMAGICVALUE && dcload_type == DCLOAD_TYPE_SER) {
        return -1; /* a dcload-serial session lives on this port */
    }
    dbgio_disable();

    irq_state = irq_disable();
    SCSCR2 = 0;    /* everything off, internal clock */
    SCFCR2 = 0x06; /* FIFOs in reset */
    SCSMR2 = 0;    /* 8N1, P0 clock */
    SCBRR2 = (uint8_t)(50000000 / (32 * baud) - 1);
    irq_restore(irq_state);
    for (i = 0; i < 800000; i++) {
        __asm__ volatile("nop");
    }

    ring_head = ring_tail = ring_count = ring_paused = 0;
    irq_state = irq_disable();
    SCFCR2 = 0x48; /* out of reset, RTS/CTS (MCE), receive trigger at 4 */
    SCSPTR2 = 0;
    (void)SCFSR2;
    SCFSR2 = 0x60;
    (void)SCLSR2;
    SCLSR2 = 0;
    irq_set_handler(EXC_SCIF_ERI, err_irq, NULL);
    irq_set_handler(EXC_SCIF_BRI, err_irq, NULL);
    irq_set_handler(EXC_SCIF_RXI, rx_irq, NULL);
    IPRC |= 0x000e << 4;
    SCSCR2 = 0x30 | 0x48; /* TE, RE, receive and receive-error interrupts */
    irq_restore(irq_state);
    for (i = 0; i < 800000; i++) {
        __asm__ volatile("nop");
    }
    port_open = 1;
    serial_port_discard();
    return 0;
}

void
serial_port_close(void) {
    int irq_state;

    if (!port_open) {
        return;
    }
    irq_state = irq_disable();
    SCSCR2 &= ~0x48;
    IPRC &= ~(0x000e << 4);
    irq_set_handler(EXC_SCIF_ERI, NULL, NULL);
    irq_set_handler(EXC_SCIF_BRI, NULL, NULL);
    irq_set_handler(EXC_SCIF_RXI, NULL, NULL);
    SCSPTR2 = 0;
    irq_restore(irq_state);
    port_open = 0;
    /* KOS's driver programs the port again with its own speed (it keeps no other state that this touched) */
    scif_init();
    dbgio_enable();
}

int
serial_port_is_open(void) {
    return port_open;
}

int
serial_port_write(const uint8_t* data, size_t len, int timeout_ms) {
    size_t i;

    if (!port_open) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        if (!(SCFSR2 & 0x20)) {
            uint64_t start = timer_ms_gettime64();
            while (!(SCFSR2 & 0x20)) {
                if ((int)(timer_ms_gettime64() - start) >= timeout_ms) {
                    return -1;
                }
                thd_pass();
            }
        }
        SCFTDR2 = data[i];
        SCFSR2 &= 0xff9f; /* TDFE, TEND */
    }
    return 0;
}

size_t
serial_port_read(uint8_t* out, size_t max) {
    size_t n = 0;
    int irq_state = irq_disable();

    while (n < max && ring_count > 0) {
        out[n++] = ring[ring_tail];
        ring_tail = (ring_tail + 1) % RING_SIZE;
        ring_count--;
    }
    if (ring_paused && RING_SIZE - ring_count >= 256) {
        ring_paused = 0;
        SCSPTR2 = 0;
    }
    irq_restore(irq_state);
    return n;
}

void
serial_port_discard(void) {
    int irq_state = irq_disable();
    ring_head = ring_tail = ring_count = 0;
    if (ring_paused) {
        ring_paused = 0;
        SCSPTR2 = 0;
    }
    irq_restore(irq_state);
}

int
serial_port_command(const char* cmd, const char* const* replies, int nreplies, int timeout_ms, volatile int* cancel,
                    char* line_out, size_t line_len) {
    char line[64];
    size_t used = 0;
    uint64_t start;

    serial_port_discard();
    if (serial_port_write((const uint8_t*)cmd, strlen(cmd), 500) < 0
        || serial_port_write((const uint8_t*)"\r", 1, 500) < 0) {
        return -1;
    }
    start = timer_ms_gettime64();
    while ((int)(timer_ms_gettime64() - start) < timeout_ms) {
        uint8_t c;
        if (cancel != NULL && *cancel) {
            return -2;
        }
        if (serial_port_read(&c, 1) == 0) {
            thd_sleep(10);
            continue;
        }
        if (c != '\r' && c != '\n') {
            if (used < sizeof(line) - 1) {
                line[used++] = (char)c;
            }
            continue;
        }
        line[used] = '\0';
        for (int i = 0; i < nreplies; i++) {
            if (used > 0 && strncmp(line, replies[i], strlen(replies[i])) == 0) {
                if (line_out != NULL && line_len > 0) {
                    strncpy(line_out, line, line_len - 1);
                    line_out[line_len - 1] = '\0';
                }
                return i;
            }
        }
        used = 0; /* an echo or anything else: keep listening */
    }
    return -1;
}
