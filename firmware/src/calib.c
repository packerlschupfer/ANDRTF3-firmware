/*
 * calib.c - persistent settings in data EEPROM and the parallel-resistor calibration
 */
#include "calib.h"
#include "board.h"
#include "debug.h"
#include "flash_layout.h"
#include <string.h>

#define EE_ADDR             0x08080100UL    /* word 0 (0x08080000) is left to the original's use */
#define EE_ADDR_COPY        0x08080140UL    /* second copy: a power loss during a save keeps one valid */
#define EE_MAGIC            0x4C414341UL    /* "ACAL" */
#define EE_VERSION          1U

#define CAPTURE_SAMPLES     16U
#define RREF_NOMINAL        10000000UL
#define RREF_MIN            9500000UL       /* +-5 % plausibility */
#define RREF_MAX            10500000UL

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    settings_t s;
    uint32_t crc;
} ee_image_t;

_Static_assert(sizeof(ee_image_t) <= EE_ADDR_COPY - EE_ADDR, "settings copies overlap");

settings_t g_set;

static int s_loaded, s_dirty;
static uint16_t s_state = CAL_IDLE, s_last_cmd;

/* capture accumulation */
static int s_capture;                       /* 0 none, 'A' or 'B' */
static uint32_t s_n;
static int64_t s_sum;
static int64_t s_sum_a, s_sum_b;
static uint32_t s_cnt_a, s_cnt_b;

static void defaults(void)
{
    g_set.rref_milliohm = RREF_NOMINAL;
    g_set.offset_cdeg = 0;
    g_set.rcal_deciohm = 10000;     /* 1000.0 Ohm */
    g_set.filter = 1;
    g_set.interval_ms = 1000;
    g_set.keep_last_good = 0;
    g_set.debug = 1;
    g_set.reserved = 0;
    g_set.reserved2 = 0;
}

static void clear_identity(void)
{
    g_set.asset = 0;
    memset(g_set.label, 0, sizeof(g_set.label));
}

static int load_from(uint32_t addr)
{
    const ee_image_t *img = (const ee_image_t *)addr;
    uint32_t crc;

    if (img->magic != EE_MAGIC) {
        return 0;
    }
    if (img->version != EE_VERSION || img->size != sizeof(settings_t)) {
        return 0;
    }
    memcpy(&crc, (const uint8_t *)&img->s + img->size, sizeof(crc));   /* CRC follows the payload */
    if (crc32_calc(0, (const uint8_t *)&img->s, img->size) != crc) {
        return 0;
    }
    memcpy(&g_set, &img->s, sizeof(settings_t));
    if (g_set.interval_ms < 100U || g_set.interval_ms > 60000U) {
        g_set.interval_ms = 1000U;
    }
    return 1;
}

static int load(void)
{
    return load_from(EE_ADDR) || load_from(EE_ADDR_COPY);
}

static int save(void)
{
    ee_image_t img;

    img.magic = EE_MAGIC;
    img.version = EE_VERSION;
    img.size = sizeof(settings_t);
    img.s = g_set;
    img.crc = crc32_calc(0, (const uint8_t *)&img.s, sizeof(settings_t));
    /* one copy after the other: whichever write a power loss interrupts, the other copy is whole */
    return nvm_eeprom_write(EE_ADDR, (const uint32_t *)&img, sizeof(img) / 4U) &&
           nvm_eeprom_write(EE_ADDR_COPY, (const uint32_t *)&img, sizeof(img) / 4U);
}

void calib_init(void)
{
    defaults();
    clear_identity();
    s_loaded = load();
    s_dirty = 0;
}

int calib_loaded_from_eeprom(void) { return s_loaded; }
int calib_dirty(void) { return s_dirty; }
void calib_mark_dirty(void) { s_dirty = 1; }
uint16_t calib_state(void) { return s_state; }
uint16_t calib_last_command(void) { return s_last_cmd; }
int calib_capturing(void) { return s_capture != 0; }

static void start_capture(int which)
{
    s_capture = which;
    s_n = 0;
    s_sum = 0;
    s_state = (which == 'A') ? CAL_CAPTURING_A : CAL_CAPTURING_B;
}

static void compute(void)
{
    double a, b, rref;

    if (!s_cnt_a) {
        s_state = CAL_ERR_NO_A;
        return;
    }
    if (!s_cnt_b) {
        s_state = CAL_ERR_NO_B;
        return;
    }
    a = (double)s_sum_a / (double)s_cnt_a;          /* mean code */
    b = (double)s_sum_b / (double)s_cnt_b;
    a = a / (2097152.0 - a);                        /* ratio R/R_ref */
    b = b / (2097152.0 - b);
    if (a <= 0.0 || b <= 0.0 || b >= a) {
        s_state = CAL_ERR_IMPLAUSIBLE;
        return;
    }
    rref = (double)g_set.rcal_deciohm * 100.0 * (1.0 / b - 1.0 / a);   /* mOhm */
    dbg_puts("CAL a=");
    dbg_put_int((int32_t)(a * 1e6));
    dbg_puts("e-6 b=");
    dbg_put_int((int32_t)(b * 1e6));
    dbg_puts("e-6 Rref=");
    dbg_put_int((int32_t)rref);
    dbg_puts(" mOhm Rpt=");
    dbg_put_int((int32_t)(a * rref));
    dbg_puts(" mOhm\n");
    if (rref < RREF_MIN || rref > RREF_MAX) {
        s_state = CAL_ERR_IMPLAUSIBLE;
        return;
    }
    g_set.rref_milliohm = (uint32_t)(rref + 0.5);
    s_dirty = 1;
    s_state = CAL_COMPUTED;
}

void calib_command(uint16_t cmd)
{
    s_last_cmd = cmd;
    switch (cmd) {
    case CAL_CMD_CAPTURE_A: start_capture('A'); break;
    case CAL_CMD_CAPTURE_B: start_capture('B'); break;
    case CAL_CMD_COMPUTE:   compute(); break;
    case CAL_CMD_CLEAR:
        s_capture = 0;
        s_sum_a = s_sum_b = 0;
        s_cnt_a = s_cnt_b = 0;
        s_state = CAL_IDLE;
        break;
    case CAL_CMD_SAVE:
        if (save()) {
            s_loaded = 1;
            s_dirty = 0;
            s_state = CAL_SAVED;
        } else {
            s_state = CAL_ERR_EEPROM;
        }
        break;
    case CAL_CMD_DEFAULTS:
        defaults();
        s_dirty = 1;
        s_state = CAL_IDLE;
        break;
    case CAL_CMD_RELOAD:
        defaults();
        clear_identity();
        s_loaded = load();
        s_dirty = 0;
        s_state = CAL_IDLE;
        break;
    default:
        s_state = CAL_ERR_BAD_COMMAND;
        break;
    }
}

void calib_feed(int ok, int32_t code)
{
    if (!s_capture) {
        return;
    }
    if (!ok) {
        s_capture = 0;
        s_state = CAL_ERR_MEASUREMENT;
        return;
    }
    s_sum += code;
    if (++s_n < CAPTURE_SAMPLES) {
        return;
    }
    if (s_capture == 'A') {
        s_sum_a += s_sum;
        s_cnt_a += s_n;
    } else {
        s_sum_b += s_sum;
        s_cnt_b += s_n;
    }
    dbg_puts(s_capture == 'A' ? "CAL captured A, mean code=" : "CAL captured B, mean code=");
    dbg_put_int((int32_t)(s_sum / (int64_t)s_n));
    dbg_puts("\n");
    s_capture = 0;
    s_state = CAL_CAPTURED;
}
