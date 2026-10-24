#ifndef REGS_H
#define REGS_H

#include <stdint.h>

/*
 * Modbus register map. Addresses are 0-based (protocol addresses).
 *
 * Input registers (FC04), 0..83 (original: 0..64; others -> exception 02):
 *   1      device type                  1 (as original)
 *   2      firmware version             FW_VERSION (original: 7)
 *   50     temperature, 0.1 degC        filtered (holding 517), offset applied.
 *                                       Invalid: 0 (as original, also on ADC timeouts)
 *                                       unless holding 516 = 1;
 *                                       with the filter on, up to 2 consecutive bad
 *                                       readings are bridged with the last value.
 *   51     status bits                  see REG_STATUS_*
 *   52     PT1000 resistance, 0.1 Ohm   last conversion, 0 if not measurable
 *   53     age of last good reading, s  0xFFFF = never / saturated
 *   54/55  good / failed readings       counters, wrap
 *   56     last failure reason          1 timeout, 2 overflow, 3 range
 *   57     temperature, 0.01 degC       last good reading, unfiltered, offset applied
 *   58/59  uptime, s                    high / low word
 *   60     ADC timeouts                 counter
 *   61     resistance out of range      counter
 *   62     ADC overflows                counter
 *   63     settings flags               bit0 loaded from EEPROM, bit1 unsaved changes
 *   64     reset cause                  RCC_CSR 31..24 at boot, see system.h (0x0C = power-on)
 *   65     bus frames received          any address, good or bad
 *   66     frames handled               valid CRC, for this address / broadcast
 *   67     CRC errors
 *   68     bad length / UART-poisoned frames
 *   69-72  UART framing / parity / noise / overrun errors
 *   73     exception responses sent
 *   65..73 are 16-bit views of 32-bit counters (wrap)
 *   74     running slot                 0 = A, 1 = B
 *   75     boot record                  active slot | trial slot << 8 (0xFF = none); 0xFFFF = no record
 *   76     trial boot attempts
 *   77     bootloader version           0 = no bootloader
 *   78-83  factory unique ID (96 bit)   UID[95:80] .. UID[15:0], read only, different on every chip
 *
 * Holding registers (FC03/06/16), 512..531 - settings are RAM until command 0x5AFE:
 *   512-514  reserved (unused in the original too)
 *   515      debug trace on USART5 (PC12)             default 1
 *   516      invalid policy: 0 report 0, 1 keep last  default 0 (original behaviour)
 *   517      filter: 0 off, 1 median-of-3 + hold      default 1
 *   518      measurement interval, ms (100..60000)    default 1000
 *   520/521  reference resistor, mOhm (high, low)     default 10 000 000; write 520 then 521
 *   522      temperature offset, 0.01 degC (signed)   default 0
 *   523      calibration resistor, 0.1 Ohm            default 10000 (1000.0 Ohm)
 *   530      command (see calib.h)                    reads the last command
 *   531      calibration state (read only)
 *   532      asset number (user ID)                   default 0; saved with 0x5AFE, kept by 0xDEF0
 *   533-539  label, 14 ASCII chars                    2 per register, high byte first, NUL padded
 *   540      update command (see update.h)            reads the update state
 *   541      update state (read only)
 *   542/543  update bytes received (high, low)
 *   544-609  update data window (FC16 only, 66 registers: offset + 128 bytes)
 */
#define FW_VERSION              0x8001U     /* bit 15 = open firmware, 0x001 = v0.1 */

#define REG_INPUT_LAST          83U
#define REG_TEMP                50U

#define REG_HOLD_FIRST          512U
#define REG_HOLD_LAST           609U
#define REG_UPD_DATA            544U
#define REG_ASSET               532U
#define REG_LABEL_FIRST         533U

/*
 * bit0 means "the value in reg 50 is trustworthy": set after a good reading AND while the filter
 * bridges 1-2 failed readings (then bit5 is set too). Cleared when reg 50 is 0 or, with holding
 * 516 = 1, a stale value kept after 3+ failures. Bits 1-3 describe the most recent RAW reading
 * (cleared by the next good one); counters 55 and 60-62 record every failure, held or not.
 */
#define REG_STATUS_VALID        0x0001U     /* reg 50 trustworthy (fresh or bridged) */
#define REG_STATUS_TIMEOUT      0x0002U     /* last raw reading: ADC not ready */
#define REG_STATUS_OVERFLOW     0x0004U     /* last raw reading: ADC overflow */
#define REG_STATUS_RANGE        0x0008U     /* last raw reading: R outside 500..3000 Ohm */
#define REG_STATUS_NEVER        0x0010U     /* no good reading since boot */
#define REG_STATUS_HELD         0x0020U     /* reg 50 currently bridged by the filter */

void regs_update_measurement(int ok, int status, int32_t temp_cdeg, uint32_t r_milliohm, uint32_t now_ms);

/* Counters for the periodic status line */
void regs_counters(uint32_t *good, uint32_t *fail, uint32_t *timeouts, uint32_t *range, uint32_t *overflow);

/* Return 0 on success, Modbus exception code otherwise */
int regs_read_input(uint16_t addr, uint16_t *val, uint32_t now_ms);
int regs_read_holding(uint16_t addr, uint16_t *val);
int regs_write_holding(uint16_t addr, uint16_t val);

#endif /* REGS_H */
