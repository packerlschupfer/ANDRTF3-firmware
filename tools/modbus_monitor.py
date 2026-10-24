#!/usr/bin/env python3
"""Modbus RTU health monitor for the ANDRTF3 (open firmware, falls back to reg 50 only).

Polls input registers 50..73 every INTERVAL seconds and logs one line per poll. Lines that need
attention start with '!!': no response, bad CRC, exception, temperature 0 / invalid status,
increased failure or bus-error counters, device reboot.

usage: modbus_monitor.py DEVICE OUTFILE [--addr 4] [--baud 9600] [--parity E] [--interval 10]
OUTFILE may contain strftime fields (logs/modbus_%Y%m%d.log). Needs pyserial.
"""
import argparse, struct, sys, time
import serial

FIRST, COUNT = 50, 24          # input registers 50..73
SUMMARY_EVERY = 3600           # s


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


class Log:
    def __init__(self, pattern):
        self.pattern, self.name, self.f = pattern, None, None

    def __call__(self, text):
        t = time.time()
        name = time.strftime(self.pattern, time.localtime(t))
        if name != self.name:
            if self.f:
                self.f.close()
            self.f, self.name = open(name, "a", buffering=1), name
        self.f.write("%s.%03d %s\n" % (time.strftime("%H:%M:%S", time.localtime(t)), int(t * 1000) % 1000, text))


def read_input(ser, addr, first, count):
    """Return (list of registers | None, error string | None)."""
    req = struct.pack(">BBHH", addr, 4, first, count)
    req += struct.pack("<H", crc16(req))
    ser.reset_input_buffer()
    ser.write(req)
    head = ser.read(3)
    if len(head) < 3:
        return None, "timeout" if not head else "short response (%d bytes)" % len(head)
    if head[1] == 0x84:
        rest = ser.read(2)
        frame = head + rest
        if len(frame) == 5 and crc16(frame) == 0:
            return None, "exception %d" % head[2]
        return None, "bad exception frame %s" % frame.hex()
    n = head[2]
    rest = ser.read(n + 2)
    frame = head + rest
    if len(rest) < n + 2:
        return None, "short response (%d bytes)" % len(frame)
    if crc16(frame) != 0:
        return None, "CRC error %s" % frame.hex()
    if head[0] != addr or head[1] != 4 or n != 2 * count:
        return None, "unexpected frame %s" % frame.hex()
    return list(struct.unpack(">%dH" % count, frame[3:3 + n])), None


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("device")
    ap.add_argument("outfile")
    ap.add_argument("--addr", type=int, default=4)
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--parity", default="E", choices="NEO")
    ap.add_argument("--interval", type=float, default=10)
    a = ap.parse_args()

    log = Log(a.outfile)
    ser = serial.Serial(a.device, a.baud, parity=a.parity, stopbits=1, bytesize=8, timeout=0.5)
    log("# monitor start %s addr=%d %d 8%s1 every %gs" % (a.device, a.addr, a.baud, a.parity, a.interval))

    prev, prev_t = None, 0.0
    full = True
    stats = {"polls": 0, "errors": 0}
    t_summary = time.time()

    while True:
        t0 = time.time()
        stats["polls"] += 1
        regs, err = read_input(ser, a.addr, FIRST, COUNT) if full else read_input(ser, a.addr, FIRST, 1)
        if err == "exception 2" and full:
            full = False                    # original firmware / older open firmware: reg 50 only
            log("# registers 51..73 not available - polling reg 50 only")
            regs, err = read_input(ser, a.addr, FIRST, 1)

        if err:
            stats["errors"] += 1
            log("!! %s" % err)
        elif not full:
            t = s16(regs[0])
            log("%sT=%.1f" % ("!! " if t == 0 else "", t / 10))
        else:
            r = dict(zip(range(FIRST, FIRST + COUNT), regs))
            up = (r[58] << 16) | r[59]
            bus = [r[i] for i in range(67, 73)]        # crc badlen fe pe ne ore
            alarms = []
            if r[50] == 0:
                alarms.append("T=0")
            if not r[51] & 1:
                alarms.append("status=0x%02x" % r[51])
            if prev:
                pup = (prev[58] << 16) | prev[59]
                if up + 3 < pup + (t0 - prev_t) or r[64] != prev[64]:    # uptime fell behind the clock
                    alarms.append("REBOOT (reset cause 0x%02x)" % r[64])
                else:
                    if r[55] != prev[55]:
                        alarms.append("fail +%d (last reason %d)" % ((r[55] - prev[55]) & 0xFFFF, r[56]))
                    pbus = [prev[i] for i in range(67, 73)]
                    if bus != pbus:
                        alarms.append("bus errors +%s" % [(x - y) & 0xFFFF for x, y in zip(bus, pbus)])
            log("%sT=%.1f st=0x%02x R=%.1f age=%d good=%d fail=%d up=%d rst=0x%02x "
                "bus fr=%d ok=%d crc=%d badlen=%d fe=%d pe=%d ne=%d ore=%d exc=%d%s" % (
                    "!! " if alarms else "", s16(r[50]) / 10, r[51], r[52] / 10, r[53], r[54], r[55], up, r[64],
                    r[65], r[66], *bus, r[73], ("  <- " + ", ".join(alarms)) if alarms else ""))
            prev, prev_t = r, t0

        if time.time() - t_summary >= SUMMARY_EVERY:
            log("SUMMARY polls=%d errors=%d" % (stats["polls"], stats["errors"]))
            full = True                     # retry the full block (firmware may have been updated)
            t_summary = time.time()
        time.sleep(max(0.0, a.interval - (time.time() - t0)))


if __name__ == "__main__":
    try:
        main()
    except serial.SerialException as e:
        print("serial error: %s" % e, file=sys.stderr)
        sys.exit(1)
