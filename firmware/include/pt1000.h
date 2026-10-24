#ifndef PT1000_H
#define PT1000_H

#include <stdint.h>

typedef enum {
    PT_OK = 0,
    PT_TIMEOUT,     /* MCP355x never signalled data ready */
    PT_OVERFLOW,    /* OVL/OVH bit set in the conversion result */
    PT_RANGE        /* resistance outside 500..3000 Ohm (open/short/bad read) */
} pt_status_t;

typedef struct {
    pt_status_t status;
    uint32_t raw;           /* raw 24-bit word from the ADC (OVL, OVH, 22-bit data) */
    uint32_t ready_ms;      /* time from conversion wait end to data ready */
    int32_t code;           /* signed 22-bit conversion result */
    uint32_t r_milliohm;    /* PT1000 resistance */
    int32_t temp_cdeg;      /* temperature in 0.01 degC (no offset), valid only if status == PT_OK */
} pt_result_t;

void pt_init(void);

/* Non-blocking measurement: start, then poll until it returns 1 (result filled in). */
void pt_start(void);
int pt_poll(pt_result_t *res);
int pt_busy(void);

/* Resistance -> 0.01 degC (Callendar-Van Dusen, IEC 60751) */
int32_t pt1000_to_centidegrees(uint32_t r_milliohm);

#endif /* PT1000_H */
