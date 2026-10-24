# @category ANDRTF3
# @runtime Jython
# Create functions from (a) thumb function pointers stored in flash, (b) uncovered PUSH {..,LR} prologues.
import os
from ghidra.app.cmd.disassemble import ArmDisassembleCommand
from ghidra.app.cmd.function import CreateFunctionCmd
out = open(os.environ.get("GH_OUT", "/tmp/gh_out.txt"), "w")
mem = currentProgram.getMemory(); fm = currentProgram.getFunctionManager(); listing = currentProgram.getListing()
LO, HI = 0x08000000, 0x0800BABC
def make(a):
    addr = toAddr(a)
    if fm.getFunctionContaining(addr) is not None: return False
    ins = listing.getInstructionAt(addr)
    if ins is None:
        ArmDisassembleCommand(addr, None, True).applyTo(currentProgram, monitor)
        if listing.getInstructionAt(addr) is None: return False
    return CreateFunctionCmd(addr).applyTo(currentProgram, monitor)
n1 = n2 = 0
for rnd in range(3):
    for off in range(0, HI - LO, 4):
        v = mem.getInt(toAddr(LO + off)) & 0xffffffff
        if (v & 1) and LO <= v < HI and make(v & ~1): n1 += 1
    for off in range(0, HI - LO, 2):
        a = LO + off
        hw = mem.getShort(toAddr(a)) & 0xffff
        if (hw & 0xff00) == 0xb500 and make(a): n2 += 1
out.write("from pointers: %d, from prologues: %d, total functions: %d\n" % (n1, n2, fm.getFunctionCount()))
out.close()
