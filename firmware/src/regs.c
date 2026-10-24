/*
 * regs.c - Modbus register map and the reg 50 filter (see regs.h)
 */
#include "regs.h"
#include "calib.h"
#include "pt1000.h"
#include "modbus.h"
#include "system.h"
#include "update.h"

#define EX_ILLEGAL_ADDRESS  2
#define EX_ILLEGAL_VALUE    3

#define GLITCH_HOLD         3U      /* filter bridges up to GLITCH_HOLD-1 bad readings */
#define RREF_MIN            9500000UL
#define RREF_MAX            10500000UL

static int16_t s_temp_report;       /* reg 50, 0.1 degC */
static int32_t s_temp_last_cdeg;    /* reg 57 */
static uint16_t s_status = REG_STATUS_NEVER;
static uint16_t s_r_deciohm;
static uint32_t s_good, s_fail, s_timeouts, s_range, s_overflow;
static uint16_t s_last_fail;
static uint32_t s_good_ms;
static uint32_t s_bad_run;

static int32_t s_hist[3];
static uint32_t s_hist_n, s_hist_pos;

static uint16_t s_rref_hi_staged;

static int16_t cdeg_to_ddeg(int32_t c)
{
    return (int16_t)((c >= 0) ? (c + 5) / 10 : (c - 5) / 10);
}

static int32_t median(void)
{
    int32_t a = s_hist[0], b = s_hist[1], c = s_hist[2];

    if (s_hist_n == 1) {
        return s_hist[(s_hist_pos + 2) % 3];
    }
    if (s_hist_n == 2) {
        int32_t x = s_hist[(s_hist_pos + 1) % 3], y = s_hist[(s_hist_pos + 2) % 3];
        return (x + y) / 2;
    }
    if ((a <= b && b <= c) || (c <= b && b <= a)) {
        return b;
    }
    if ((b <= a && a <= c) || (c <= a && a <= b)) {
        return a;
    }
    return c;
}

void regs_update_measurement(int ok, int status, int32_t temp_cdeg, uint32_t r_milliohm, uint32_t now_ms)
{
    uint32_t r = r_milliohm / 100U;

    s_r_deciohm = (r > 0xFFFFU) ? 0xFFFFU : (uint16_t)r;

    if (ok) {
        int32_t t = temp_cdeg + g_set.offset_cdeg;

        if (s_bad_run >= GLITCH_HOLD) {
            s_hist_n = 0;                   /* long outage: don't mix in stale values */
        }
        s_bad_run = 0;
        s_hist[s_hist_pos] = t;
        s_hist_pos = (s_hist_pos + 1U) % 3U;
        if (s_hist_n < 3U) {
            s_hist_n++;
        }
        s_temp_last_cdeg = t;
        s_temp_report = cdeg_to_ddeg(g_set.filter ? median() : t);
        s_good_ms = now_ms;
        s_good++;
        s_status = REG_STATUS_VALID;
        return;
    }

    s_fail++;
    s_bad_run++;
    s_last_fail = (uint16_t)status;
    s_status = (uint16_t)(s_status & REG_STATUS_NEVER);
    switch (status) {
    case PT_TIMEOUT:  s_status |= REG_STATUS_TIMEOUT;  s_timeouts++; break;
    case PT_OVERFLOW: s_status |= REG_STATUS_OVERFLOW; s_overflow++; break;
    default:          s_status |= REG_STATUS_RANGE;    s_range++;    break;
    }

    if (s_status & REG_STATUS_NEVER) {
        s_temp_report = 0;
    } else if (g_set.filter && s_bad_run < GLITCH_HOLD) {
        s_status |= REG_STATUS_VALID | REG_STATUS_HELD;     /* short glitch: reg 50 still trustworthy */
    } else if (g_set.keep_last_good) {
        /* keep s_temp_report */
    } else {
        s_temp_report = 0;                  /* as the original, but also on ADC timeouts (the
                                               original keeps the old value there) */
    }
}

void regs_counters(uint32_t *good, uint32_t *fail, uint32_t *timeouts, uint32_t *range, uint32_t *overflow)
{
    *good = s_good;
    *fail = s_fail;
    *timeouts = s_timeouts;
    *range = s_range;
    *overflow = s_overflow;
}

int regs_read_input(uint16_t addr, uint16_t *val, uint32_t now_ms)
{
    uint32_t age, up = now_ms / 1000U;
    modbus_stats_t bus;
    uint16_t bi[4];

    if (addr > REG_INPUT_LAST) {
        return EX_ILLEGAL_ADDRESS;
    }
    switch (addr) {
    case 1:  *val = 1; break;
    case 2:  *val = FW_VERSION; break;
    case REG_TEMP: *val = (uint16_t)s_temp_report; break;
    case 51: *val = s_status; break;
    case 52: *val = s_r_deciohm; break;
    case 53:
        age = (now_ms - s_good_ms) / 1000U;
        *val = ((s_status & REG_STATUS_NEVER) || age > 0xFFFEU) ? 0xFFFFU : (uint16_t)age;
        break;
    case 54: *val = (uint16_t)s_good; break;
    case 55: *val = (uint16_t)s_fail; break;
    case 56: *val = s_last_fail; break;
    case 57:
        *val = (uint16_t)(int16_t)((s_temp_last_cdeg > INT16_MAX) ? INT16_MAX :
                                   (s_temp_last_cdeg < INT16_MIN) ? INT16_MIN : s_temp_last_cdeg);
        break;
    case 58: *val = (uint16_t)(up >> 16); break;
    case 59: *val = (uint16_t)up; break;
    case 60: *val = (uint16_t)s_timeouts; break;
    case 61: *val = (uint16_t)s_range; break;
    case 62: *val = (uint16_t)s_overflow; break;
    case 63: *val = (uint16_t)((calib_loaded_from_eeprom() ? 1U : 0U) | (calib_dirty() ? 2U : 0U)); break;
    case 64: *val = system_reset_cause(); break;
    case 65: case 66: case 67: case 68: case 69: case 70: case 71: case 72: case 73:
        modbus_get_stats(&bus);
        {
            const uint32_t v[] = { bus.frames, bus.handled, bus.crc, bus.bad_length, bus.framing,
                                   bus.parity, bus.noise, bus.overrun, bus.exceptions };
            *val = (uint16_t)v[addr - 65U];
        }
        break;
    case 74: case 75: case 76: case 77:
        update_boot_info(&bi[0], &bi[1], &bi[2], &bi[3]);
        *val = bi[addr - 74U];
        break;
    case 78: case 79: case 80: case 81: case 82: case 83:
        *val = system_uid_word((uint8_t)(addr - 78U));
        break;
    default: *val = 0; break;
    }
    return 0;
}

int regs_read_holding(uint16_t addr, uint16_t *val)
{
    if (addr < REG_HOLD_FIRST || addr > REG_HOLD_LAST) {
        return EX_ILLEGAL_ADDRESS;
    }
    switch (addr) {
    case 515: *val = g_set.debug; break;
    case 516: *val = g_set.keep_last_good; break;
    case 517: *val = g_set.filter; break;
    case 518: *val = g_set.interval_ms; break;
    case 520: *val = (uint16_t)(g_set.rref_milliohm >> 16); break;
    case 521: *val = (uint16_t)g_set.rref_milliohm; break;
    case 522: *val = (uint16_t)g_set.offset_cdeg; break;
    case 523: *val = g_set.rcal_deciohm; break;
    case REG_ASSET: *val = g_set.asset; break;
    case 533: case 534: case 535: case 536: case 537: case 538: case 539:
        *val = g_set.label[addr - REG_LABEL_FIRST];
        break;
    case 530: *val = calib_last_command(); break;
    case 531: *val = calib_state(); break;
    case 540:
    case 541: *val = update_state(); break;
    case 542: *val = (uint16_t)(update_received() >> 16); break;
    case 543: *val = (uint16_t)update_received(); break;
    default:  *val = 0; break;
    }
    return 0;
}

/* label bytes: printable ASCII, or 0 as padding */
static int label_char(uint16_t c)
{
    return c == 0U || (c >= 0x20U && c <= 0x7EU);
}

int regs_write_holding(uint16_t addr, uint16_t val)
{
    uint32_t rref;

    if (addr < REG_HOLD_FIRST || addr > REG_HOLD_LAST) {
        return EX_ILLEGAL_ADDRESS;
    }
    switch (addr) {
    case 515: g_set.debug = val ? 1U : 0U; break;
    case 516: g_set.keep_last_good = val ? 1U : 0U; break;
    case 517:
        if (val > 1U) {
            return EX_ILLEGAL_VALUE;
        }
        g_set.filter = val;
        break;
    case 518:
        if (val < 100U || val > 60000U) {
            return EX_ILLEGAL_VALUE;
        }
        g_set.interval_ms = val;
        break;
    case 520:
        s_rref_hi_staged = val;             /* applied together with 521 */
        return 0;
    case 521:
        rref = ((uint32_t)s_rref_hi_staged << 16) | val;
        if (rref < RREF_MIN || rref > RREF_MAX) {
            return EX_ILLEGAL_VALUE;
        }
        g_set.rref_milliohm = rref;
        break;
    case 522: g_set.offset_cdeg = (int16_t)val; break;
    case 523:
        if (val < 1000U || val > 30000U) {  /* 100 .. 3000 Ohm */
            return EX_ILLEGAL_VALUE;
        }
        g_set.rcal_deciohm = val;
        break;
    case REG_ASSET: g_set.asset = val; break;
    case 533: case 534: case 535: case 536: case 537: case 538: case 539:
        if (!label_char(val >> 8) || !label_char(val & 0xFFU)) {
            return EX_ILLEGAL_VALUE;
        }
        g_set.label[addr - REG_LABEL_FIRST] = val;
        break;
    case 530:
        calib_command(val);
        return 0;
    case 540:
        update_command(val, millis());
        return 0;
    default:
        return 0;                           /* reserved / read-only: ignored */
    }
    calib_mark_dirty();
    return 0;
}
