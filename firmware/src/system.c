/*
 * system.c - clock (HSI16), 1 ms SysTick, delays, independent watchdog
 */
#include "system.h"
#include "board.h"

static volatile uint32_t s_ticks;
static uint8_t s_reset_cause;
static int s_reset_latched;

void SysTick_Handler(void)
{
    s_ticks++;
}

void system_clock_init(void)
{
    /* Voltage range 1 (1.8 V) and 1 wait state: valid for 16 MHz in any range */
    RCC->APB1ENR |= RCC_APB1ENR_PWREN;
    PWR->CR = (PWR->CR & ~PWR_CR_VOS) | PWR_CR_VOS_0;
    while (PWR->CSR & PWR_CSR_VOSF) {
    }
    FLASH->ACR |= FLASH_ACR_LATENCY;

    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) {
    }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {
    }
    SystemCoreClock = SYSCLK_HZ;

    SysTick_Config(SYSCLK_HZ / 1000U);
}

uint32_t millis(void)
{
    return s_ticks;
}

void delay_ms(uint32_t ms)
{
    uint32_t start = s_ticks;
    while ((s_ticks - start) < ms) {
    }
}

void delay_cycles(uint32_t n)
{
    while (n--) {
        __NOP();
    }
}

uint8_t system_reset_cause(void)
{
    if (!s_reset_latched) {
        s_reset_cause = (uint8_t)(RCC->CSR >> 24);
        RCC->CSR |= RCC_CSR_RMVF;
        s_reset_latched = 1;
    }
    return s_reset_cause;
}

/* Most specific cause first; PIN alone means the NRST pin was pulled externally */
const char *system_reset_cause_name(uint8_t c)
{
    if (c & 0x20U) return "IWDG";
    if (c & 0x40U) return "WWDG";
    if (c & 0x80U) return "LPWR";
    if (c & 0x08U) return "POR";
    if (c & 0x10U) return "SOFT";
    if (c & 0x02U) return "OBL";
    if (c & 0x01U) return "FW";
    if (c & 0x04U) return "PIN";
    return "none";
}

void watchdog_start(void)
{
    /* LSI ~37 kHz, /64 -> ~578 Hz, reload 1200 -> ~2 s */
    IWDG->KR = 0xCCCC;
    IWDG->KR = 0x5555;
    IWDG->PR = 4;
    IWDG->RLR = 1200;
    while (IWDG->SR) {
    }
    IWDG->KR = 0xAAAA;
}

void watchdog_kick(void)
{
    IWDG->KR = 0xAAAA;
}

/* UID registers of the STM32L0: bits 31:0, 63:32 and 95:64 (RM0377 "Unique device ID registers") */
#define UID_31_0    (*(const volatile uint32_t *)0x1FF80050UL)
#define UID_63_32   (*(const volatile uint32_t *)0x1FF80054UL)
#define UID_95_64   (*(const volatile uint32_t *)0x1FF80064UL)

uint16_t system_uid_word(uint8_t i)
{
    uint32_t w = (i < 2U) ? UID_95_64 : (i < 4U) ? UID_63_32 : UID_31_0;

    return (i & 1U) ? (uint16_t)w : (uint16_t)(w >> 16);
}
