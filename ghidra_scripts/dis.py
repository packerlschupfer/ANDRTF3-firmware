# @runtime Jython
import os
from ghidra.app.cmd.disassemble import ArmDisassembleCommand
args = getScriptArgs(); a = toAddr(long(args[0], 16)); end = toAddr(long(args[1], 16))
out = open(os.environ.get("GH_OUT", "/tmp/gh_out.txt"), "w")
lst = currentProgram.getListing()
while a.compareTo(end) < 0:
    ins = lst.getInstructionAt(a)
    if ins is None:
        out.write("%s  <no insn> %04x\n" % (a, currentProgram.getMemory().getShort(a) & 0xffff)); a = a.add(2); continue
    out.write("%s  %s\n" % (a, ins)); a = ins.getMaxAddress().add(1)
out.close()
