/*
 * bootrec.c - boot record in data EEPROM and image validation (shared with the bootloader)
 */
#include "flash_layout.h"

static uint8_t check_byte(const bootrec_t *r)
{
    return (uint8_t)((r->active ^ r->trial ^ r->tries) ^ 0xA5U);
}

int bootrec_read(bootrec_t *r)
{
    uint32_t w = *(volatile const uint32_t *)BOOTREC_ADDR;

    r->active = (uint8_t)w;
    r->trial = (uint8_t)(w >> 8);
    r->tries = (uint8_t)(w >> 16);
    return (uint8_t)(w >> 24) == check_byte(r) && r->active <= SLOT_B &&
           (r->trial <= SLOT_B || r->trial == SLOT_NONE);
}

int bootrec_write(const bootrec_t *r)
{
    uint32_t w = (uint32_t)r->active | ((uint32_t)r->trial << 8) | ((uint32_t)r->tries << 16) |
                 ((uint32_t)check_byte(r) << 24);

    return nvm_eeprom_write(BOOTREC_ADDR, &w, 1);
}

int image_valid(uint8_t slot, const app_header_t **hdr)
{
    uint32_t base = slot_base(slot);
    const app_header_t *h = (const app_header_t *)(base + HEADER_OFFSET);
    uint32_t sp = *(const uint32_t *)base, pc = *(const uint32_t *)(base + 4U);

    if (hdr) {
        *hdr = h;
    }
    if (h->magic != IMG_MAGIC_APP || h->hdr_version != IMG_HDR_VERSION || h->link_addr != base ||
        h->size < HEADER_OFFSET + sizeof(app_header_t) || h->size > SLOT_SIZE - 4U || (h->size & 3U)) {
        return 0;
    }
    if (sp < 0x20000000UL || sp > 0x20005000UL || pc < base || pc >= base + h->size) {
        return 0;
    }
    return crc32_calc(0, (const uint8_t *)base, h->size) == *(const uint32_t *)(base + h->size);
}
