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


def encode(rgb, index, twiddled=True):
    """rgb: HxWx3 uint8. Returns the bytes of the .pvr file. BG_L is twiddled (data format 1); BG_R is a plain
    rectangle (data format 9, rows in order)."""
    h, w, _ = rgb.shape
    r = (rgb[:, :, 0].astype(np.uint16) >> 3) << 11
    g = (rgb[:, :, 1].astype(np.uint16) >> 2) << 5
    b = rgb[:, :, 2].astype(np.uint16) >> 3
    px = r | g | b
    if not twiddled:
        data = px.astype("<u2").tobytes()
        return (b"GBIX" + struct.pack("<II", 8, index) + b"\0\0\0\0" +
                b"PVRT" + struct.pack("<I", len(data) + 8) + bytes([1, 9, 0, 0]) + struct.pack("<HH", w, h) + data)
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
            b"PVRT" + struct.pack("<I", len(data) + 8) + bytes([1, 1, 0, 0]) + struct.pack("<HH", w, h) + data)


def decode(blob):
    """(rgb HxWx3 uint8 with the 565 values widened, global index)."""
    assert blob[:4] == b"GBIX" and blob[16:20] == b"PVRT", "not a GBIX/PVRT file"
    index = struct.unpack("<I", blob[8:12])[0]
    pixfmt, datfmt = blob[24], blob[25]
    w, h = struct.unpack("<HH", blob[28:32])
    assert pixfmt == 1 and datfmt in (1, 9), "only RGB565, twiddled or rectangle, is handled"
    raw = np.frombuffer(blob[32:32 + w * h * 2], dtype="<u2")
    if datfmt == 9:
        px = raw.reshape(h, w)
        r = ((px >> 11) & 31) << 3
        g = ((px >> 5) & 63) << 2
        b = (px & 31) << 3
        return np.stack([r, g, b], axis=2).astype(np.uint8), index
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
    r = ((px >> 11) & 31) << 3
    g = ((px >> 5) & 63) << 2
    b = (px & 31) << 3
    return np.stack([r, g, b], axis=2).astype(np.uint8), index
