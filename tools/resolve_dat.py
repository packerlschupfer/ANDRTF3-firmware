#!/usr/bin/env python3
"""Replace Ghidra DAT_/PTR_ literal-pool names in decompiled C with the word they hold."""
import re, struct, sys
flash = open(sys.argv[1], 'rb').read()
names = {0x50000000: "GPIOA", 0x50000400: "GPIOB", 0x50000800: "GPIOC", 0x50000C00: "GPIOD",
         0x50001C00: "GPIOH", 0x40013000: "SPI1", 0x40003800: "SPI2", 0x40005400: "I2C1",
         0x40005800: "I2C2", 0x40013800: "USART1", 0x40004400: "USART2", 0x40005000: "USART5",
         0x40004C00: "USART4", 0x40004800: "LPUART1", 0x40021000: "RCC", 0x40012400: "ADC1",
         0x40000000: "TIM2", 0x40010400: "EXTI", 0x40020000: "DMA1"}
def word(a):
    o = a - 0x08000000
    return struct.unpack_from('<I', flash, o)[0] if 0 <= o < len(flash) - 3 else None
def sub(m):
    v = word(int(m.group(2), 16))
    if v is None: return m.group(0)
    return "%s/*%s*/" % (names.get(v, "0x%08X" % v), m.group(0)) if True else m.group(0)
src = open(sys.argv[2]).read()
print(re.sub(r'\b(DAT|PTR_[A-Za-z0-9_]*?)_([0-9a-f]{8})\b', sub, src))
