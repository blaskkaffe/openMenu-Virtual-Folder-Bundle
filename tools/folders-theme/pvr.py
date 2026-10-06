"""RGB565 twiddled PVR (Dreamcast texture) reader and writer, for openMenu's BG_L.PVR / BG_R.PVR.

Layout (checked against the default theme's files): a GBIX chunk (global index), then a PVRT chunk with pixel format 1 (RGB565),
data format 1 (twiddled), the width and height, then width*height 16-bit pixels. A twiddled texture interleaves the bits of x and y
(Morton order); BG_R uses data format 9, a plain rectangle."""
import struct
import numpy as np


def _spread(v, bits):
    out = 0
    for i in range(bits):
        out |= ((v >> i) & 1) << (2 * i)
    return out


def _twiddle_index(size):
    """index[y][x] = position of pixel (x, y) in a size*size twiddled square (y in the even bits, x in the odd ones)."""
    bits = size.bit_length() - 1
    sp = np.array([_spread(v, bits) for v in range(size)], dtype=np.int64)
    return (sp[None, :] << 1) | sp[:, None]


def _pack_4444(rgba):
    """ARGB4444: 4 bits each, rounded (v*15/255), alpha in the top nibble."""
    q = lambda c: (c.astype(np.uint16) * 15 + 127) // 255
    return (q(rgba[:, :, 3]) << 12) | (q(rgba[:, :, 0]) << 8) | (q(rgba[:, :, 1]) << 4) | q(rgba[:, :, 2])


def encode(rgb, index, twiddled=True, argb4444=False):
    """rgb: HxWx3 uint8 (HxWx4 with argb4444=True). Returns the bytes of the .pvr file. BG_L is twiddled (data format 1); BG_R is
    a plain rectangle (data format 9, rows in order). argb4444 writes pixel format 2 (translucent) instead of RGB565."""
    h, w = rgb.shape[:2]
    pixfmt = 2 if argb4444 else 1
    if argb4444:
        px = _pack_4444(rgb)
    else:
        r = (rgb[:, :, 0].astype(np.uint16) >> 3) << 11
        g = (rgb[:, :, 1].astype(np.uint16) >> 2) << 5
        b = rgb[:, :, 2].astype(np.uint16) >> 3
        px = r | g | b
    if not twiddled:
        data = px.astype("<u2").tobytes()
        return (b"GBIX" + struct.pack("<II", 8, index) + b"\0\0\0\0" +
                b"PVRT" + struct.pack("<I", len(data) + 8) + bytes([pixfmt, 9, 0, 0]) + struct.pack("<HH", w, h) + data)
    side = min(w, h)
    tw = _twiddle_index(side)
    out = np.zeros(w * h, dtype="<u2")
    if w == h:
        out[tw.reshape(-1)] = px.reshape(-1)
    else:
        blocks = (w * h) // (side * side)
        for k in range(blocks):
            if h > w:
                tile = px[k * side:(k + 1) * side, :]
            else:
                tile = px[:, k * side:(k + 1) * side]
            out[k * side * side + tw.reshape(-1)] = tile.reshape(-1)
    data = out.tobytes()
    return (b"GBIX" + struct.pack("<II", 8, index) + b"\0\0\0\0" +
            b"PVRT" + struct.pack("<I", len(data) + 8) + bytes([pixfmt, 1, 0, 0]) + struct.pack("<HH", w, h) + data)


def _unpack(px, pixfmt):
    if pixfmt == 2:
        a, r, g, b = ((px >> 12) & 15) * 17, ((px >> 8) & 15) * 17, ((px >> 4) & 15) * 17, (px & 15) * 17
        return np.stack([r, g, b, a], axis=2).astype(np.uint8)
    r = ((px >> 11) & 31) << 3
    g = ((px >> 5) & 63) << 2
    b = (px & 31) << 3
    return np.stack([r, g, b], axis=2).astype(np.uint8)


def decode(blob):
    """(HxWx3 RGB with the 565 values widened, or HxWx4 RGBA for ARGB4444; the global index)."""
    assert blob[:4] == b"GBIX" and blob[16:20] == b"PVRT", "not a GBIX/PVRT file"
    index = struct.unpack("<I", blob[8:12])[0]
    pixfmt, datfmt = blob[24], blob[25]
    w, h = struct.unpack("<HH", blob[28:32])
    assert pixfmt in (1, 2) and datfmt in (1, 9), "only RGB565 / ARGB4444, twiddled or rectangle, is handled"
    raw = np.frombuffer(blob[32:32 + w * h * 2], dtype="<u2")
    if datfmt == 9:
        return _unpack(raw.reshape(h, w), pixfmt), index
    side = min(w, h)
    tw = _twiddle_index(side)
    px = np.zeros((h, w), dtype=np.uint16)
    if w == h:
        px[:, :] = raw[tw]
    else:
        for k in range((w * h) // (side * side)):
            tile = raw[k * side * side:(k + 1) * side * side][tw]
            if h > w:
                px[k * side:(k + 1) * side, :] = tile
            else:
                px[:, k * side:(k + 1) * side] = tile
    return _unpack(px, pixfmt), index
