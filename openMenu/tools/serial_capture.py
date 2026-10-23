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

Performance test (Settings > Other > Performance test): capture as above while it runs, then
    python3 serial_capture.py --bench openmenu-serial-<date>.log      (or the DreamPi log itself)
which writes bench-report-<date>.md: a table per step and run (fps, dropped frames, worst and 95th percentile frame,
CPU busy share, build and PVR wait times, heap and video RAM), the slow frames, a leak check, and where a crash or hang
happened if the log stops inside a step. The report holds no flash data and is safe to paste.

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


BENCH_KV = re.compile(r"(\w+)=([^\s]+)")


def console_lines(path):
    """The console's lines of a capture, or of a DreamPi log (its serial: b'...' lines)."""
    with open(path, encoding="utf-8", errors="replace") as src:
        raw = src.read().splitlines()
    pi = [m for m in (DREAMPI_LINE.search(r) for r in raw) if m]
    if not pi:
        return [r.rstrip("\r") for r in raw]
    out = []
    for m in pi:
        try:
            out.append(ast.literal_eval(m.group(1)).decode("latin-1").rstrip("\r"))
        except (ValueError, SyntaxError):
            pass
    return out


def bench_report(path):
    lines = console_lines(path)
    steps, drops, mem, cpu, load, runs = {}, [], {}, {}, {}, []
    boot, begun, done, crash, cfg = [], None, None, [], []
    for i, text in enumerate(lines):
        if text.startswith("OM boot"):
            boot.append(text[3:])
        elif text.startswith("OM cfg"):
            cfg.append(text[7:])
        if not text.startswith("OM bench "):
            if begun and ("Unhandled exception" in text or "kernel panic" in text.lower() or text.startswith("Stack Trace")
                          or text.startswith("PC=") or "assert" in text.lower()):
                crash.append(text)
            continue
        parts = text.split()
        kind = parts[2]
        if kind == "start":  # only the last test in the log is reported
            steps, drops, mem, cpu, load, runs, begun, done, crash = {}, [], {}, {}, {}, [], None, None, []
            continue
        if kind == "begin":
            begun = (parts[3], parts[4])
            crash = []
        elif kind == "step":
            key = parts[3]
            steps[key] = dict(BENCH_KV.findall(text), name=parts[4])
            begun = None
        elif kind in ("cpu", "load", "mem"):
            {"cpu": cpu, "load": load, "mem": mem}[kind][parts[3]] = dict(BENCH_KV.findall(text))
            m = re.search(r"heap_used=\d+ \(([+-]\d+)\)", text)
            v = re.search(r"vram_free=\d+ \(([+-]\d+)\)", text)
            if kind == "mem" and m:
                mem[parts[3]]["heap_delta"] = m.group(1)
            if kind == "mem" and v:
                mem[parts[3]]["vram_delta"] = v.group(1)
        elif kind == "drop":
            drops.append((parts[3], text.split(" ", 4)[4]))
        elif kind == "run":
            runs.append(text[len("OM bench "):])
        elif kind == "done":
            done = text[len("OM bench done "):]
    if not steps and not begun:
        sys.exit("No performance test lines (OM bench ...) in %s" % path)

    names = {}
    for key, st in steps.items():
        names.setdefault(key.split(".")[1], st["name"])
    run_ids = sorted({k.split(".")[0] for k in steps}, key=int)
    out = ["# openMenu performance test", "", "Source: %s" % os.path.basename(path), ""]
    out += ["Build: %s" % b for b in boot[:1]] + ["Start-up: %s" % b for b in boot[1:3]] + [""]
    if done:
        out += ["Result: **%s**" % done, ""]
    else:
        where = "%s (%s)" % (begun[1], begun[0]) if begun else "after the last step"
        out += ["Result: **the log ends inside step %s: a crash or a hang**" % where, ""]
    if crash:
        out += ["Crash output after `OM bench begin`:", "", "```"] + crash[:40] + ["```", ""]
    out += ["| Step | Run | Screen | fps | Drops | Worst ms | p95 ms | CPU busy | Build avg/max ms | Wait ms | GPU ms "
            "| Tris | Art loads | Heap change | VRAM change |",
            "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for idx in sorted(names, key=int):
        for r in run_ids:
            key = "%s.%s" % (r, idx)
            st = steps.get(key)
            if not st:
                continue
            c, l, m = cpu.get(key, {}), load.get(key, {}), mem.get(key, {})
            out.append("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
                st["name"], r, st.get("screen", "?"), st.get("fps", "?"), st.get("drops", "?"), st.get("worst", "?"),
                st.get("p95", "?"), c.get("busy", "?"), c.get("build", "?"), c.get("wait", "?"), c.get("gpu", "?"),
                l.get("tris", "?"), l.get("art", "?").split("/")[0], m.get("heap_delta", "?"),
                m.get("vram_delta", "?")))
    out += [""]
    if runs:
        out += ["Runs:", ""] + ["- " + r for r in runs] + [""]
        if len(runs) > 1:
            deltas = [int(m.group(1)) for m in (re.search(r"\(([+-]\d+) since the run began", r) for r in runs) if m]
            if len(deltas) > 1 and all(d > 1024 for d in deltas):
                out += ["**Leak suspected:** the heap grew in every run (%s bytes)." % ", ".join(map(str, deltas)), ""]
    if drops:
        out += ["Slow frames (first ones of each step):", "", "| Step | Frame |", "|---|---|"]
        out += ["| %s %s | %s |" % (k, names.get(k.split(".")[1], ""), d) for k, d in drops[:120]] + [""]
    if cfg:
        out += ["Card defaults (DEFAULTS.INI [BIOSMENU]):", ""] + ["- " + c for c in cfg] + [""]
    out += ["<details><summary>Raw lines</summary>", "", "```"]
    out += [t for t in lines if t.startswith("OM bench") or t.startswith("OM boot") or t.startswith("OM cfg")]
    out += ["```", "</details>", ""]
    name = "bench-report-%s.md" % stamp()
    with open(name, "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    print("Report written to %s (%d steps measured%s)" % (name, len(steps), "" if done else ", log ends inside a step"))


def main():
    if len(sys.argv) > 2 and sys.argv[1] == "--bench":
        bench_report(sys.argv[2])
        return
    arg = sys.argv[1] if len(sys.argv) > 1 else None
    if arg and os.path.isfile(arg) and not arg.startswith("/dev/"):
        from_dreampi_log(arg)
    else:
        live(arg or find_port())


if __name__ == "__main__":
    main()
