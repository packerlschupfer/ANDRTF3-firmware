#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

void system_clock_init(void);
uint32_t millis(void);
void delay_ms(uint32_t ms);
void delay_cycles(uint32_t n);

/*
 * Reset cause: RCC_CSR flags 31..24 as one byte, latched once at boot and then cleared in hardware.
 * bit7 LPWR, bit6 WWDG, bit5 IWDG, bit4 SFT (incl. SWD reset), bit3 POR/PDR, bit2 PIN (NRST,
 * set on every reset that drives the pin), bit1 OBL, bit0 FW.
 */
uint8_t system_reset_cause(void);
const char *system_reset_cause_name(uint8_t cause);
/* Factory unique ID (96 bit) as 6 words, i = 0 -> UID[95:80] .. i = 5 -> UID[15:0] */
uint16_t system_uid_word(uint8_t i);

void watchdog_start(void);
void watchdog_kick(void);

#endif /* SYSTEM_H */
