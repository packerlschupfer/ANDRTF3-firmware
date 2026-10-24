/*
 * board.h - ANDRTF3/MD controller pin map (STM32L071RBT6, LQFP64)
 *
 * Derived from the original Andivi firmware (see ../docs/firmware_analysis.md).
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32l0xx.h"

#define SYSCLK_HZ           16000000UL   /* HSI16, no crystal fitted */

/* RS485 (TI ISO3082) on USART1 - DE driven by USART1 hardware */
#define RS485_PORT          GPIOB
#define RS485_TX_PIN        6            /* PB6  AF0 USART1_TX */
#define RS485_RX_PIN        7            /* PB7  AF0 USART1_RX */
#define RS485_DE_PIN        3            /* PB3  AF5 USART1_DE */

/* PT1000 ADC (Microchip MCP3551/3553), bit-banged SPI mode 3 */
#define ADC_CS_PORT         GPIOA
#define ADC_CS_PIN          15           /* PA15 /CS, idle high */
#define ADC_SCK_PORT        GPIOA
#define ADC_SCK_PIN         5            /* PA5  SCK, idle high */
#define ADC_SDO_PORT        GPIOA
#define ADC_SDO_PIN         6            /* PA6  SDO/RDY */

/* DIP switches S1/S2 via 2x 74HC165, clocked by USART2 in synchronous mode */
#define DIP_PL_PORT         GPIOA
#define DIP_PL_PIN          1            /* PA1  /PL (parallel load) */
#define DIP_RX_PIN          3            /* PA3  AF4 USART2_RX  <- QH */
#define DIP_CK_PIN          4            /* PA4  AF4 USART2_CK  -> CP */

/* Debug output on USART5 TX */
#define DBG_TX_PORT         GPIOC
#define DBG_TX_PIN          12           /* PC12 AF2 USART5_TX, 115200 8N1 */

/* Status / misc outputs */
#define LED_HB_PORT         GPIOB
#define LED_HB_PIN          4            /* PB4  heartbeat (toggled each loop) */
#define ACT_TX_PORT         GPIOC
#define ACT_TX_PIN          11           /* PC11 Modbus TX activity */
#define ACT_RX_PORT         GPIOC
#define ACT_RX_PIN          13           /* PC13 Modbus RX activity */

/* Present on the board, purpose unknown - kept in the original firmware's states */
#define UNK_PC9_HIGH        9            /* PC9  set high at init */
#define UNK_PB5_LOW         5            /* PB5  driven low */
#define UNK_PA8_LOW         8            /* PA8  driven low */

/* LCD lines (not fitted on the minimal variant) - held in inactive states */
#define LCD_CS_PIN          12           /* PB12 high */
#define LCD_DC_PIN          14           /* PB14 low */
#define LCD_RST_PIN         7            /* PC7  low */
#define LCD_PWR_PIN         8            /* PC8  low */

static inline void pin_set(GPIO_TypeDef *port, uint32_t pin) { port->BSRR = 1UL << pin; }
static inline void pin_clr(GPIO_TypeDef *port, uint32_t pin) { port->BRR = 1UL << pin; }
static inline uint32_t pin_read(GPIO_TypeDef *port, uint32_t pin) { return (port->IDR >> pin) & 1UL; }

void board_init(void);
void board_gpio_mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode, uint32_t af, uint32_t pull);

/* MODER values */
#define GPIO_IN             0U
#define GPIO_OUT            1U
#define GPIO_AF             2U
#define GPIO_ANALOG         3U
/* PUPDR values */
#define PULL_NONE           0U
#define PULL_UP             1U
#define PULL_DOWN           2U

#endif /* BOARD_H */
