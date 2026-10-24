/*
 * nvm.c - data EEPROM word writes, flash page erase + program, CRC32 (shared with the bootloader)
 */
#include "flash_layout.h"
#include "board.h"

#define SR_ERRORS   (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR | FLASH_SR_OPTVERR | \
                     FLASH_SR_RDERR | FLASH_SR_NOTZEROERR | FLASH_SR_FWWERR)

uint32_t crc32_calc(uint32_t crc, const uint8_t *p, uint32_t n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 1U) ? (crc >> 1) ^ 0xEDB88320UL : crc >> 1;
        }
    }
    return ~crc;
}

static void wait_ready(void)
{
    while (FLASH->SR & FLASH_SR_BSY) {
    }
}

static int check_errors(void)
{
    uint32_t sr = FLASH->SR & SR_ERRORS;

    if (sr) {
        FLASH->SR = sr;
        return 1;
    }
    return 0;
}

static void unlock(int program_memory)
{
    wait_ready();
    if (FLASH->PECR & FLASH_PECR_PELOCK) {
        FLASH->PEKEYR = 0x89ABCDEFUL;
        FLASH->PEKEYR = 0x02030405UL;
    }
    if (program_memory && (FLASH->PECR & FLASH_PECR_PRGLOCK)) {
        FLASH->PRGKEYR = 0x8C9DAEBFUL;
        FLASH->PRGKEYR = 0x13141516UL;
    }
    FLASH->SR = SR_ERRORS;
}

static void lock(void)
{
    wait_ready();
    FLASH->PECR |= FLASH_PECR_PRGLOCK;
    FLASH->PECR |= FLASH_PECR_PELOCK;
}

int nvm_eeprom_write(uint32_t addr, const uint32_t *w, uint32_t nwords)
{
    int err = 0;

    unlock(0);
    for (uint32_t i = 0; i < nwords && !err; i++) {
        volatile uint32_t *dst = (volatile uint32_t *)(addr + 4U * i);
        if (*dst == w[i]) {
            continue;                       /* spare the cell */
        }
        *dst = w[i];
        wait_ready();
        err = check_errors() || *dst != w[i];
    }
    lock();
    return !err;
}

/* Must not be called for the bank the caller executes from (no RAM functions here). */
int nvm_flash_write_page(uint32_t addr, const uint32_t *w)
{
    int err;

    if (addr & (FLASH_PAGE - 1U)) {
        return 0;
    }
    unlock(1);
    FLASH->PECR |= FLASH_PECR_ERASE | FLASH_PECR_PROG;
    *(volatile uint32_t *)addr = 0;
    wait_ready();
    FLASH->PECR &= ~(FLASH_PECR_ERASE | FLASH_PECR_PROG);
    err = check_errors();
    for (uint32_t i = 0; i < FLASH_PAGE / 4U && !err; i++) {
        volatile uint32_t *dst = (volatile uint32_t *)(addr + 4U * i);
        if (w[i] != 0U) {                   /* erased flash already reads 0 */
            *dst = w[i];
            wait_ready();
            err = check_errors();
        }
        if (!err && *dst != w[i]) {
            err = 1;
        }
    }
    lock();
    return !err;
}
