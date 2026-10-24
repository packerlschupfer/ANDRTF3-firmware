/*
 * board.c - clocks to GPIO ports and initial pin states
 *
 * Initial output states follow the original firmware so nothing on the board
 * sees a different level than before.
 */
#include "board.h"

void board_gpio_mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode, uint32_t af, uint32_t pull)
{
    uint32_t shift2 = pin * 2U;

    port->MODER = (port->MODER & ~(3UL << shift2)) | (mode << shift2);
    port->PUPDR = (port->PUPDR & ~(3UL << shift2)) | (pull << shift2);
    if (mode == GPIO_AF) {
        volatile uint32_t *afr = &port->AFR[pin >> 3];
        uint32_t shift4 = (pin & 7U) * 4U;
        *afr = (*afr & ~(0xFUL << shift4)) | (af << shift4);
    }
}

static void out(GPIO_TypeDef *port, uint32_t pin, int level)
{
    if (level) {
        pin_set(port, pin);
    } else {
        pin_clr(port, pin);
    }
    board_gpio_mode(port, pin, GPIO_OUT, 0, PULL_NONE);
}

void board_init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOCEN | RCC_IOPENR_GPIODEN;

    /* PT1000 ADC: CS and SCK idle high (SPI mode 3), SDO input */
    out(ADC_CS_PORT, ADC_CS_PIN, 1);
    out(ADC_SCK_PORT, ADC_SCK_PIN, 1);
    board_gpio_mode(ADC_SDO_PORT, ADC_SDO_PIN, GPIO_IN, 0, PULL_UP);

    /* DIP shift register load line idle high */
    out(DIP_PL_PORT, DIP_PL_PIN, 1);

    /* Status outputs */
    out(LED_HB_PORT, LED_HB_PIN, 1);
    out(ACT_TX_PORT, ACT_TX_PIN, 0);
    out(ACT_RX_PORT, ACT_RX_PIN, 0);

    /* Unknown function - same levels as the original firmware */
    out(GPIOC, UNK_PC9_HIGH, 1);
    out(GPIOB, UNK_PB5_LOW, 0);
    out(GPIOA, UNK_PA8_LOW, 0);

    /* LCD lines inactive */
    out(GPIOB, LCD_CS_PIN, 1);
    out(GPIOB, LCD_DC_PIN, 0);
    out(GPIOC, LCD_RST_PIN, 0);
    out(GPIOC, LCD_PWR_PIN, 0);
}
