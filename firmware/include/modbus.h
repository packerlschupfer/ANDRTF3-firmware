#ifndef MODBUS_H
#define MODBUS_H

#include <stdint.h>

/*
 * Modbus RTU slave on USART1 (PB6/PB7), RS485 driver enable by USART1 hardware (PB3).
 * cfg is DIP S1 as interpreted by the original firmware:
 *   bits 7:6  baud      00=9600 01=19200 10=38400 11=57600
 *   bits 4:3  parity    00=even (factory) 01=odd 10/11=none
 *   bit  2    mode      0=RTU 1=ASCII (ASCII not supported - stays RTU, logged)
 *   bit  1    stop bits 0=1 1=2
 */
/* Bus diagnostics, all counters wrap. frames = every received frame (any address). */
typedef struct {
    uint32_t frames;        /* frames received (any address, good or bad) */
    uint32_t handled;       /* valid frames for this address (or broadcast) */
    uint32_t crc;           /* CRC mismatch */
    uint32_t bad_length;    /* < 4 bytes, too long, or poisoned by a UART error */
    uint32_t framing;       /* USART framing errors */
    uint32_t parity;        /* USART parity errors */
    uint32_t noise;         /* USART noise detected */
    uint32_t overrun;       /* USART overrun (normally 0: overrun detection is disabled) */
    uint32_t exceptions;    /* exception responses sent */
    uint32_t replies;       /* responses sent (incl. exceptions); not exposed, used to confirm updates */
} modbus_stats_t;

void modbus_init(uint8_t address, uint8_t cfg);
void modbus_poll(uint32_t now_ms);

/* Human-readable summary of the decoded configuration for the boot banner */
uint32_t modbus_baud(void);
char modbus_parity(void);
uint8_t modbus_stopbits(void);

void modbus_get_stats(modbus_stats_t *st);

#endif /* MODBUS_H */
