#!/usr/bin/env python3
"""Timestamped serial logger: uartlog.py DEVICE BAUD OUTFILE [SECONDS]

OUTFILE may contain strftime fields (e.g. logs/debug_%Y%m%d.log) for one file per day.
SECONDS = 0 runs forever (default 3600). Exits with status 1 if the device goes away, so a
service manager can restart it. After SILENT_S seconds without data a '!! no data' line is written.
"""
import os, select, sys, time, termios, tty

SILENT_S = 120

dev, baud, pattern = sys.argv[1], int(sys.argv[2]), sys.argv[3]
dur = float(sys.argv[4]) if len(sys.argv) > 4 else 3600
fd = os.open(dev, os.O_RDONLY | os.O_NOCTTY)
tty.setraw(fd)
a = termios.tcgetattr(fd)
speed = getattr(termios, "B%d" % baud)
a[4] = a[5] = speed
a[2] = (a[2] & ~(termios.PARENB | termios.CSTOPB | termios.CSIZE)) | termios.CS8 | termios.CLOCAL | termios.CREAD
termios.tcsetattr(fd, termios.TCSANOW, a)

end = time.time() + dur if dur > 0 else float("inf")
buf = b""
f, fname = None, None
last_rx, silent = time.time(), False


def write(text):
    global f, fname
    t = time.time()
    name = time.strftime(pattern, time.localtime(t))
    if name != fname:
        if f:
            f.close()
        f, fname = open(name, "a", buffering=1), name
        f.write("# %s %s %d\n" % (time.strftime("%F %T"), dev, baud))
    f.write("%s.%03d %s\n" % (time.strftime("%H:%M:%S", time.localtime(t)), int(t * 1000) % 1000, text))


write("# logger start")
while time.time() < end:
    ready, _, _ = select.select([fd], [], [], 5)
    if not ready:
        if not silent and time.time() - last_rx > SILENT_S:
            write("!! no data for %d s" % SILENT_S)
            silent = True
        continue
    try:
        chunk = os.read(fd, 256)
    except OSError as e:
        write("!! device error: %s" % e)
        sys.exit(1)
    if not chunk:
        write("!! device closed")
        sys.exit(1)
    last_rx, silent = time.time(), False
    buf += chunk
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        write(line.rstrip(b"\r").decode("latin-1"))
