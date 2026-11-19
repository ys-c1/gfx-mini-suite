"""Minimal dependency-free PNG reader/writer for 8-bit RGB images."""

import binascii
import struct
import zlib

_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _chunk(kind, payload):
    crc = binascii.crc32(kind + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc)


def write_png(path, width, height, rgb, palette=None, indices=None):
    """Write a PNG.

    With ``rgb`` (a bytes-like of width*height*3) this writes an 8-bit RGB
    truecolour image.  With ``palette`` and ``indices`` it writes an 8-bit
    palette image (colour type 3), which is the natural container for a
    dithered result and is also what makes the size story honest: the index
    plane is what gets compressed, not re-expanded RGB.
    """
    if palette is not None or indices is not None:
        if palette is None or indices is None:
            raise ValueError("palette and indices must be given together")
        if len(palette) > 256:
            raise ValueError("PNG palette may hold at most 256 entries")
        if len(indices) != width * height:
            raise ValueError("indices length does not match dimensions")
        plte = bytearray()
        for color in palette:
            plte.extend(bytes(color))
        rows = bytearray()
        for y in range(height):
            rows.append(0)
            rows.extend(bytes(indices[y * width:(y + 1) * width]))
        with open(path, "wb") as fh:
            fh.write(_SIGNATURE)
            fh.write(_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)))
            fh.write(_chunk(b"PLTE", bytes(plte)))
            fh.write(_chunk(b"IDAT", zlib.compress(bytes(rows), 9)))
            fh.write(_chunk(b"IEND", b""))
        return

    if len(rgb) != width * height * 3:
        raise ValueError("rgb length does not match dimensions")
    stride = width * 3
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter: None
        raw.extend(rgb[y * stride:(y + 1) * stride])
    with open(path, "wb") as fh:
        fh.write(_SIGNATURE)
        fh.write(_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        fh.write(_chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        fh.write(_chunk(b"IEND", b""))


def _paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png(path):
    """Read an 8-bit RGB or 8-bit palette PNG written by :func:`write_png`.

    Returns ``(width, height, bytearray)`` RGB bytes; palette images are
    expanded through their PLTE.  No interlacing.
    """
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:8] != _SIGNATURE:
        raise ValueError("not a PNG file")
    pos = 8
    width = height = None
    color_type = None
    plte = None
    idat = bytearray()
    while pos + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        payload = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, color_type, comp, filt, interlace = \
                struct.unpack(">IIBBBBB", payload)
            if depth != 8 or color_type not in (2, 3) or interlace != 0:
                raise ValueError("unsupported PNG format")
        elif kind == b"PLTE":
            plte = payload
        elif kind == b"IDAT":
            idat.extend(payload)
        elif kind == b"IEND":
            break
    if width is None:
        raise ValueError("missing IHDR")
    channels = 3 if color_type == 2 else 1
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(width * height * 3)
    prev = bytearray(stride)
    pos = 0
    for y in range(height):
        ftype = raw[pos]
        pos += 1
        row = bytearray(raw[pos:pos + stride])
        pos += stride
        if ftype == 0:
            pass
        elif ftype == 1:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                row[i] = (row[i] + left) & 0xFF
        elif ftype == 2:
            for i in range(stride):
                row[i] = (row[i] + prev[i]) & 0xFF
        elif ftype == 3:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                row[i] = (row[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                upleft = prev[i - channels] if i >= channels else 0
                row[i] = (row[i] + _paeth(left, prev[i], upleft)) & 0xFF
        else:
            raise ValueError("unsupported PNG filter %d" % ftype)
        if channels == 3:
            out[y * stride:(y + 1) * stride] = row
        else:
            if plte is None:
                raise ValueError("palette image without PLTE")
            base = y * width * 3
            for x in range(width):
                e = row[x] * 3
                out[base + x * 3] = plte[e]
                out[base + x * 3 + 1] = plte[e + 1]
                out[base + x * 3 + 2] = plte[e + 2]
        prev = row
    return width, height, out
