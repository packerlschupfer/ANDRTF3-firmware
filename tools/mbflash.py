#!/usr/bin/env python3
"""Update the ANDRTF3 open firmware over Modbus RTU (needs the bootloader + open firmware).

usage: mbflash.py DEVICE IMAGE_DIR_OR_FILES... [--addr 4] [--baud 9600] [--parity E] [--gap 0.12]

IMAGE: the directory with andrtf3_slot_a.bin / andrtf3_slot_b.bin (e.g. a release download or
firmware/.pio/build/*/), or both files. The tool asks the sensor which slot is running and sends
the build for the other slot.

Each 128-byte chunk is one Modbus transaction; the bus is released for --gap seconds between
chunks. Only ONE master may be active on the bus: pause any other poller first (on the bench:
systemctl --user stop andrtf3-modbus). The sensor keeps measuring and answering during the transfer.
"""
import argparse, glob, os, struct, sys, time
import serial

PAGE = 128
REG_CMD, REG_STATE, REG_DATA = 540, 541, 544
CMD_BEGIN, CMD_VERIFY, CMD_ACTIVATE, CMD_ABORT = 0x55AA, 0xC0DE, 0xB007, 0xAB0B
STATE = {0: "idle", 1: "receiving", 2: "verified", 3: "activating", 0x8101: "sequence error",
         0x8102: "offset error", 0x8103: "flash error", 0x8104: "image invalid", 0x8105: "bad command",
         0x8106: "running on trial - wait for the confirmation"}
SLOT_BASE = {0: 0x08002000, 1: 0x08010000}


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


class ModbusError(Exception):
    pass


class Master:
    def __init__(self, dev, addr, baud, parity):
        self.addr = addr
        self.ser = serial.Serial(dev, baud, parity=parity, stopbits=1, bytesize=8, timeout=1.0)

    def _xfer(self, pdu, resp_len):
        req = bytes([self.addr]) + pdu
        req += struct.pack("<H", crc16(req))
        self.ser.reset_input_buffer()
        self.ser.write(req)
        head = self.ser.read(2)
        if len(head) < 2:
            raise ModbusError("timeout")
        if head[1] & 0x80:
            # exception frame: addr, fc|0x80, code, CRC - only trusted with a good CRC
            frame = head + self.ser.read(3)
            if len(frame) != 5 or crc16(frame) != 0 or frame[0] != self.addr:
                raise ModbusError("bad response %s" % frame.hex())
            raise ModbusError("exception %d" % frame[2])
        rest = self.ser.read(resp_len - 2 + 2)
        frame = head + rest
        if len(frame) != resp_len + 2 or crc16(frame) != 0 or frame[0] != self.addr:
            raise ModbusError("bad response %s" % frame.hex())
        return frame[:-2]

    def read(self, fc, reg, n):
        f = self._xfer(struct.pack(">BHH", fc, reg, n), 3 + 2 * n)
        return list(struct.unpack(">%dH" % n, f[3:]))

    def write1(self, reg, val):
        self._xfer(struct.pack(">BHH", 6, reg, val), 6)

    def write_n(self, reg, values):
        pdu = struct.pack(">BHHB", 16, reg, len(values), 2 * len(values))
        pdu += struct.pack(">%dH" % len(values), *values)
        self._xfer(pdu, 6)

    def retry(self, fn, *args, tries=5):
        for i in range(tries):
            try:
                return fn(*args)
            except ModbusError as e:
                if i == tries - 1:
                    raise
                print("  retry (%s)" % e)
                time.sleep(0.3)


def find_images(paths):
    files = []
    for p in paths:
        files += glob.glob(os.path.join(p, "**", "andrtf3_slot_*.bin"), recursive=True) if os.path.isdir(p) else [p]
    found = {}
    for f in files:
        img = open(f, "rb").read()
        if len(img) < 0xD0:
            continue
        magic, link, size, ver, _ = struct.unpack_from("<IIIHH", img, 0xC0)
        for slot, base in SLOT_BASE.items():
            if magic == 0x474D4941 and link == base:
                found[slot] = (f, img, ver)
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("device")
    ap.add_argument("images", nargs="+")
    ap.add_argument("--addr", type=int, default=4)
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--parity", default="E", choices="NEO")
    ap.add_argument("--gap", type=float, default=0.12, help="bus idle time between chunks, s")
    ap.add_argument("--no-wait", action="store_true", help="do not wait for the confirmation (keep polling: it needs Modbus replies)")
    a = ap.parse_args()

    images = find_images(a.images)
    m = Master(a.device, a.addr, a.baud, a.parity)

    ver = m.retry(m.read, 4, 2, 1)[0]
    if not ver & 0x8000:
        sys.exit("device firmware 0x%04X has no Modbus update support (needs the open firmware)" % ver)
    slot, rec, tries, boot = m.retry(m.read, 4, 74, 4)
    if boot == 0:
        sys.exit("no bootloader on the device")
    if rec != 0xFFFF and (rec >> 8) != 0xFF:
        sys.exit("slot %s is still on trial (boot record 0x%04X): wait until it confirms itself"
                 % ("AB"[rec >> 8 & 1], rec))
    target = 1 - slot
    if target not in images:
        sys.exit("no image for slot %s found in %s" % ("AB"[target], a.images))
    f, img, new_ver = images[target]
    print("device: fw 0x%04X running slot %s, bootloader 0x%04X -> sending %s (fw 0x%04X, %d bytes) to slot %s"
          % (ver, "AB"[slot], boot, f, new_ver, len(img), "AB"[target]))

    img += b"\0" * (-len(img) % PAGE)
    m.retry(m.write1, REG_CMD, CMD_BEGIN)
    t0 = time.time()
    for off in range(0, len(img), PAGE):
        chunk = img[off:off + PAGE]
        words = list(struct.unpack(">%dH" % (PAGE // 2), chunk))
        m.retry(m.write_n, REG_DATA, [off >> 16, off & 0xFFFF] + words)
        done = off + PAGE
        print("\r  %6d / %d bytes  %3d %%  %4.0f s" % (done, len(img), 100 * done // len(img), time.time() - t0),
              end="", flush=True)
        time.sleep(a.gap)
    print()

    m.retry(m.write1, REG_CMD, CMD_VERIFY)
    st = m.retry(m.read, 3, REG_STATE, 1)[0]
    if st != 2:
        m.retry(m.write1, REG_CMD, CMD_ABORT)
        sys.exit("verify failed: state 0x%04X (%s)" % (st, STATE.get(st, "?")))
    print("verified; activating")
    m.retry(m.write1, REG_CMD, CMD_ACTIVATE)

    time.sleep(3)
    for _ in range(20):
        try:
            ver2 = m.read(4, 2, 1)[0]
            slot2, rec2, tries2, _ = m.read(4, 74, 4)
            break
        except ModbusError:
            time.sleep(1)
    else:
        sys.exit("device does not answer after the reboot - the bootloader will roll back after 3 failed boots")
    if slot2 != target:
        sys.exit("device is running slot %s (fw 0x%04X) - the new image did not start" % ("AB"[slot2], ver2))
    print("running fw 0x%04X in slot %s on trial (attempt %d)" % (ver2, "AB"[slot2], tries2))
    if a.no_wait:
        return
    print("waiting for the firmware to confirm itself (60 s after boot, needs Modbus traffic) ...")
    rec3 = None
    for _ in range(90):
        time.sleep(1)
        try:
            slot3, rec3, _, _ = m.read(4, 74, 4)
        except ModbusError:
            continue
        if slot3 != target:
            sys.exit("!! device rebooted into slot %s - rollback" % "AB"[slot3])
        if rec3 == (target | 0xFF00):
            print("confirmed: slot %s is now the active firmware" % "AB"[target])
            return
    sys.exit("not confirmed after 90 s (boot record %s)" % ("?" if rec3 is None else "0x%04X" % rec3))


if __name__ == "__main__":
    try:
        main()
    except (ModbusError, serial.SerialException) as e:
        sys.exit("error: %s" % e)
