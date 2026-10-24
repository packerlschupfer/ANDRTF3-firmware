# ANDRTF3 open firmware

Open replacement firmware, plus backup/analysis tooling, for the **Andivi ANDRTF3/MD** RS485 Modbus RTU
room-temperature sensor (PT1000). The board's controller is an STM32L071RBT6.

> Not affiliated with Andivi. Flashing your own firmware voids the warranty. **Back up the original
> firmware first** with `scripts/dump.sh`; it is not part of this repository.

- **Firmware** (`firmware/`): register-level C, built with PlatformIO. It provides Modbus RTU that is
  compatible with the original (input register 50 = temperature in 0.1 °C, DIP-switch address and
  serial settings). It adds diagnostics registers, a debug UART log, a median filter, EEPROM
  settings, a reference-resistor calibration that works without disconnecting the PT1000, and a
  bootloader with A/B slots for safe firmware updates over RS485 (automatic rollback).
  See [firmware/README.md](firmware/README.md).
- **Backup and analysis**: SWD dump/probe scripts (OpenOCD), Ghidra helper scripts and
  [notes on the original firmware](docs/firmware_analysis.md).
- **Hardware**: [programming/debug connector pinout](docs/connectors.md) and a printable
  [MCU pinout PDF](docs/ANDRTF3_STM32L071_pinout.pdf).

Tested on the minimal ANDRTF3/MD variant: PT1000 only, no LCD, no button, no add-on sensors.

## Compared to the original firmware
A bus master that only reads input register 50 sees no difference, except that faults disappear.
Same DIP-switch decoding (address, baud, parity, stop bits) and the same 0.1 °C format in reg 50.

| | Original (Andivi, version 7) | Open firmware |
|---|---|---|
| ADC read | SPI1 RX-only master | bit-banged SPI (same timeout, overflow and range checks) |
| Sporadic 0.0 °C | one bad conversion (R outside 500–3000 Ω) writes **0** to reg 50; seen 1–2×/day on the bench, 12 in 8198 reads (11.4 h) on the installed unit | 0 failed readings in 41 039 (11.6 h) on the bench unit; short glitches (max. 2 readings) are bridged by the filter |
| Validity | 0 is both "fault" and a real 0.0 °C | status register 51: bit 0 = reg 50 trustworthy, plus the reason for the last failure |
| Measurement | ~2.2 s cycle, Callendar-Van Dusen without the C term, truncated to 0.1 °C | 1 s (configurable), median of 3, CVD incl. the C term below 0 °C, rounded; 0.01 °C in reg 57 |
| Calibration | none, nominal 10 kΩ reference | reference-resistor calibration with a 1000 Ω resistor, PT1000 stays connected; offset; settings in EEPROM (CRC32) |
| Identity | – | factory unique ID (regs 78–83), user asset number and label (holding 532–539, EEPROM) |
| Diagnostics | none | regs 52–77: resistance, age of the last good reading, good/failed/timeout/range/overflow counters, uptime, reset cause, bus error counters (CRC, framing, parity, noise, overrun, exceptions), boot slot |
| Debug UART (PC12) | only when holding 515 is set, a few plain-text messages | boot banner (address, serial settings, reset cause), every measurement, `STAT`/`BUS` summaries every 60 s, `!!` marks problems |
| Dead ADC (timeout) | reg 50 keeps the last value: looks like a valid, frozen temperature | reg 50 = 0 after the bridge, status bit 0 clear |
| Robustness | – | independent watchdog (~2 s), reset cause in reg 64 |
| Updates | SWD only | bootloader with A/B slots: update over RS485 while it keeps measuring, CRC-checked, trial boot (confirmed only after 60 s with Modbus replies), automatic rollback after 3 failed boots |
| Not supported | – | Modbus ASCII (DIP falls back to RTU with a warning), FC02/FC23, LCD, button and the add-on sensors of other variants (regs 10–41, 60/61) |

The ADC read is the most likely cause of the original's 0.0 °C readings (misaligned SPI reads); the
evidence is the bench comparison above, not a proof. Details: [docs/firmware_analysis.md](docs/firmware_analysis.md).
For ESP32 bus masters, [ESP32-ANDRTF3](https://github.com/packerlschupfer/ESP32-ANDRTF3) detects
the firmware (input reg 2) and can use the status and diagnostics registers.

## Hardware
- STM32L071RBT6 (LQFP64, Cortex-M0+, 128 KB flash, 20 KB RAM, 6 KB data EEPROM), 16 MHz HSI
- TI ISO3082 isolated RS485 on USART1 (PB6/PB7), driver enable PB3 (hardware DE)
- Microchip MCP3551/3553 22-bit ADC for the PT1000 against a 10 kΩ reference (SPI1, CS PA15)
- 2x 74HC165 for the DIP switches: S2 = Modbus address, S1 = baud/parity/stop bits
- B8S bridge rectifier and a buck converter to 3.3 V

Photos (`photos/`): [front with DIP switches](photos/front-dip-switches.jpg),
[back with MCU and SWD header](photos/back-mcu-swd-header.jpg), [ISO3082](photos/iso3082-rs485.jpg),
[74HC165](photos/74hc165-dip-shift-registers.jpg), [MCP3551](photos/mcp3551-pt1000-adc.jpg),
[MCU marking](photos/stm32l071rbt6-marking.jpg).

## Connections
2x4 header next to the MCU (details and orientation in [docs/connectors.md](docs/connectors.md)):

```
          left    right
 row 1    VDD     x
 row 2    x       SWDIO
 row 3    TX      SWCLK
 row 4    VSS     x
```
NRST is on the unpopulated 2x5 pad array (row 5 left). It is not needed for SWD.
The debug UART (TX = PC12, USART5) runs at 115200 8N1, 3.3 V.

## Quick start
Requirements: an ST-LINK (V2 or V3), OpenOCD ≥ 0.12, PlatformIO, and `mbpoll` for Modbus tests.

```bash
scripts/probe.sh                 # IDCODE, RDP level, option bytes (read only)
LABEL=unit1 scripts/dump.sh      # back up flash/EEPROM/option bytes to dumps/<date>-unit1/; flash only after BACKUP OK

cd firmware && pio run           # build bootloader + firmware for slot A and B
openocd -f ../scripts/common.cfg -c "init; reset halt; program .pio/build/boot/firmware.bin 0x08000000 verify; program .pio/build/slot_a/andrtf3_slot_a.bin 0x08002000 verify; reset run; exit"

# later updates over RS485 (A/B slots, automatic rollback)
../tools/mbflash.py /dev/ttyUSB485 .pio/build

# restore the original
openocd -f ../scripts/common.cfg -c "program ../dumps/<date>/flash.bin 0x08000000 verify reset exit"
```
Never change the option bytes. RDP level 1 blocks reading the flash, and level 2 is permanent.

## Tools
| | |
|---|---|
| `scripts/common.cfg` | OpenOCD config (ST-LINK, hla_swd, stm32l0, 480 kHz) |
| `scripts/probe.sh`, `scripts/dump.sh` | read-only probe and verified backup (exact sizes, two identical reads, watchdog frozen while halted) |
| `tools/uartlog.py DEV BAUD OUT [SECONDS]` | timestamped debug-UART logger (OUT may contain `%Y%m%d`, SECONDS 0 = forever) |
| `tools/modbus_monitor.py DEV OUT [--addr 4]` | polls input regs 50..73 every 10 s, flags `!!` on no response, CRC, T=0, failures, bus errors, reboot |
| `tools/systemd/install.sh UART_DEV MODBUS_DEV [ADDR]` | installs both as systemd user services, daily logs in `logs/` |
| `tools/mbflash.py DEV IMAGES [--addr 4]` | firmware update over Modbus into the inactive slot |
| `tools/mbident.py DEV [--asset N] [--label TEXT]` | show or set the sensor's identity |
| `tools/post_image.py` | build step: image size + CRC32 for the bootloader |
| `tools/calibrate.sh [addr] [dev]` | guided reference-resistor calibration over Modbus |
| `tools/pinout_pdf.py` | regenerates the pinout PDF |
| `ghidra_scripts/`, `tools/gpio_*.py`, `tools/resolve_dat.py` | analysis helpers for a raw dump (Ghidra 11, base 0x08000000) |

## Original firmware facts (bench unit, 2026-10-24)
- IDCODE 0x20086447, RDP level 0, no write protection; erased flash on the L0 reads 0x00
- Andivi multi-sensor platform build (STM32Cube LL), 46.7 KB; firmware version (input reg 2) = 7
- The data EEPROM was all zero. There is no PT1000 calibration; the reference is a nominal 10 kΩ.
- If the resistance is outside 500–3000 Ω, the original writes **0** to reg 50. That explains the
  sporadic 0.0 °C readings. The open firmware counts and logs every such event and can bridge
  short glitches.

## License
GPL-3.0 for the code and documentation in this repository. The original Andivi firmware is
not included and is not covered by this license.

## Long-term bench monitoring
```bash
tools/systemd/install.sh /dev/serial/by-id/<usb-serial on PC12> /dev/serial/by-id/<RS485 adapter> 4
grep '!!' logs/*.log                      # anything that needs attention
loginctl enable-linger "$USER"            # optional: keep running while logged out
```
