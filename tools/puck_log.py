# SPDX-License-Identifier: GPL-3.0-only
# Copyright The AstroTurboMaxiSolver authors
#
# Fetches the puck's last log lines, kept in its memory (main/log_history.h), over the USB
# cable, without resetting it:
#
#   python tools/puck_log.py COM3 [out.txt]
#
# Needs pyserial (in ESP-IDF's Python).

import sys
import time

import serial


def main():
    if len(sys.argv) < 2:
        sys.exit('Usage: puck_log.py PORT [OUT.txt]')
    port = serial.Serial()
    port.port = sys.argv[1]
    port.baudrate = 115200
    port.timeout = 1
    # Low before opening: otherwise opening the port resets the board.
    port.dtr = False
    port.rts = False
    port.open()
    time.sleep(0.3)
    port.reset_input_buffer()
    port.write(b'L')
    port.flush()
    lines = []
    inside = False
    end = time.time() + 30
    while time.time() < end:
        line = port.readline().decode('utf-8', 'replace').rstrip()
        if line == 'LOG BEGIN':
            inside = True
        elif line == 'LOG END':
            break
        elif inside and line:
            lines.append(line)
    port.close()
    text = '\n'.join(lines) + '\n'
    if len(sys.argv) > 2:
        with open(sys.argv[2], 'w', encoding='utf-8') as out:
            out.write(text)
        print('Saved %d lines in %s' % (len(lines), sys.argv[2]))
    else:
        sys.stdout.write(text)


if __name__ == '__main__':
    main()
