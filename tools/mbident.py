#!/usr/bin/env python3
"""Read or set the identity of an ANDRTF3 with the open firmware.

usage: mbident.py DEVICE [--addr 4] [--asset N] [--label TEXT]

Without --asset/--label: prints the factory unique ID, asset number and label.
With them: writes holding 532 (asset) / 533..539 (label, max. 14 printable ASCII chars) and saves
the settings to EEPROM (command 0x5AFE). Note that the save writes ALL current settings, so any
unsaved change made before (calibration, offset, interval) is saved too.

Only one Modbus master on the bus (bench: systemctl --user stop andrtf3-modbus).
"""
import argparse, sys, time
import serial
from mbflash import Master, ModbusError

REG_UID, REG_ASSET, REG_LABEL, LABEL_REGS = 78, 532, 533, 7
REG_CMD, REG_STATE, CMD_SAVE, STATE_SAVED = 530, 531, 0x5AFE, 5


def label_regs(text):
    raw = text.encode("ascii")
    if len(raw) > 2 * LABEL_REGS or any(not 0x20 <= b <= 0x7E for b in raw):
        sys.exit("label: max. %d printable ASCII characters" % (2 * LABEL_REGS))
    raw = raw.ljust(2 * LABEL_REGS, b"\0")
    return [raw[i] << 8 | raw[i + 1] for i in range(0, len(raw), 2)]


def show(m):
    uid = m.retry(m.read, 4, REG_UID, 6)
    hold = m.retry(m.read, 3, REG_ASSET, 1 + LABEL_REGS)
    label = b"".join(bytes([w >> 8, w & 0xFF]) for w in hold[1:]).split(b"\0")[0].decode("ascii", "replace")
    print("uid    %s" % "".join("%04X" % w for w in uid))
    print("asset  %d" % hold[0])
    print("label  %r" % label)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("device")
    ap.add_argument("--addr", type=int, default=4)
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--parity", default="E", choices="NEO")
    ap.add_argument("--asset", type=int, help="0..65535")
    ap.add_argument("--label", help="max. 14 printable ASCII characters")
    a = ap.parse_args()

    m = Master(a.device, a.addr, a.baud, a.parity)
    ver = m.retry(m.read, 4, 2, 1)[0]
    if not ver & 0x8000:
        sys.exit("device firmware 0x%04X has no identity registers (needs the open firmware)" % ver)

    if a.asset is not None or a.label is not None:
        if a.asset is not None:
            if not 0 <= a.asset <= 0xFFFF:
                sys.exit("asset: 0..65535")
            m.retry(m.write1, REG_ASSET, a.asset)
        if a.label is not None:
            m.retry(m.write_n, REG_LABEL, label_regs(a.label))
        m.retry(m.write1, REG_CMD, CMD_SAVE)
        time.sleep(0.2)
        st = m.retry(m.read, 3, REG_STATE, 1)[0]
        if st != STATE_SAVED:
            sys.exit("save failed: state 0x%04X" % st)
        print("saved")
    show(m)


if __name__ == "__main__":
    try:
        main()
    except (ModbusError, serial.SerialException) as e:
        sys.exit("error: %s" % e)
