/*
 * boot.c - ANDRTF3 bootloader: pick a slot, check it, start it. No Modbus, no flash writes.
 *
 *   trial slot pending  -> tries < BOOT_MAX_TRIES and image valid: tries++, start trial
 *                          otherwise: drop the trial (rollback), continue with the active slot
 *   active slot valid   -> start it
 *   other slot valid    -> make it active, start it (recovery, e.g. active slot damaged)
 *   nothing valid       -> stay here, print a message every 5 s (needs SWD)
 *
 * The firmware confirms a trial after CONFIRM_AFTER_MS of normal running (see update.c).
 */
#include "board.h"
#include "system.h"
#include "debug.h"
#include "flash_layout.h"

__attribute__((section(".app_header"), used))
const app_header_t boot_header = {
    .magic = IMG_MAGIC_BOOT,
    .link_addr = BOOT_BASE,
    .size = 0,
    .fw_version = BOOT_VERSION,
    .hdr_version = IMG_HDR_VERSION,
};

static void say(const char *a, uint8_t slot, const char *b)
{
    dbg_puts(a);
    if (slot != SLOT_NONE) {
        dbg_puts(slot == SLOT_A ? "A" : "B");
    }
    dbg_puts(b);
}

static void start(uint8_t slot)
{
    uint32_t base = slot_base(slot);
    const app_header_t *h = (const app_header_t *)(base + HEADER_OFFSET);
    uint32_t sp = *(const uint32_t *)base, pc = *(const uint32_t *)(base + 4U);

    say("BOOT start slot ", slot, " fw 0x");
    dbg_put_hex(h->fw_version, 4);
    dbg_puts("\n");

    /* Hand over a reset-like state: peripherals the bootloader touched go back to reset values */
    __disable_irq();
    SysTick->CTRL = 0;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    RCC->APB1RSTR |= RCC_APB1RSTR_USART5RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_USART5RST;
    RCC->APB1ENR &= ~RCC_APB1ENR_USART5EN;

    SCB->VTOR = base;
    __set_MSP(sp);
    __enable_irq();
    ((void (*)(void))pc)();
    for (;;) {
    }
}

int main(void)
{
    bootrec_t r;
    uint8_t other;
    int have = 0;

    system_clock_init();
    board_init();
    dbg_init();
    dbg_puts("\nBOOT ANDRTF3 bootloader 0x");
    dbg_put_hex(BOOT_VERSION, 4);
    dbg_puts("\n");

    if (!bootrec_read(&r)) {
        /* First start after SWD flashing, or a record damaged by a power loss during a write:
         * prefer A. If B is valid too, A only runs on trial, so a hanging A still falls back to B. */
        uint8_t first = image_valid(SLOT_A, 0) ? SLOT_A : SLOT_B;
        uint8_t second = (first == SLOT_A) ? SLOT_B : SLOT_A;

        if (image_valid(second, 0)) {
            r.active = second;
            r.trial = first;
        } else {
            r.active = first;
            r.trial = SLOT_NONE;
        }
        r.tries = 0;
        dbg_puts("BOOT no boot record, default\n");
        have = 1;
    }
    if (r.trial == r.active) {
        r.trial = SLOT_NONE;                /* never both: the active slot must stay known-good */
        r.tries = 0;
        have = 1;
    }

    if (r.trial != SLOT_NONE) {
        if (r.tries < BOOT_MAX_TRIES && image_valid(r.trial, 0)) {
            r.tries++;
            bootrec_write(&r);
            say("BOOT trial slot ", r.trial, ", attempt ");
            dbg_put_int(r.tries);
            dbg_puts("\n");
            start(r.trial);
        }
        say("!! BOOT trial slot ", r.trial, " failed - rollback\n");
        r.trial = SLOT_NONE;
        r.tries = 0;
        bootrec_write(&r);
        have = 0;
    }

    if (image_valid(r.active, 0)) {
        if (have) {
            bootrec_write(&r);
        }
        start(r.active);
    }
    other = (r.active == SLOT_A) ? SLOT_B : SLOT_A;
    say("!! BOOT active slot ", r.active, " invalid\n");
    if (image_valid(other, 0)) {
        r.active = other;
        bootrec_write(&r);
        start(other);
    }

    for (;;) {
        dbg_puts("!! BOOT no valid firmware - flash over SWD\n");
        delay_ms(5000);
    }
}
