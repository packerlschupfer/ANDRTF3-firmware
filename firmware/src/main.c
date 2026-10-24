/*
 * main.c - open firmware for the Andivi ANDRTF3/MD (minimal PT1000 variant)
 */
#include "board.h"
#include "system.h"
#include "debug.h"
#include "dip.h"
#include "modbus.h"
#include "pt1000.h"
#include "regs.h"
#include "calib.h"
#include "update.h"
#include "flash_layout.h"

__attribute__((section(".app_header"), used))
const app_header_t app_header = {
    .magic = IMG_MAGIC_APP,
    .link_addr = (APP_SLOT == SLOT_B) ? SLOT_B_BASE : SLOT_A_BASE,
    .size = 0,                              /* patched by tools/post_image.py */
    .fw_version = FW_VERSION,
    .hdr_version = IMG_HDR_VERSION,
};

#define STAT_INTERVAL_MS        60000U

static void trace(const pt_result_t *r)
{
    static const char *const names[] = { "ok", "timeout", "overflow", "range" };

    dbg_put_int((int32_t)millis());
    dbg_puts(r->status == PT_OK ? " PT1000 " : " !! PT1000 ");
    dbg_puts(names[r->status]);
    dbg_puts(" raw=");
    dbg_put_hex(r->raw, 6);
    dbg_puts(" rdy=");
    dbg_put_int((int32_t)r->ready_ms);
    dbg_puts("ms code=");
    dbg_put_int(r->code);
    dbg_puts(" R=");
    dbg_put_int((int32_t)r->r_milliohm);
    dbg_puts(" mOhm");
    if (r->status == PT_OK) {
        dbg_puts(" T=");
        dbg_put_int(r->temp_cdeg + g_set.offset_cdeg);
        dbg_puts(" (0.01 C)");
    }
    dbg_puts("\n");
}

static void put_kv(const char *k, uint32_t v)
{
    dbg_puts(k);
    dbg_put_int((int32_t)v);
}

static void stat_line(uint32_t now)
{
    uint32_t good, fail, to, rng, ovf;
    modbus_stats_t bus;

    regs_counters(&good, &fail, &to, &rng, &ovf);
    dbg_put_int((int32_t)now);
    dbg_puts(" STAT up=");
    dbg_put_int((int32_t)(now / 1000U));
    dbg_puts("s good=");
    dbg_put_int((int32_t)good);
    dbg_puts(" fail=");
    dbg_put_int((int32_t)fail);
    dbg_puts(" (timeout=");
    dbg_put_int((int32_t)to);
    dbg_puts(" range=");
    dbg_put_int((int32_t)rng);
    dbg_puts(" overflow=");
    dbg_put_int((int32_t)ovf);
    dbg_puts(") rref=");
    dbg_put_int((int32_t)g_set.rref_milliohm);
    dbg_puts(" offset=");
    dbg_put_int(g_set.offset_cdeg);
    dbg_puts(calib_loaded_from_eeprom() ? " ee=ok" : " ee=defaults");
    dbg_puts(calib_dirty() ? " unsaved\n" : "\n");

    modbus_get_stats(&bus);
    dbg_put_int((int32_t)now);
    put_kv(" BUS frames=", bus.frames);
    put_kv(" handled=", bus.handled);
    put_kv(" crc=", bus.crc);
    put_kv(" badlen=", bus.bad_length);
    put_kv(" fe=", bus.framing);
    put_kv(" pe=", bus.parity);
    put_kv(" ne=", bus.noise);
    put_kv(" ore=", bus.overrun);
    put_kv(" exc=", bus.exceptions);
    dbg_puts("\n");
}

int main(void)
{
    uint16_t dip;
    uint32_t last_measure, last_blink = 0, last_stat = 0;
    pt_result_t res;

    uint8_t rst = system_reset_cause();     /* latch before anything can reset again */

    system_clock_init();
    board_init();
    dbg_init();
    calib_init();

    dip = dip_read();
    modbus_init((uint8_t)(dip >> 8), (uint8_t)dip);
    pt_init();

    dbg_puts("\nANDRTF3 open firmware 0x");
    dbg_put_hex(FW_VERSION, 4);
    dbg_puts("  DIP=0x");
    dbg_put_hex(dip, 4);
    dbg_puts("  addr=");
    dbg_put_int(dip >> 8);
    dbg_puts("  ");
    dbg_put_int((int32_t)modbus_baud());
    dbg_puts(" 8");
    {
        char p[2] = { modbus_parity(), 0 };
        dbg_puts(p);
    }
    dbg_put_int(modbus_stopbits());
    dbg_puts(APP_SLOT == SLOT_B ? "  slot B\n" : "  slot A\n");
    dbg_puts("ID uid=");
    for (uint8_t i = 0; i < 6U; i++) {
        dbg_put_hex(system_uid_word(i), 4);
    }
    dbg_puts(" asset=");
    dbg_put_int(g_set.asset);
    dbg_puts(" label=\"");
    {
        char l[2 * IDENT_LABEL_REGS + 1];
        for (uint8_t i = 0; i < IDENT_LABEL_REGS; i++) {
            l[2 * i] = (char)(g_set.label[i] >> 8);
            l[2 * i + 1] = (char)g_set.label[i];
        }
        l[2 * IDENT_LABEL_REGS] = 0;
        dbg_puts(l);                        /* stops at the first NUL */
    }
    dbg_puts("\"\n");
    dbg_puts("RESET cause=");
    dbg_puts(system_reset_cause_name(rst));
    dbg_puts(" flags=0x");
    dbg_put_hex(rst, 2);
    dbg_puts(rst & 0x60U ? "  !! watchdog reset\n" : "\n");
    stat_line(0);

    watchdog_start();
    pt_start();
    last_measure = millis();

    for (;;) {
        uint32_t now = millis();

        watchdog_kick();
        modbus_poll(now);
        update_poll(now);
#ifdef TEST_HANG
        if (now > 10000U) {                 /* bench test only: simulate a firmware that hangs */
            for (;;) {
            }
        }
#endif

        if (pt_poll(&res)) {
            int ok = (res.status == PT_OK);

            if (calib_capturing()) {
                calib_feed(ok, res.code);   /* captures do not touch reg 50 */
            } else {
                regs_update_measurement(ok, res.status, res.temp_cdeg, res.r_milliohm, now);
            }
            if (g_set.debug || !ok) {
                trace(&res);
            }
        }
        if (!pt_busy() && (calib_capturing() || (now - last_measure) >= g_set.interval_ms)) {
            last_measure = now;
            pt_start();
        }
        if ((now - last_stat) >= STAT_INTERVAL_MS) {
            last_stat = now;
            stat_line(now);
        }
        if ((now - last_blink) >= 500U) {
            last_blink = now;
            LED_HB_PORT->ODR ^= (1UL << LED_HB_PIN);
        }
    }
}
