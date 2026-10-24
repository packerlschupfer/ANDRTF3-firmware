# ANDRTF3 open firmware (STM32L071RBT6)

Replacement firmware for the Andivi ANDRTF3/MD wall sensor, **minimal variant** (PT1000 only,
no LCD, no button, no add-on sensors). Hardware facts come from the analysis of the original
firmware in `../docs/firmware_analysis.md`.

Status: **v0.1 (bootloader + A/B update over Modbus), installed (Modbus address 4).**

## Build
```bash
pio run            # boot/firmware.bin, slot_a/andrtf3_slot_a.bin, slot_b/andrtf3_slot_b.bin
```
PlatformIO `ststm32` + `stm32cube` (CMSIS headers, startup, linker script). Peripherals are
driven at register level; no HAL calls. 16 MHz HSI, no crystal on the board.

## Behaviour
- Boot: read DIP switches (S2 = address, S1 = serial settings, same decoding as the original),
  start Modbus RTU on USART1 with hardware RS485 driver-enable (PB3), print a banner on the debug UART.
- Every second (holding 518): one MCP355x conversion (non-blocking), resistance check 500..3000 Ohm,
  Callendar-Van Dusen incl. the C term below 0 degC, reported in 0.1 degC (reg 50, rounded) and 0.01 degC (reg 57).
- Independent watchdog (~2 s), heartbeat toggles PB4.
- Debug UART: USART5 TX on PC12, 115200 8N1. Boot banner and failed measurements always,
  every measurement when holding register 515 != 0 (default 1 during bring-up). Line format:
  `<ms> PT1000 ok raw=<24-bit hex> rdy=<ms> code=<22-bit> R=<mOhm> mOhm T=<0.01 C>`,
  failures are prefixed with `!!`.
- Boot: `RESET cause=<IWDG|WWDG|LPWR|POR|SOFT|OBL|FW|PIN> flags=0x..` (RCC_CSR 31..24; a watchdog
  reset is flagged with `!!`).
- Every 60 s: `STAT` (measurement counters, settings) and `BUS` (Modbus frames received/handled,
  CRC, bad length, UART framing/parity/noise/overrun errors, exceptions sent).

## Modbus map (0-based addresses)
See the table at the top of `include/regs.h` (authoritative). Summary:

| Type | Reg | Meaning |
|---|---|---|
| Input | 1 / 2 | device type 1 / firmware version (0x8001 = v0.1, bit 15 = open firmware; original: 7) |
| Input | **50** | temperature 0.1 degC: median of 3, offset applied; invalid -> 0 like the original (also on ADC timeouts, where the original keeps the old value), but up to 2 consecutive bad readings are bridged while the filter is on |
| Input | 51 | status: bit0 = reg 50 trustworthy (also while bridged), bits 1-3 last raw reading failed (timeout / overflow / range), 4 never measured, 5 held by filter |
| Input | 52 | PT1000 resistance, 0.1 Ohm |
| Input | 53 | seconds since last good reading |
| Input | 54-56 | good / failed counters, last failure reason |
| Input | 57 | temperature 0.01 degC (last good, unfiltered) |
| Input | 58/59 | uptime s (hi/lo) |
| Input | 60-62 | timeout / range / overflow counters |
| Input | 63 | bit0 settings from EEPROM, bit1 unsaved changes |
| Input | 64 | reset cause (RCC_CSR 31..24: 0x20 IWDG, 0x10 soft/SWD, 0x08 power-on, 0x04 NRST pin) |
| Input | 65-73 | bus: frames, handled, CRC, bad length, framing, parity, noise, overrun, exceptions (see note below) |
| Input | 74-77 | running slot (0 A, 1 B), boot record (active \| trial<<8), trial attempts, bootloader version |
| Input | 78-83 | factory unique ID (96 bit, high word first), different on every chip |
| Holding | 515 | debug trace (default 1) |
| Holding | 516 | 1 = keep last good value instead of 0 |
| Holding | 517 | filter on/off (default 1) |
| Holding | 518 | interval ms (default 1000) |
| Holding | 520/521 | R_ref mOhm hi/lo (default 10 000 000) |
| Holding | 522 | offset 0.01 degC |
| Holding | 523 | R_cal 0.1 Ohm (default 10000) |
| Holding | 530 / 531 | command / calibration state |
| Holding | 532 | asset number (user ID, default 0) |
| Holding | 533-539 | label, 14 printable ASCII chars, 2 per register, high byte first, NUL padded |
| Holding | 540-543 | update command / state / bytes received (see `include/update.h`) |
| Holding | 544-609 | update data window (FC16, 66 registers) |

The bus counters see every frame on the bus, not only the sensor's. A frame ends after 3.5 character
times of silence (~4 ms at 9600 8E1). If another slave answers its master faster than that, the
sensor sees request and reply as one frame with a bad CRC. So on a bus with other slaves, CRC
(67) also counts healthy foreign traffic: seen on the installed unit, 2345 of 5976 frames. It is
only a line-quality metric when the sensor is the only slave. A real line problem shows up
as framing/parity/noise errors (69-71), or as CRC errors on the sensor's own requests
(66 handled stalls while the master retries).

Settings live in RAM until command 0x5AFE writes them to the data EEPROM (0x08080100 and a copy
at 0x08080140, CRC32).

## Identity
Every chip has a 96-bit factory unique ID (input 78-83, read only). For your own numbering there is
an asset number (holding 532) and a 14-character label (533-539), saved with the settings. Factory
defaults (0xDEF0) keep them; they survive firmware updates. The boot banner prints all three:
`ID uid=003400213034303801473331 asset=1 label="Bench"`.
```bash
../tools/mbident.py /dev/ttyUSB485                          # show
../tools/mbident.py /dev/ttyUSB485 --asset 2 --label "Living room"   # set and save
```
The save writes all current settings, including unsaved changes made before.

## Calibration
The 10 kOhm reference resistor dominates the error (0.1 % = ~0.28 K). It is calibrated without
disconnecting the PT1000 by clipping a known resistor (R_cal, default 1000.0 Ohm) across it:
`R_ref = R_cal * (1/b - 1/a)` with a = R_pt/R_ref (alone), b = (R_pt||R_cal)/R_ref.
The PT1000 value cancels out; the R_cal tolerance carries over 1:1.

```bash
../tools/calibrate.sh 4 /dev/ttyUSB485     # guided: A, B (resistor on), A, compute, save
```
Manual: 530=4 (clear), 530=1 (A), 530=2 (B, resistor on), 530=1 (A again), 530=3 (compute),
check 531=4 and regs 52/57, then 530=0x5AFE (save). During captures reg 50 is frozen, but between
clipping the resistor on and starting capture B the normal readings see ~522 Ohm - do it while no
master depends on the value.

Offset (e.g. against a reference thermometer): write 522 in 0.01 degC, then 530=0x5AFE.

Supported function codes: 03, 04, 06, 16. Broadcast (address 0) writes are accepted silently.

Differences from the original that a Modbus master could notice:
- reg 2 = 0x8001 instead of 7; input registers go up to 83 instead of 64
- an ADC timeout sets reg 50 to 0 as well (the original keeps the last value, so a dead ADC
  looked like a frozen, valid temperature)
- FC16 to the settings answers exception 03 if a value is rejected (registers before it are written)
- temperature is rounded instead of truncated (max. 0.1 degC difference)
- discrete inputs (FC02) and coils are not implemented (exception 01)
- Modbus ASCII is not implemented (DIP S1 bit 2 is logged and ignored)

## Verified on the hardware (2026-10-24)
- DIP decoding: `addr=4  9600 8E1`
- PT1000 ~1092 Ohm, same as the original's own debug output
- FC03/04/06/16 and exceptions via mbpoll
- Still unknown: outputs PA0/PA8/PB5/PC9 (kept at the original levels), inputs PC6/PC10

## Flash layout and updates over Modbus
```
0x08000000   8 KB  bootloader   (SWD, once)
0x08002000  56 KB  slot A       bank 1
0x08010000  56 KB  slot B       bank 2
```
The firmware is built twice (`slot_a`, `slot_b`). An update goes into the slot that is not
running, always in the other flash bank, so the sensor keeps measuring and answering during the
transfer. After `activate` it reboots (~0.2 s offline) into the new slot **on trial**; the new
firmware confirms itself after 60 s of normal running **and at least one Modbus reply** (a build
with a broken bus never becomes the known-good one). If it does not confirm (crash, hang, watchdog,
no bus) within 3 boots, the bootloader returns to the previous slot. While it runs on trial, a
further update is refused (state 0x8106), so the previous slot stays known-good. Without a valid
boot record (first start, or a power cut during a record write) and both slots valid, the
bootloader starts A on trial with B as the fallback. Nothing is copied or swapped, so a
power cut at any point leaves a bootable sensor. Details: `include/flash_layout.h`, `include/update.h`;
protocol for other bus masters: [docs/modbus_update_protocol.md](../docs/modbus_update_protocol.md).

```bash
../tools/mbflash.py /dev/ttyUSB485 .pio/build      # or the release download directory
```
- ~23 KB in 183 chunks of 128 bytes, about 1 min at 9600 baud; each chunk is one FC16 transaction
  that occupies the bus for ~176 ms, then the bus is free (the sensor programs the page after replying).
- Only one Modbus master on the bus: stop other pollers (bench: `systemctl --user stop andrtf3-modbus`)
  or let the bus master itself run the transfer between its own transactions.
- Input regs 74-77: running slot, boot record, trial attempts, bootloader version.

Tested on the bench (2026-10-24): A->B and B->A updates, corrupted image (rejected at verify),
firmware that hangs (3 watchdog resets, then automatic rollback), transfer interrupted + reset
(old slot keeps running), measurements uninterrupted during the transfer, update refused while
on trial (0x8106), confirmation only after the first replies. Identity set, kept by 0xDEF0 and a
reset; a label with a control character is rejected (exception 03).

## Flashing over SWD and restoring (do NOT run without an explicit go)
Original firmware backup: `../dumps/<date>-<label>/flash.bin` (made with `LABEL=<unit> scripts/dump.sh`,
not in git). Flash only after it prints `BACKUP OK`; keep each unit's dump in its own directory.
The data EEPROM was all zero; option bytes stay untouched (RDP level 0).

First installation (bootloader + firmware in slot A). Slot B (0x08010000-0x0801DFFF) must not hold
an image yet: it stays as the original left it (all 0x00 on the units seen so far, check the dump).
Then there is no trial and nothing to roll back to.
```bash
pio run
openocd -f ../scripts/common.cfg -c "init; reset halt; program .pio/build/boot/firmware.bin 0x08000000 verify; program .pio/build/slot_a/andrtf3_slot_a.bin 0x08002000 verify; reset run; exit"
```
Restore original:
```bash
openocd -f ../scripts/common.cfg -c "program ../dumps/<date>/flash.bin 0x08000000 verify reset exit"
```
Never change the option bytes / RDP level: level 1 would block reading, level 2 is permanent.
