/* serial_log: debugging over a coder's cable on the rear serial port. KOS sends its own output there already
 * (115200 8N1, hardware flow control: open the PC side with RTS/CTS on, or with RTS held high), including the
 * register dump and stack trace of a crash. This adds openMenu's own lines, each starting with "OM ":
 *   OM boot ...          version, commit and start-up times
 *   OM log ...           the debug log page (the same text as its QR code), sent when the page opens
 *   OM flash ...         the whole flash ROM as hex with a CRC-32, on request from the debug log page
 * Nothing is sent while the online features use the port for DreamPi. With the cable in a DreamPi 2.0+ instead
 * of a computer, DreamPi logs every line as "serial: b'...'"; tools/serial_capture.py <log> pulls them back out. */
#pragma once

/* "OM boot" line with the version and commit. */
void serial_log_banner(void);

/* Sends a block of text as "OM <tag> " lines, one per text line. */
void serial_log_block(const char* tag, const char* text);

/* Starts sending the flash ROM on a worker thread. Returns 0 when started, -1 when busy or the port is taken. */
int serial_log_flash_start(void);
/* -1 idle, 0..100 percent while sending, 101 done (stays until the next start). */
int serial_log_flash_progress(void);
