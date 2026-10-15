/*
 * File: serial_port.h
 * Project: openmenu
 * The Dreamcast's rear serial port (SCIF) for PPP to DreamPi 2.0+ over a USB-serial cable ("coder's cable"):
 * 115200 8N1 with RTS/CTS, as the patched games and the browsers' serial option use it. Interrupt-driven receive into
 * a ring buffer. The KOS debug output goes to the same port, so it is switched off while the port is open and
 * handed back on close.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define SERIAL_PORT_BAUD 115200

/* Returns 0 when the port is ours, -1 when dcload-serial owns it. */
int serial_port_open(int baud);
/* Gives the port back to KOS (debug output on again). Safe to call when closed. */
void serial_port_close(void);
int serial_port_is_open(void);

/* Writes everything or gives up after timeout_ms without room in the FIFO (the peer holds CTS). Returns 0 or -1. */
int serial_port_write(const uint8_t* data, size_t len, int timeout_ms);
/* Copies what has arrived, up to max bytes. Never blocks. */
size_t serial_port_read(uint8_t* out, size_t max);
/* Drops everything received so far. */
void serial_port_discard(void);

/* Hayes helpers for the DreamPi answer: sends cmd + "\r" and waits up to timeout_ms for a line that starts with one
 * of the replies ("OK", "CONNECT", ...). Returns the index of the reply seen, -1 on timeout, -2 when *cancel became
 * set. The line itself is copied to line_out when given. */
int serial_port_command(const char* cmd, const char* const* replies, int nreplies, int timeout_ms,
                        volatile int* cancel, char* line_out, size_t line_len);
