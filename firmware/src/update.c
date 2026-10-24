/*
 * update.c - firmware update over Modbus into the inactive slot (see update.h)
 */
#include "update.h"
#include "flash_layout.h"
#include "debug.h"
#include "board.h"
#include "modbus.h"

#define EX_ILLEGAL_ADDRESS  2
#define EX_ILLEGAL_VALUE    3
#define EX_DEVICE_FAILURE   4

static uint16_t s_state = UPD_IDLE;
static uint32_t s_next;                 /* next expected byte offset */
static uint32_t s_reset_at;
static int s_confirmed;
static uint32_t s_page[FLASH_PAGE / 4U];   /* received chunk, programmed after the reply went out */
static uint32_t s_page_off;
static int s_page_pending;

static uint8_t target_slot(void)
{
    return (APP_SLOT == SLOT_A) ? SLOT_B : SLOT_A;
}

/* This firmware has not confirmed itself yet: the other slot is the only known-good one */
static int on_trial(void)
{
    bootrec_t r;

    return bootrec_read(&r) && r.trial == APP_SLOT;
}

static void fail(uint16_t err)
{
    s_state = err;
    dbg_puts("!! UPDATE error 0x");
    dbg_put_hex(err, 4);
    dbg_puts("\n");
}

uint16_t update_state(void) { return s_state; }
uint32_t update_received(void) { return s_next; }

void update_command(uint16_t cmd, uint32_t now_ms)
{
    const app_header_t *h;
    bootrec_t r;

    if (s_state == UPD_ACTIVATING) {
        return;                             /* reboot pending; a repeated command changes nothing */
    }
    switch (cmd) {
    case UPD_CMD_BEGIN:
        if (on_trial()) {
            fail(UPD_ERR_ON_TRIAL);
            break;
        }
        s_next = 0;
        s_state = UPD_RECEIVING;
        dbg_puts("UPDATE begin, target slot ");
        dbg_puts(target_slot() == SLOT_A ? "A\n" : "B\n");
        break;
    case UPD_CMD_VERIFY:
        if (s_state != UPD_RECEIVING && s_state != UPD_VERIFIED) {   /* repeat allowed */
            fail(UPD_ERR_SEQUENCE);
        } else if (!image_valid(target_slot(), &h)) {
            fail(UPD_ERR_IMAGE);
        } else if (s_next < h->size + 4U) {
            fail(UPD_ERR_IMAGE);            /* CRC word not received yet */
        } else {
            s_state = UPD_VERIFIED;
            dbg_puts("UPDATE verified, fw 0x");
            dbg_put_hex(h->fw_version, 4);
            dbg_puts(" size ");
            dbg_put_int((int32_t)h->size);
            dbg_puts("\n");
        }
        break;
    case UPD_CMD_ACTIVATE:
        if (s_state != UPD_VERIFIED || !image_valid(target_slot(), 0)) {
            fail(UPD_ERR_SEQUENCE);
            break;
        }
        if (on_trial()) {
            fail(UPD_ERR_ON_TRIAL);
            break;
        }
        if (!bootrec_read(&r)) {
            r.active = APP_SLOT;
        }
        r.trial = target_slot();
        r.tries = 0;
        if (!bootrec_write(&r)) {
            fail(UPD_ERR_FLASH);
            break;
        }
        s_state = UPD_ACTIVATING;
        s_reset_at = now_ms + 200U;         /* let the Modbus reply go out first */
        dbg_puts("UPDATE activate - reboot into the new slot\n");
        break;
    case UPD_CMD_ABORT:
        s_state = UPD_IDLE;
        s_next = 0;
        break;
    default:
        fail(UPD_ERR_BAD_COMMAND);
        break;
    }
}

int update_write_chunk(const volatile uint8_t *data, uint16_t nregs)
{
    uint32_t off;

    if (s_state != UPD_RECEIVING || s_page_pending) {
        return EX_DEVICE_FAILURE;
    }
    if (nregs != 2U + FLASH_PAGE / 2U) {
        return EX_ILLEGAL_VALUE;
    }
    off = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
    /* in order; the previous chunk may be repeated (lost reply) */
    if ((off != s_next && off + FLASH_PAGE != s_next) || off >= SLOT_SIZE) {
        fail(UPD_ERR_OFFSET);
        return EX_ILLEGAL_VALUE;
    }
    for (uint32_t i = 0; i < FLASH_PAGE / 4U; i++) {
        const volatile uint8_t *b = &data[4U + 4U * i];
        s_page[i] = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    }
    s_page_off = off;
    s_page_pending = 1;                     /* programmed by update_poll(), keeps the bus turn short */
    s_next = off + FLASH_PAGE;
    return 0;
}

void update_poll(uint32_t now_ms)
{
    bootrec_t r, want;
    modbus_stats_t st;
    int valid;

    if (s_page_pending) {
        if (!nvm_flash_write_page(slot_base(target_slot()) + s_page_off, s_page)) {
            fail(UPD_ERR_FLASH);
        }
        s_page_pending = 0;
    }

    if (s_state == UPD_ACTIVATING) {
        if ((int32_t)(now_ms - s_reset_at) >= 0) {
            NVIC_SystemReset();
        }
        return;
    }
    if (s_confirmed || now_ms < CONFIRM_AFTER_MS) {
        return;
    }
    modbus_get_stats(&st);
    if (st.replies == 0) {
        return;     /* Modbus not proven yet: a build with a broken bus stays on trial and is
                       rolled back after 3 boots instead of becoming the known-good one */
    }
    /* Running fine for a minute and answering on the bus: this slot becomes the known-good one */
    s_confirmed = 1;
    valid = bootrec_read(&r);
    if (valid && r.trial != SLOT_NONE && r.trial != APP_SLOT) {
        return;                             /* someone else's update is pending */
    }
    if (valid && r.active == APP_SLOT && r.trial == SLOT_NONE && r.tries == 0) {
        return;                             /* nothing to change: spare the EEPROM */
    }
    want.active = APP_SLOT;
    want.trial = SLOT_NONE;
    want.tries = 0;
    dbg_puts(bootrec_write(&want) ? "UPDATE confirmed, active slot " : "!! UPDATE confirm failed, slot ");
    dbg_puts(APP_SLOT == SLOT_A ? "A\n" : "B\n");
}

void update_boot_info(uint16_t *slot, uint16_t *rec, uint16_t *tries, uint16_t *boot_ver)
{
    bootrec_t r;
    const app_header_t *bh = (const app_header_t *)(BOOT_BASE + HEADER_OFFSET);

    *slot = APP_SLOT;
    if (bootrec_read(&r)) {
        *rec = (uint16_t)(r.active | (r.trial << 8));
        *tries = r.tries;
    } else {
        *rec = 0xFFFFU;
        *tries = 0;
    }
    *boot_ver = (bh->magic == IMG_MAGIC_BOOT) ? bh->fw_version : 0U;
}
