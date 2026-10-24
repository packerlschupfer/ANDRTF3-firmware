# ANDRTF3 original firmware - analysis notes

Source: `dumps/20261024-030101/flash.bin` (46.7 KB used, RDP 0), Ghidra project `ghidra/andrtf3.gpr`
(raw import, ARM:LE:32:Cortex, base 0x08000000; `ghidra_scripts/l0_vectors.py` + `find_funcs.py` applied).
Live checks: sensor at Modbus address 4, 9600 8E1, via FTDI `/dev/ttyUSB485`.

Build: STM32Cube with **LL drivers** (LL_GPIO_Init = `FUN_08002334`, LL_USART_Init = `FUN_08002788`,
LL_SPI_Init = `FUN_08002718`), project file `../Src/main.c`. One firmware for the whole Andivi sensor
family (PT1000, SHT, iAQ CO2/TVOC, SM9541 pressure, Senseair S8 CO2, light).

## Interrupts in use
| Handler | Function | Purpose |
|---|---|---|
| USART1_IRQHandler | 0x08009044 | Modbus RX/TX, clears PE/FE |
| USART4_5_IRQHandler | 0x08009018 | USART5 RX byte -> `FUN_08005f8c` (external sensor module) |
| LPUART1_IRQHandler | 0x08009084 | LPUART1 RX byte -> `FUN_08004adc` (external sensor module) |
| EXTI4_15_IRQHandler | 0x08008fd0 | EXTI line 10 -> `FUN_08008870` |
| DMA1_Channel1_IRQHandler | 0x08008ff8 | ADC DMA complete -> `FUN_08005f80` |
| SysTick_Handler | 0x08008fc8 | tick |

## Pin map (from LL_GPIO_Init calls, `re/gpio_table.txt`)
| Pin | Config | Use (confirmed / likely) |
|---|---|---|
| PB6 / PB7 | AF0 | **USART1 TX / RX - Modbus** (confirmed) |
| PB3 | AF5, pull-down | **USART1_DE -> ISO3082 DE/RE** (hardware driver-enable, DEM=1, active high) |
| PA5 / PA6 | AF0 | **SPI1 SCK / MISO, RX-only master** -> MCP3551/3553 (PT1000 ADC), /64 prescaler |
| PA4 / PA3 | AF4 | **USART2 CK / RX, synchronous mode** -> 74HC165 chain (DIP switches), 38400 |
| PB13 / PB15 | AF0 | **SPI2 SCK / MOSI, TX-only master** -> likely LCD controller |
| PB12 | output, idle high | **SPI2 chip select (LCD)** - low around every SPI2 transfer |
| PB14 | output | **LCD command(0)/data(1)** select |
| PC7 | output | **LCD reset** (low->high pulse in LCD init `FUN_08005738`) |
| PC8 | output | set in LCD init (LCD power/backlight?) |
| PA15 | output, idle high | **MCP355x /CS** (low pulse = start conversion, low = read) |
| PA1 | output | **74HC165 /PL** (load) before USART2 clocks 2 bytes in (`FUN_08005b50`) |
| PC11 | output | set on each Modbus TX byte (TX activity) |
| PC13 | output | set on each Modbus RX byte, cleared by RTU timer (RX activity) |
| PB4 | output | toggled once per main-loop round (heartbeat) |
| PC6 | input, pull-up | read in `FUN_08008656/08008664` (button S3?) |
| PC12 / PD2 | AF2 / AF6 | USART5 TX / RX (sensor module port, e.g. Senseair S8) |
| PB10 / PB11 | AF7 (LPUART1) **or** AF6 (I2C2) | shared sensor connector: LPUART1 or I2C2 depending on variant |
| PA9 / PA10 | AF6 open-drain | I2C1 SCL / SDA (sensor bus) |
| PB0 / PB1 | analog | internal ADC (DMA ch1) - likely "Adj" trimmer and/or reference |
| PC6 | input, pull-up | TBD (button S3?) |
| PA0, PA8, PC9, PB5 | outputs | TBD (PB5/PC9 set at init) |

## Modbus
- Stack: FreeModbus-like with instance struct and per-instance callbacks; RTU + ASCII transports,
  CRC16 table at 0x0800B278/0x0800B378 (`FUN_08003f3c`).
- Function-code table at **0x0800B228**: 4, 3, 6, 16, 23, 22, 2, 1, 5, 15.
- Setup `FUN_08005ff4(param)`: low byte = DIP S1, high byte = address (DIP S2)
  - S1 bits 7:6 baud: 00=9600, 01=19200, 10=38400, 11=57600
  - S1 bits 4:3 parity (+2 &3 mapping): parity -> 9-bit word (8 data + parity); even = factory default
  - S1 bit 2 RTU/ASCII, S1 bit 1 stop bits (CR2 STOP), bit 0 not used here (TERM is hardware)
  - Verified at instruction level (2026-10-24): call site 0x080078d0 passes the 16-bit DIP word
    from `FUN_08005b50` (r0) straight into `FUN_08005ff4`; S1 is really read, nothing is hardcoded.
    The open firmware uses the identical decode, so a unit that answers under the original at
    address/baud/parity X answers under the open firmware at X too (bench: same switches, both addr 4).
- Callbacks registered: input regs -> `FUN_080058d4`, holding -> `FUN_08005a50`,
  discrete inputs -> `FUN_08005ad0`, no coil callback.

### Input registers (FC04), addresses 0-64 (0-based)
Special-cased via jump table at 0x0800B68C; all others read the word array at 0x20000404[reg].

| Reg | Source | Live value (2026-10-24) | Meaning (so far) |
|---|---|---|---|
| 1 | constant set at boot | 1 | device type? |
| 2 | constant set at boot | 7 | firmware version? |
| 10 | 0x200004F4 | 0 | TBD |
| 20-24 | 0x200004D8 +0..+8 | 0 | TBD (other sensor variant values) |
| 30, 31 | 0x200004E8 +0/+2 | 0 | TBD |
| 40 | 0x200004D4 (byte-swapped) | 0 | SM9541 pressure sensor (I2C) value |
| 41 | `(EEPROM[0x08080000] + 6553) * 2000 / 13107 - 999` | 0 | SM9541-style transfer function on data-EEPROM word 0 |
| **50** | **0x200003D8** | 232 | **PT1000 temperature, 0.1 degC** |
| 60, 61 | 0x200004F0 +0/+2 | 0 | TBD |
| 64 | 0x20000484 | 1 | SM9541 driver status (1 = not present/initial) |
Reads beyond 64 -> exception 2 (illegal data address).

### Holding registers (FC03/06/16/23): 512-515 (0x200-0x203)
Backed by state struct 0x200003D8 +4/+6/+8/+10. 512-514: never read by the firmware (unused).
**515 = debug flag (byte +10)**: non-zero enables plain-text debug output on USART5 TX (PC12),
e.g. "Widerstandswert: <mOhm> Ohm", "ADC read timeout occured!". Not written yet.
USART5: **115200 8N1**, TX only on PC12 (3.3 V logic).

### Discrete inputs (FC02): 256-320 (0x100-0x140)
Reads the same array as the input registers (0x20000404[reg-0x100]) but copies whole 16-bit words
into the bit buffer - looks like a firmware bug; harmless for reads.

## Main loop and the 0x0000 value
- Setup `FUN_08007824`: reg[1]=1, reg[2]=7, peripheral init, read DIP switches, Modbus init, probe
  every optional sensor, then loop: PT1000 (`FUN_08005ba0`), LCD init/update, button.
- Measurement loop `FUN_08007a00`: each sensor's "valid" flag goes into status byte 0x200003F8
  (bit0 reg10-sensor, bit1 SHT regs 20-24, bit2 regs 30/31, bit3 SM9541 reg 40, **bit4 PT1000**,
  bit5 regs 60/61). Live: 0x10 (PT1000 only). Display flag +0x24 is set to 1 unconditionally at boot - it does NOT
  indicate a fitted LCD (this unit is the minimal variant: no display, no button, no add-on sensors).
- **PT1000 measurement `FUN_08005ba0(&reg50)`** writes the result straight into register 50:
  1. PA15 low-pulse starts an MCP355x conversion, wait 70 ms, poll RDY (PA6) up to 1000x.
     Timeout -> "ADC read timeout occured!", returns invalid, **register 50 keeps its old value**.
  2. Read 3 bytes (SPI1). Overflow bits -> fixed fallback.
  3. `V = code*2.5/2^21`, `R[mOhm] = V*1e7/(2.5-V)` -> divider with **10 kOhm reference**, 2.5 V ref.
  4. **Plausibility 500..3000 Ohm**; outside -> **register 50 := 0** and status bit4 cleared.
  5. Callendar-Van Dusen (A=3.9083e-3, B=-5.775e-7, R0=1000), *10, truncated to int.
- So a Modbus 0x0000 from this sensor means the last conversion gave an implausible resistance
  (bad/misaligned ADC read, open/short PT1000), visible until the next measurement round.
  A genuine 0.0 degC also reads 0 - indistinguishable on Modbus.

## Open questions / next steps
- LCD controller type (SPI2 command set in `FUN_08005738`/`FUN_080063c0`)
- Remaining outputs PA0, PA8, PB5, PC9; registers 10, 20-24, 30/31, 60/61 (other variants, 0 here)
- Optional: enable debug (holding 515 = 1) with a serial adapter on PC12 to watch raw resistance
- EXTI line 10 source pin and purpose; debug output channel for the "Widerstandswert" strings
