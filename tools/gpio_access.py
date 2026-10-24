#!/usr/bin/env python3
"""List GPIO set/reset/read accesses per function in resolved decompiled C."""
import re, sys
REG = {0x10: "IDR", 0x14: "ODR", 0x18: "BSRR", 0x28: "BRR"}
func = None; alias = {}
res = {}
for line in open(sys.argv[1]):
    m = re.match(r'/\* ==== (\S+) @', line)
    if m: func = m.group(1); alias = {}; continue
    for v, port in re.findall(r'(\w*Var\d+) = (GPIO[A-H])', line): alias[v] = port
    if "FUN_08002334" in line: continue
    l = re.sub(r'/\*.*?\*/', '', line)
    for v, p in alias.items(): l = re.sub(r'\b%s\b' % v, p, l)
    l = l.replace("&DAT_50000000", "GPIOA").replace("DAT_50000000", "GPIOA")
    for port, off, rhs in re.findall(r'(GPIO[A-H])\s*\+\s*(0x[0-9a-f]+)\)\s*=\s*([^;]+);', l):
        res.setdefault(func, []).append("%s.%s = %s" % (port, REG.get(int(off, 16), off), rhs.strip()))
    for port, idx, rhs in re.findall(r'(GPIO[A-H])\[(\d+)\]\s*=\s*([^;]+);', l):
        res.setdefault(func, []).append("%s.%s = %s" % (port, REG.get(int(idx) * 4, "+%d" % (int(idx) * 4)), rhs.strip()))
    for port, off in re.findall(r'(GPIO[A-H])\s*\+\s*(0x10)\)', l):
        if "=" not in l.split(port)[0][-3:]:
            res.setdefault(func, []).append("%s.IDR read" % port)
    for port in re.findall(r'(GPIO[A-H])\[4\]', l):
        res.setdefault(func, []).append("%s.IDR read" % port)
def pins(v):
    try: n = int(v, 0)
    except ValueError: return v
    return "|".join(str(i) for i in range(16) if n >> i & 1) or v
for f, acc in res.items():
    seen = []
    for a in acc:
        m = re.match(r'(GPIO\w)\.(BSRR|BRR) = (.*)', a)
        s = "%s P%s%s" % ("SET" if m.group(2) == "BSRR" else "CLR", m.group(1)[-1], pins(m.group(3))) if m else a
        if s not in seen: seen.append(s)
    print("%-14s %s" % (f, "; ".join(seen)))
