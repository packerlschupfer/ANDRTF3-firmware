#!/usr/bin/env python3
"""Extract LL_GPIO_Init(port, {Pin, Mode, Speed, OutputType, Pull, Alternate}) calls from decompiled C."""
import re, sys
MODE = {0: "input", 1: "output", 2: "AF", 3: "analog"}
PULL = {0: "-", 1: "pull-up", 2: "pull-down"}
OTYP = {0: "push-pull", 1: "open-drain"}
func = None; vals = {}; alias = {}
for line in open(sys.argv[1]):
    m = re.match(r'/\* ==== (\S+) @ (\S+)', line)
    if m: func = m.group(1); vals = {}; alias = {}; continue
    m = re.match(r'\s*(local_[0-9a-f]+) = (0x[0-9a-f]+|\d+);', line)
    if m: vals[m.group(1)] = int(m.group(2), 0); continue
    m = re.match(r'\s*(\w*Var\d+) = (GPIO[A-H])', line)
    if m: alias[m.group(1)] = m.group(2); continue
    m = re.search(r'FUN_08002334\(([^,]+),&(local_[0-9a-f]+)', line)
    if m:
        port = m.group(1)
        port = re.sub(r'/\*.*?\*/', '', port).strip()
        port = alias.get(port, port).replace("&DAT_50000000", "GPIOA")
        base = int(m.group(2)[6:], 16)
        f = [vals.get("local_%x" % (base - 4 * i)) for i in range(6)]
        pin, mode, speed, otype, pull, af = f
        pins = [i for i in range(16) if pin and pin >> i & 1]
        name = ",".join("P%s%d" % (port[-1], p) for p in pins)
        print("%-14s %-12s %-7s speed=%s %-10s %-9s AF%s" % (func, name, MODE.get(mode, mode), speed,
              OTYP.get(otype, otype), PULL.get(pull, pull), af))
