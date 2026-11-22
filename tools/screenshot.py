# SPDX-License-Identifier: GPL-3.0-only
# Copyright The AstroTurboMaxiSolver authors
"""Takes a screenshot of the puck over its USB cable (main/screenshot.c) and saves a PNG.

Run with ESP-IDF's Python, which has pyserial:  python tools/screenshot.py COM3 shot.png
With --tap after them, it first acts as a tap on the map, so the bottom third shows;
with --menu, it first opens the menu; with --brightness, its Brightness page.
The puck must not be held by another serial monitor at the same time.
"""

import struct
import sys
import time
import zlib

import serial


def png(path, width, height, rows):
    """Writes RGB rows (bytes, 3 per pixel) as a PNG, with zlib only."""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    raw = b''.join(b'\x00' + row for row in rows)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 6)))
        f.write(chunk(b'IEND', b''))


def rgb565_to_rgb(line, width):
    data = bytes.fromhex(line)
    out = bytearray(width * 3)
    for i in range(width):
        v = data[2 * i] | data[2 * i + 1] << 8  # little-endian, as the ESP32-S3 keeps it
        r, g, b = v >> 11, (v >> 5) & 0x3F, v & 0x1F
        out[3 * i:3 * i + 3] = bytes((r * 255 // 31, g * 255 // 63, b * 255 // 31))
    return bytes(out)


def main():
    port, path = sys.argv[1], sys.argv[2]
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 10
    s.dtr = s.rts = False  # set before opening, or opening the port resets the board
    with s:  # opens the port
        time.sleep(0.3)
        if '--tap' in sys.argv[3:]:
            s.write(b'T')
            time.sleep(0.5)
        if '--menu' in sys.argv[3:]:
            s.write(b'M')
            time.sleep(0.5)
        if '--brightness' in sys.argv[3:]:
            s.write(b'B')
            time.sleep(0.5)
        s.reset_input_buffer()
        s.write(b'S')
        width = height = 0
        rows = {}
        deadline = time.time() + 60
        while time.time() < deadline:
            line = s.readline().decode('ascii', 'replace').strip()
            if line.startswith('SHOT BEGIN'):
                width, height = (int(n) for n in line.split()[2:4])
            elif line.startswith('R') and width and ' ' in line:
                index, hex_row = line[1:].split(' ', 1)
                if index.isdigit() and len(hex_row) == width * 4:
                    rows[int(index)] = rgb565_to_rgb(hex_row, width)
            elif line == 'SHOT END':
                break
    if not width or len(rows) < height // 2:
        sys.exit(f'No screenshot: {len(rows)} of {height} rows came')
    # A row lost on the way (the log shares the port) is filled with the one above.
    missing = height - len(rows)
    blank = bytes(width * 3)
    out = []
    for y in range(height):
        out.append(rows.get(y, out[-1] if out else blank))
    png(path, width, height, out)
    print(f'Saved {path}' + (f' ({missing} rows filled in)' if missing else ''))


if __name__ == '__main__':
    main()
