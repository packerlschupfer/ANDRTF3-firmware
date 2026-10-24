/*
 * pt1000.c - PT1000 measurement through the MCP3551/3553 delta-sigma ADC
 *
 * Front end (from the original firmware): PT1000 in a divider with a 10 kOhm
 * reference resistor, ratiometric to the ADC reference, so
 *     R = R_ref * code / (2^21 - code)       (R_ref calibrated, see calib.c)
 * The ADC runs single conversions: a short /CS low pulse starts one, it takes
 * ~73 ms (MCP3551), then SDO going low with /CS low signals data ready.
 * Data is read in SPI mode 3 (SCK idle high, sample on the rising edge).
 */
#include <math.h>
#include "pt1000.h"
#include "board.h"
#include "system.h"
#include "calib.h"

#define CONV_WAIT_MS        75U     /* original firmware: 70 ms, MCP3551 typ. 72.7 ms */
#define READY_TIMEOUT_MS    100U
#define R_MIN_MILLIOHM      500000UL
#define R_MAX_MILLIOHM      3000000UL

static enum { ST_IDLE, ST_CONVERTING, ST_WAIT_READY } s_state;
static uint32_t s_t0;

void pt_init(void)
{
    pin_set(ADC_CS_PORT, ADC_CS_PIN);
    pin_set(ADC_SCK_PORT, ADC_SCK_PIN);
    s_state = ST_IDLE;
}

int pt_busy(void)
{
    return s_state != ST_IDLE;
}

void pt_start(void)
{
    pin_clr(ADC_CS_PORT, ADC_CS_PIN);
    delay_cycles(200);
    pin_set(ADC_CS_PORT, ADC_CS_PIN);
    s_t0 = millis();
    s_state = ST_CONVERTING;
}

static uint32_t read24(void)
{
    uint32_t v = 0;

    for (int i = 0; i < 24; i++) {
        pin_clr(ADC_SCK_PORT, ADC_SCK_PIN);     /* ADC shifts the next bit out */
        delay_cycles(4);
        pin_set(ADC_SCK_PORT, ADC_SCK_PIN);
        delay_cycles(4);
        v = (v << 1) | pin_read(ADC_SDO_PORT, ADC_SDO_PIN);
    }
    return v;
}

static void convert(uint32_t raw, pt_result_t *res)
{
    int32_t code;
    uint64_t r;

    if (raw & 0xC00000UL) {                     /* OVL (bit 23) / OVH (bit 22) */
        res->status = PT_OVERFLOW;
        res->code = 0;
        res->r_milliohm = 0;
        return;
    }
    code = (int32_t)(raw << 10) >> 10;          /* sign-extend 22 bits */
    res->code = code;
    if (code <= 0 || code >= (1L << 21)) {
        res->status = PT_RANGE;
        res->r_milliohm = 0;
        return;
    }
    /* range check in 64 bit: near full scale (open sensor) the quotient exceeds 32 bit */
    r = ((uint64_t)g_set.rref_milliohm * (uint32_t)code) / (uint32_t)((1L << 21) - code);
    if (r < R_MIN_MILLIOHM || r > R_MAX_MILLIOHM) {
        res->status = PT_RANGE;
        res->r_milliohm = (r > INT32_MAX) ? INT32_MAX : (uint32_t)r;   /* printed as int32 */
        return;
    }
    res->r_milliohm = (uint32_t)r;
    res->temp_cdeg = pt1000_to_centidegrees(res->r_milliohm);
    res->status = PT_OK;
}

int pt_poll(pt_result_t *res)
{
    switch (s_state) {
    case ST_IDLE:
        return 0;

    case ST_CONVERTING:
        if ((millis() - s_t0) >= CONV_WAIT_MS) {
            pin_clr(ADC_CS_PORT, ADC_CS_PIN);   /* SDO now shows /RDY */
            s_t0 = millis();
            s_state = ST_WAIT_READY;
        }
        return 0;

    case ST_WAIT_READY:
        delay_cycles(50);
        if (!pin_read(ADC_SDO_PORT, ADC_SDO_PIN)) {
            uint32_t raw = read24();
            pin_set(ADC_CS_PORT, ADC_CS_PIN);
            res->raw = raw;
            res->ready_ms = millis() - s_t0;
            convert(raw, res);
            s_state = ST_IDLE;
            return 1;
        }
        if ((millis() - s_t0) >= READY_TIMEOUT_MS) {
            pin_set(ADC_CS_PORT, ADC_CS_PIN);
            res->raw = 0;
            res->ready_ms = millis() - s_t0;
            res->status = PT_TIMEOUT;
            res->code = 0;
            res->r_milliohm = 0;
            s_state = ST_IDLE;
            return 1;
        }
        return 0;
    }
    return 0;
}

int32_t pt1000_to_centidegrees(uint32_t r_milliohm)
{
    const float A = 3.9083e-3f, B = -5.775e-7f, C = -4.183e-12f;
    float ratio = (float)r_milliohm / 1000000.0f;          /* R / R0, R0 = 1000 Ohm */
    float t = (-A + sqrtf(A * A - 4.0f * B * (1.0f - ratio))) / (2.0f * B);

    if (t < 0.0f) {
        /* Below 0 degC the C term applies: refine with Newton iterations */
        for (int i = 0; i < 4; i++) {
            float f = 1.0f + A * t + B * t * t + C * (t - 100.0f) * t * t * t - ratio;
            float df = A + 2.0f * B * t + C * (4.0f * t * t * t - 300.0f * t * t);
            t -= f / df;
        }
    }
    t *= 100.0f;
    return (int32_t)(t < 0.0f ? t - 0.5f : t + 0.5f);
}
