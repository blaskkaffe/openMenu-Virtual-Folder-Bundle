#!/usr/bin/env python3
"""openMenu's serial output from a coder's cable, live or from a DreamPi log.

Live, with the cable in a computer:
    pip3 install pyserial
    python3 serial_capture.py                      # finds the USB-serial port by itself
    python3 serial_capture.py /dev/tty.usbserial-0001

From DreamPi 2.0+, with the cable in the Pi: DreamPi watches the port for modem commands and logs every line the
console sends as  serial: b'...'  so the same output ends up in its log. Copy the log off the Pi, for example
    journalctl -u dreampi --since "30 min ago" > dreampi.log
(or /var/log/syslog / /var/log/messages on older images) and run
    python3 serial_capture.py dreampi.log
which writes openmenu-serial-<date>.log with only the console's lines and rebuilds a flash dump if there is one.

A flash ROM dump (debug log page, X) is checked against its CRC-32 and saved as flash_<date>.bin. Lines DreamPi's
logger dropped are listed, so the dump can be sent again. The log and the dump can hold the console's ID and ISP
logins: keep them out of the repository (.gitignore covers them) and out of public places.

The Dreamcast side runs at 115200 8N1 with hardware flow control, so a live port is opened with RTS/CTS on. If the
cable has no RTS line wired, CTS on the Dreamcast must be grounded instead.
"""
import ast
import datetime
import glob
import os
import re
import sys
import zlib

FLASH_SIZE = 0x20000
LINE_BYTES = 64
FLASH_LINE = re.compile(r"^OM flash ([0-9a-f]{5}) ([0-9a-f]+)$")
# DreamPi: "... serial: b'text'" or "... serial unterminated: b'text'"
DREAMPI_LINE = re.compile(r"serial(?: unterminated)?: (b'.*'|b\".*\")\s*$")


def stamp():
    return datetime.datetime.now().strftime("%Y%m%d-%H%M%S")


class Collector:
    def __init__(self, out, echo):
        self.out = out
        self.echo = echo
        self.flash = None
        self.seen = set()

    def line(self, text):
        self.out.write(text + "\n")
        if text.startswith("OM flash begin"):
            self.flash = bytearray(b"\xee" * FLASH_SIZE)
            self.seen = set()
            self.say(text)
            return
        m = FLASH_LINE.match(text)
        if m and self.flash is not None:
            off = int(m.group(1), 16)
            data = bytes.fromhex(m.group(2))
            self.flash[off:off + len(data)] = data
            self.seen.add(off)
            return
        if text.startswith("OM flash end") and self.flash is not None:
            self.finish(int(text.split()[-1], 16))
            return
        self.say(text)

    def say(self, text):
        if self.echo:
            print(text)

    def finish(self, want):
        missing = [o for o in range(0, FLASH_SIZE, LINE_BYTES) if o not in self.seen]
        got = zlib.crc32(bytes(self.flash)) & 0xFFFFFFFF
        name = "flash_%s.bin" % stamp()
        with open(name, "wb") as f:
            f.write(self.flash)
        if got == want:
            print("Flash saved as %s (CRC OK)" % name)
        else:
            print("Flash saved as %s but the CRC does not match (%08x, expected %08x)" % (name, got, want))
            if missing:
                print("  %d of %d lines missing, first at %05x: send the dump again" %
                      (len(missing), FLASH_SIZE // LINE_BYTES, missing[0]))
        self.flash = None


def from_dreampi_log(path):
    name = "openmenu-serial-%s.log" % stamp()
    count = 0
    with open(path, encoding="utf-8", errors="replace") as src, open(name, "w", encoding="utf-8") as out:
        col = Collector(out, echo=False)
        for raw in src:
            m = DREAMPI_LINE.search(raw.rstrip("\n"))
            if not m:
                continue
            try:
                data = ast.literal_eval(m.group(1))
            except (ValueError, SyntaxError):
                continue
            col.line(data.decode("latin-1").rstrip("\r"))
            count += 1
        if col.flash is not None:
            print("The flash dump has no end line (the log stops before it finished)")
    print("%d console lines written to %s" % (count, name))


def find_port():
    ports = sorted(glob.glob("/dev/tty.usbserial*") + glob.glob("/dev/tty.SLAB_USBtoUART*")
                   + glob.glob("/dev/ttyUSB*") + glob.glob("/dev/tty.usbmodem*"))
    if not ports:
        sys.exit("No USB-serial port found. Give the port as the first argument.")
    return ports[0]


def live(port):
    try:
        import serial
    except ImportError:
        sys.exit("pyserial is missing: pip3 install pyserial")
    name = "openmenu-serial-%s.log" % stamp()
    ser = serial.Serial(port, 115200, rtscts=True, timeout=0.2)
    ser.rts = True
    print("Listening on %s, writing %s (Ctrl+C to stop)" % (port, name))
    pending = b""
    with open(name, "w", encoding="utf-8") as out:
        col = Collector(out, echo=True)
        try:
            while True:
                pending += ser.read(4096)
                while b"\n" in pending:
                    raw, pending = pending.split(b"\n", 1)
                    col.line(raw.decode("latin-1").rstrip("\r"))
                    out.flush()
        except KeyboardInterrupt:
            print("\nStopped. Log: %s" % name)


def main():
    arg = sys.argv[1] if len(sys.argv) > 1 else None
    if arg and os.path.isfile(arg) and not arg.startswith("/dev/"):
        from_dreampi_log(arg)
    else:
        live(arg or find_port())


if __name__ == "__main__":
    main()
