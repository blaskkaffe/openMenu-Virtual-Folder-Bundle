#!/usr/bin/env python3
"""Capture openMenu's serial output from a coder's cable.

    pip3 install pyserial
    python3 serial_capture.py                      # finds the USB-serial port by itself
    python3 serial_capture.py /dev/tty.usbserial-0001

Everything is shown and also written to openmenu-serial-<date>.log in the current folder. A flash ROM dump
(debug log page, X) is checked against its CRC-32 and saved as flash_<date>.bin. Both files are private: the log
and the dump can hold the console's ID and ISP logins, so keep them out of the repository (.gitignore covers them).

The Dreamcast side runs at 115200 8N1 with hardware flow control, so the port is opened with RTS/CTS on. If the
cable has no RTS line wired, CTS on the Dreamcast must be grounded instead.
Ctrl+C ends the capture.
"""
import datetime
import glob
import re
import sys
import zlib

try:
    import serial
except ImportError:
    sys.exit("pyserial is missing: pip3 install pyserial")


def find_port():
    ports = sorted(glob.glob("/dev/tty.usbserial*") + glob.glob("/dev/tty.SLAB_USBtoUART*")
                   + glob.glob("/dev/ttyUSB*") + glob.glob("/dev/tty.usbmodem*"))
    if not ports:
        sys.exit("No USB-serial port found. Give the port as the first argument.")
    return ports[0]


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    log_name = "openmenu-serial-%s.log" % stamp
    ser = serial.Serial(port, 115200, rtscts=True, timeout=0.2)
    ser.rts = True
    print("Listening on %s, writing %s (Ctrl+C to stop)" % (port, log_name))

    flash = None
    pending = b""
    line_re = re.compile(r"^OM flash ([0-9a-f]{5}) ([0-9a-f]+)$")
    with open(log_name, "w", encoding="utf-8", errors="replace") as log:
        try:
            while True:
                pending += ser.read(4096)
                while b"\n" in pending:
                    raw, pending = pending.split(b"\n", 1)
                    text = raw.decode("latin-1").rstrip("\r")
                    log.write(text + "\n")
                    log.flush()
                    if text.startswith("OM flash begin"):
                        flash = bytearray(b"\xee" * 0x20000)
                        print(text)
                        continue
                    m = line_re.match(text)
                    if m and flash is not None:
                        off = int(m.group(1), 16)
                        data = bytes.fromhex(m.group(2))
                        flash[off:off + len(data)] = data
                        if off % 0x4000 == 0:
                            print("  flash %3d%%" % (off * 100 // 0x20000))
                        continue
                    if text.startswith("OM flash end") and flash is not None:
                        want = int(text.split()[-1], 16)
                        got = zlib.crc32(bytes(flash)) & 0xFFFFFFFF
                        name = "flash_%s.bin" % datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
                        with open(name, "wb") as f:
                            f.write(flash)
                        print("Flash saved as %s (CRC %s)" % (name, "OK" if got == want else
                                                              "MISMATCH %08x != %08x, send it again" % (got, want)))
                        flash = None
                        continue
                    print(text)
        except KeyboardInterrupt:
            print("\nStopped. Log: %s" % log_name)


if __name__ == "__main__":
    main()
