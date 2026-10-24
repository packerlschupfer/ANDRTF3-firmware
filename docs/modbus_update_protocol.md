# ANDRTF3 open firmware: update protocol over Modbus RTU

For a bus master that runs the update itself, e.g. as scheduled transactions between its own reads.
Reference implementation: `tools/mbflash.py`. Firmware side: `firmware/include/update.h`, `firmware/src/update.c`.
Requires the bootloader and the open firmware on the sensor. Addresses are 0-based register addresses.

## Preconditions (read once)
| Read | Expect |
|---|---|
| FC04 reg 2 | bit 15 set (open firmware) |
| FC04 reg 75 | high byte `0xFF` (no trial pending); otherwise wait until the running firmware has confirmed itself |
| FC04 regs 74–77 | 74 = running slot (0 = A, 1 = B), 75 = boot record, 76 = trial attempts, 77 = bootloader version (0 = no bootloader: stop) |

Send the image built for the **other** slot: `andrtf3_slot_b.bin` if reg 74 = 0, else `andrtf3_slot_a.bin`.
Both are release assets. An image for the wrong slot is rejected at verify. The image is the whole file,
padded with zero bytes to a multiple of 128.

## Sequence
1. **Begin:** FC06 reg 540 = `0x55AA`. The state (reg 541) becomes 1 (receiving). Repeating it restarts the transfer.
2. **Chunks:** for `off = 0, 128, 256, …`: FC16, start 544, **66 registers**, byte count 132:
   - reg 544 = `off >> 16`, reg 545 = `off & 0xFFFF`
   - regs 546–609 = the 128 image bytes as 64 big-endian words (`image[off+2k] << 8 | image[off+2k+1]`)

   Chunks must arrive in order. Re-sending the previous chunk is allowed, e.g. after a lost reply.
   The sensor replies first and programs flash afterwards (~105 ms). If the next chunk arrives during
   that time, its reply is simply delayed. Bus time per chunk is ~176 ms at 9600 baud. There is no
   timeout between chunks, so the master may spread them out as much as it likes.
3. **Verify:** FC06 reg 540 = `0xC0DE`, then FC03 reg 541. 2 = verified. Anything else is an error
   (see below); recover with 540 = `0xAB0B` (abort) and restart at step 1. Verify may be repeated.
4. **Activate:** FC06 reg 540 = `0xB007`. The sensor answers, then reboots about 200 ms later.
   - It does not respond for ~0.5 s: 0.2 s of delay plus ~0.4 s of bootloader CRC checks.
   - A repeated activate is ignored.
5. **Check:** after the reboot, FC04 reg 74 = the new slot means it is running **on trial**.
   - About 60 s after its boot, the firmware confirms itself: reg 75 = `slot | 0xFF00`.
     It also needs to have sent at least one Modbus reply, so keep polling.
     (active = new slot, no trial pending).
   - If it does not confirm within 3 boots, the bootloader starts the previous slot again:
     reg 74 = old slot and reg 75 = `old | 0xFF00`.

Until step 4 the running firmware is untouched. An aborted or interrupted transfer only leaves an
invalid image in the inactive slot, and the bootloader ignores it.

## Registers
| Reg | Type | Meaning |
|---|---|---|
| 540 | holding W | command: `0x55AA` begin, `0xC0DE` verify, `0xB007` activate, `0xAB0B` abort |
| 540, 541 | holding R | state: 0 idle, 1 receiving, 2 verified, 3 activating, `0x8101` sequence, `0x8102` offset, `0x8103` flash, `0x8104` image invalid (header/slot/size/CRC), `0x8105` bad command, `0x8106` running on trial (wait for the confirmation) |
| 542/543 | holding R | bytes received (high, low) |
| 544–609 | holding W (FC16 only) | data window |
| 74–77 | input | running slot, boot record (`active \| trial << 8`, `0xFF` = none, `0xFFFF` = no record), trial attempts, bootloader version |

Exceptions on a chunk:
- 03: wrong register count, or the offset is out of order. The state becomes `0x8102`: abort and restart.
- 04: not in the receiving state, a flash error, or the previous chunk is still being programmed.
  Retry after ~0.3 s; if it repeats, read 541.

## Image format (for checking before sending)
At offset 0xC0 of the file is `app_header_t` (little endian):
- magic `0x474D4941` ("AIMG")
- link address `0x08002000` (A) or `0x08010000` (B)
- `size`
- `fw_version` (u16)
- `hdr_version` = 1 (u16)

The 4 bytes at `size` are the CRC32 (zlib) over `[0, size)`. The file length is `size + 4`.
