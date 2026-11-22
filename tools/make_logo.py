# SPDX-License-Identifier: GPL-3.0-only
# Copyright The AstroTurboMaxiSolver authors
#
# Makes the puck's start-up logo (main/images/logo.c) from the app's icon, as an LVGL image
# with transparent corners. Plain Python, no image library: reads the PNG with zlib and
# shrinks it by averaging the source pixels each output pixel covers.
#
#   python tools/make_logo.py ../AstroFinder/public/icons/icon-512x512.png 160 main/images/logo.c

import struct
import sys
import zlib


def read_png(path):
    """The PNG's width, height and RGBA rows (8 bits, not interlaced: as the app's icons)."""
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', 'not a PNG'
    pos, idat = 8, b''
    while pos < len(data):
        length, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b'IHDR':
            width, height, depth, colour, _, _, interlace = struct.unpack('>IIBBBBB', body)
            assert depth == 8 and interlace == 0, 'only 8-bit, non-interlaced PNGs'
            channels = {6: 4, 2: 3}[colour]  # RGBA or RGB
        elif kind == b'IDAT':
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * channels
    rows, previous = [], bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind, line = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up = previous[i]
            up_left = previous[i - channels] if i >= channels else 0
            if kind == 1:
                line[i] = (line[i] + left) & 255
            elif kind == 2:
                line[i] = (line[i] + up) & 255
            elif kind == 3:
                line[i] = (line[i] + (left + up) // 2) & 255
            elif kind == 4:  # Paeth
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                line[i] = (line[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else up_left)) & 255
        previous = line
        if channels == 3:
            line = bytearray(b''.join(bytes(line[i:i + 3]) + b'\xff' for i in range(0, stride, 3)))
        rows.append(line)
    return width, height, rows


def shrink(width, height, rows, size):
    """Each output pixel the average of the source area it covers, colours weighted by alpha."""
    out = []
    scale = width / size
    for oy in range(size):
        y0, y1 = oy * scale, (oy + 1) * scale
        for ox in range(size):
            x0, x1 = ox * scale, (ox + 1) * scale
            r = g = b = a = area = 0.0
            for sy in range(int(y0), min(height, int(y1 + 0.999))):
                wy = min(y1, sy + 1) - max(y0, sy)
                for sx in range(int(x0), min(width, int(x1 + 0.999))):
                    w = wy * (min(x1, sx + 1) - max(x0, sx))
                    pr, pg, pb, pa = rows[sy][sx * 4:sx * 4 + 4]
                    r += pr * pa * w
                    g += pg * pa * w
                    b += pb * pa * w
                    a += pa * w
                    area += w
            alpha = a / area
            out.append((round(b / a) if a else 0, round(g / a) if a else 0, round(r / a) if a else 0, round(alpha)))
    return out


def write_c(pixels, size, path):
    """An LVGL 9 image in ARGB8888: bytes blue, green, red, alpha."""
    data = bytes(v for p in pixels for v in p)
    lines = ',\n'.join('    ' + ', '.join(f'0x{v:02x}' for v in data[i:i + 16]) for i in range(0, len(data), 16))
    open(path, 'w', newline='\n').write(f'''// SPDX-License-Identifier: GPL-3.0-only
// Copyright The AstroTurboMaxiSolver authors
//
// The app's logo, {size} x {size}, for the puck's start-up screen. Made by tools/make_logo.py
// from the app's public/icons/icon-512x512.png: don't edit by hand.

#include "lvgl.h"

static const uint8_t logo_data[] = {{
{lines}
}};

const lv_image_dsc_t logo_image = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_ARGB8888,
        .w = {size},
        .h = {size},
        .stride = {size * 4},
    }},
    .data_size = sizeof(logo_data),
    .data = logo_data,
}};
''')


if __name__ == '__main__':
    source, size, target = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    write_c(shrink(*read_png(source), size), size, target)
    print(f'{target}: {size} x {size}')
