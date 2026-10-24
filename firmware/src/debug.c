/*
 * debug.c - plain-text debug output on USART5 TX (PC12), 115200 8N1
 */
#include "debug.h"
#include "board.h"


void dbg_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_USART5EN;
    board_gpio_mode(DBG_TX_PORT, DBG_TX_PIN, GPIO_AF, 2, PULL_UP);

    USART5->CR1 = 0;
    USART5->BRR = SYSCLK_HZ / 115200U;
    USART5->CR1 = USART_CR1_TE | USART_CR1_UE;
}

static void putc_raw(char c)
{
    while (!(USART5->ISR & USART_ISR_TXE)) {
    }
    USART5->TDR = (uint8_t)c;
}

void dbg_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            putc_raw('\r');
        }
        putc_raw(*s++);
    }
    while (!(USART5->ISR & USART_ISR_TC)) {
    }
}

void dbg_put_int(int32_t v)
{
    char buf[12];
    int i = 0;
    uint32_t u = (v < 0) ? (uint32_t)(-v) : (uint32_t)v;

    do {
        buf[i++] = (char)('0' + (u % 10U));
        u /= 10U;
    } while (u);
    if (v < 0) {
        buf[i++] = '-';
    }
    while (i) {
        putc_raw(buf[--i]);
    }
}

void dbg_put_hex(uint32_t v, int digits)
{
    static const char hex[] = "0123456789ABCDEF";

    while (digits--) {
        putc_raw(hex[(v >> (digits * 4)) & 0xFU]);
    }
}
