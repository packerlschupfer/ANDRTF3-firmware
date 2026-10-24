# @runtime Jython
import os
from ghidra.app.cmd.disassemble import ArmDisassembleCommand
from ghidra.program.model.address import AddressSet
args = getScriptArgs(); s = toAddr(long(args[0], 16)); e = toAddr(long(args[1], 16))
ArmDisassembleCommand(AddressSet(s, e), None, True).applyTo(currentProgram, monitor)
