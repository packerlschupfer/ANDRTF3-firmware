# Programming / debug connections (ANDRTF3/MD board)

Determined by continuity test on 2026-10-24 (TX = PC12 confirmed by the working debug log). Orientation: board back side, MCU on the left,
2x4 header to the right of the MCU, as in `photos/back-mcu-swd-header.jpg`.
Rows are counted from the top, `x` = not connected / not identified.

## 2x4 female header (next to the MCU) - SWD + debug UART

```
          left    right
 row 1    VDD     x
 row 2    x       SWDIO   (PA13, pin 46)
 row 3    TX      SWCLK   (PA14, pin 49)
 row 4    VSS     x
```

| Header | Signal | MCU | ST-LINK | Notes |
|---|---|---|---|---|
| row 1 left | VDD 3.3 V | 1/19/32/64 | 3.3V / VAPP | power the sensor from its own supply; do not feed 3.3 V from the probe while it is powered |
| row 2 right | SWDIO | PA13, 46 | SWDIO | |
| row 3 right | SWCLK | PA14, 49 | SWCLK | |
| row 4 left | VSS / GND | 18/31/47/63 | GND | |
| row 3 left | TX | PC12, 53 | - | debug UART, 3.3 V, 115200 8N1, to the RX input of a USB-serial adapter |

## 2x5 SMD pad array (no header fitted)

```
          left    right
 row 1    x       x
 row 2    x       x
 row 3    x       x
 row 4    x       x
 row 5    RST     x
```

| Pad | Signal | MCU | ST-LINK |
|---|---|---|---|
| row 5 left | NRST | pin 7 | RST |

NRST is not needed: a plain SWD connect works with the original firmware and the open firmware
(neither remaps PA13/PA14). It is only needed for connect-under-reset if the SWD pins
are ever reconfigured.

## Not routed out
- BOOT0 (pin 60) is not on either connector and is not needed for SWD.
- SWO is not available: PB3 is the RS485 driver-enable (USART1 DE).

## Minimal wiring
- **SWD (flash/dump):** SWDIO, SWCLK, GND, VDD (sense). Power the sensor from its own supply.
- **Debug log:** TX + GND to a 3.3 V USB-serial adapter, `tools/uartlog.py /dev/ttyUSBx 115200 log.txt`.
