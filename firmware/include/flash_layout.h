#ifndef FLASH_LAYOUT_H
#define FLASH_LAYOUT_H

#include <stdint.h>

/*
 * Flash layout (STM32L071RB, 128 KB in two 64 KB banks, 128-byte pages, erased = 0x00):
 *
 *   0x08000000  8 KB  bootloader (written once over SWD, never updated over Modbus)
 *   0x08002000 56 KB  slot A  (bank 1)
 *   0x08010000 56 KB  slot B  (bank 2)
 *   0x0801E000  8 KB  unused
 *
 * The firmware is linked twice (env slot_a / slot_b). The running firmware receives an update
 * into the other slot - always the other flash bank, so it keeps executing while it programs -
 * and the bootloader then starts the new slot on trial. Nothing is ever copied or swapped.
 *
 * Every image carries an app_header_t at offset 0xC0 (right after the 48-entry vector table).
 * tools/post_image.py fills in `size` and appends a CRC32 (zlib) over [base, base + size).
 */
#define FLASH_PAGE              128U
#define BOOT_BASE               0x08000000UL
#define BOOT_SIZE               0x2000UL
#define SLOT_A_BASE             0x08002000UL
#define SLOT_B_BASE             0x08010000UL
#define SLOT_SIZE               0xE000UL
#define HEADER_OFFSET           0xC0U

#define SLOT_A                  0U
#define SLOT_B                  1U
#define SLOT_NONE               0xFFU

#define IMG_MAGIC_APP           0x474D4941UL    /* "AIMG" */
#define IMG_MAGIC_BOOT          0x444C4241UL    /* "ABLD" */
#define IMG_HDR_VERSION         1U

typedef struct {
    uint32_t magic;
    uint32_t link_addr;         /* slot base the image is linked for */
    uint32_t size;              /* bytes covered by the CRC, multiple of 4; patched after linking */
    uint16_t fw_version;
    uint16_t hdr_version;
} app_header_t;

#define BOOT_VERSION            0x0001U

/*
 * Boot record: one data-EEPROM word, written atomically.
 *   bits  7:0  active slot (last confirmed good)
 *   bits 15:8  trial slot (SLOT_NONE if no update pending)
 *   bits 23:16 boot attempts of the trial slot
 *   bits 31:24 check = (active ^ trial ^ tries) ^ 0xA5   (erased word 0 is therefore invalid)
 */
#define BOOTREC_ADDR            0x08080080UL
#define BOOT_MAX_TRIES          3U
#define CONFIRM_AFTER_MS        60000U

typedef struct {
    uint8_t active, trial, tries;
} bootrec_t;

static inline uint32_t slot_base(uint8_t slot)
{
    return (slot == SLOT_B) ? SLOT_B_BASE : SLOT_A_BASE;
}

/* nvm.c - data EEPROM and flash programming, CRC32 */
uint32_t crc32_calc(uint32_t crc, const uint8_t *p, uint32_t n);    /* start with crc = 0 */
int nvm_eeprom_write(uint32_t addr, const uint32_t *w, uint32_t nwords);
int nvm_flash_write_page(uint32_t addr, const uint32_t *w);         /* erase + 32 words */

/* bootrec.c */
int bootrec_read(bootrec_t *r);                 /* 1 = valid */
int bootrec_write(const bootrec_t *r);
int image_valid(uint8_t slot, const app_header_t **hdr);   /* header + CRC check */

#endif /* FLASH_LAYOUT_H */
