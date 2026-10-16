/* qr: a small QR code encoder (byte mode, error correction level L, versions 1..40, mask chosen by the standard
 * penalty rules). Used by the debug report to show its text as a code that can be scanned from a photo. */
#pragma once

#include <stdint.h>

#define QR_MAX_SIZE 177

/* Encodes `len` bytes. On success returns the side length in modules and fills modules[y * size + x] (1 = dark,
 * the array needs QR_MAX_SIZE * QR_MAX_SIZE bytes). Returns 0 when the data does not fit (about 2950 bytes). */
int qr_encode(const uint8_t* data, int len, uint8_t* modules);
