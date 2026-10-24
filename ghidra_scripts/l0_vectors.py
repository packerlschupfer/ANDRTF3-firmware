# @category ANDRTF3
# @runtime Jython
# Label the STM32L0x1 vector table, create handler functions, add MMIO blocks, re-analyse.
import os
from ghidra.program.model.symbol import SourceType
from ghidra.app.cmd.disassemble import ArmDisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd

CORE = ["Initial_SP", "Reset_Handler", "NMI_Handler", "HardFault_Handler", None, None, None, None,
        None, None, None, "SVC_Handler", None, None, "PendSV_Handler", "SysTick_Handler"]
IRQ = ["WWDG", "PVD", "RTC", "FLASH", "RCC_CRS", "EXTI0_1", "EXTI2_3", "EXTI4_15", "IRQ8",
       "DMA1_Channel1", "DMA1_Channel2_3", "DMA1_Channel4_7", "ADC1_COMP", "LPTIM1", "USART4_5",
       "TIM2", "TIM3", "TIM6", "TIM7", "IRQ19", "TIM21", "I2C3", "TIM22", "I2C1", "I2C2", "SPI1",
       "SPI2", "USART1", "USART2", "LPUART1", "IRQ30", "IRQ31"]
out = open(os.environ.get("GH_OUT", "/tmp/gh_out.txt"), "w")
mem = currentProgram.getMemory()
st = currentProgram.getSymbolTable()
base = 0x08000000
names = CORE + [n + "_IRQHandler" for n in IRQ]
seen = {}
for i, n in enumerate(names):
    a = toAddr(base + 4 * i)
    v = mem.getInt(a) & 0xffffffff
    createLabel(a, "vec_%d" % i, True)
    if i == 0 or n is None or v == 0:
        continue
    tgt = toAddr(v & ~1)
    if tgt in seen:   # shared default handler
        seen[tgt].append(n); continue
    seen[tgt] = [n]
for tgt, ns in seen.items():
    name = ns[0] if len(ns) == 1 else "Default_Handler"
    ArmDisassembleCommand(tgt, None, True).applyTo(currentProgram, monitor)
    f = getFunctionAt(tgt)
    if f is None:
        CreateFunctionCmd(tgt).applyTo(currentProgram, monitor); f = getFunctionAt(tgt)
    if f: f.setName(name, SourceType.USER_DEFINED)
    out.write("%s %s %s\n" % (tgt, name, ",".join(ns) if len(ns) > 1 else ""))
# peripheral / RAM blocks so MMIO references resolve
def blk(name, start, size, vol=True):
    try:
        b = mem.createUninitializedBlock(name, toAddr(start), size, False)
        b.setRead(True); b.setWrite(True); b.setVolatile(vol)
    except Exception as e:
        out.write("block %s: %s\n" % (name, e))
blk("SRAM", 0x20000000, 0x5000, False)
blk("APB1", 0x40000000, 0x8000)
blk("APB2", 0x40010000, 0x6000)
blk("AHB", 0x40020000, 0x4000)
blk("IOPORT", 0x50000000, 0x2000)
blk("SYSCFG_OB", 0x1FF80000, 0x100, False)
blk("SCS", 0xE000E000, 0x1000)
out.close()
