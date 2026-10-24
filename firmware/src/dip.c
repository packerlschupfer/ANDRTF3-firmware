/*
 * dip.c - DIP switches S1/S2 via 2x 74HC165, clocked by USART2 in synchronous mode
 *
 * Mirrors the original firmware (FUN_080091f0 init, FUN_08005b50 read):
 * 38400 baud, OVER8, 9-bit word, clock output enabled with CPOL high,
 * /PL pulse on PA1, then two dummy transmissions clock the chain in.
 */
#include "dip.h"
#include "board.h"
#include "system.h"

static void dip_usart_init(void)
{
    uint32_t div;

    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    board_gpio_mode(GPIOA, DIP_RX_PIN, GPIO_AF, 4, PULL_UP);
    board_gpio_mode(GPIOA, DIP_CK_PIN, GPIO_AF, 4, PULL_NONE);

    USART2->CR1 = 0;
    div = (2U * SYSCLK_HZ) / 38400U;                       /* OVER8 */
    USART2->BRR = (div & 0xFFF0U) | ((div & 0x000FU) >> 1);
    USART2->CR2 = USART_CR2_CLKEN | USART_CR2_CPOL;
    USART2->CR3 = 0;
    USART2->CR1 = USART_CR1_OVER8 | USART_CR1_M0 | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static uint8_t dip_clock_byte(void)
{
    uint32_t t0 = millis();

    USART2->TDR = 0xFF;
    while (!(USART2->ISR & USART_ISR_TC)) {
        if ((millis() - t0) > 10U) {
            break;
        }
    }
    USART2->ICR = USART_ICR_TCCF;
    return (uint8_t)(USART2->RDR & 0xFFU);
}

uint16_t dip_read(void)
{
    uint8_t first, second;

    dip_usart_init();

    pin_clr(DIP_PL_PORT, DIP_PL_PIN);   /* load parallel inputs */
    delay_ms(1);
    pin_set(DIP_PL_PORT, DIP_PL_PIN);

    USART2->ICR = USART_ICR_TCCF;
    first = dip_clock_byte();
    second = dip_clock_byte();

    USART2->CR1 = 0;                    /* not needed after boot */
    return (uint16_t)((first << 8) | second);
}
