#ifndef CALIB_H
#define CALIB_H

#include <stddef.h>
#include <stdint.h>

/*
 * Persistent settings, identity and calibration (data EEPROM, 0x08080100 + copy at 0x08080140,
 * CRC protected).
 *
 * Reference calibration without disconnecting the PT1000 ("parallel method"):
 *   A: sensor alone                      a = R_pt / R_ref
 *   B: known R_cal clipped across PT1000  b = (R_pt || R_cal) / R_ref
 *   => R_ref = R_cal * (1/b - 1/a)
 * A is captured before and after B; both are averaged to cancel slow drift.
 */

#define IDENT_LABEL_REGS    7U      /* holding 533..539 */

typedef struct {
    uint32_t rref_milliohm;     /* reference resistor, nominal 10 000 000 */
    int16_t offset_cdeg;        /* temperature offset, 0.01 degC */
    uint16_t rcal_deciohm;      /* calibration resistor for the parallel method, 0.1 Ohm */
    uint16_t filter;            /* 0 = off, 1 = median of 3 + short glitch hold */
    uint16_t interval_ms;       /* measurement interval */
    uint16_t keep_last_good;    /* 1 = reg 50 keeps last good value when invalid */
    uint16_t debug;             /* debug trace on USART5 */
    uint16_t reserved;          /* unused */
    /* identity, not touched by CAL_CMD_DEFAULTS */
    uint16_t asset;             /* user number, holding 532 (0 = not set) */
    uint16_t label[IDENT_LABEL_REGS];   /* 14 ASCII chars, 2 per register, high byte first, NUL padded */
    uint16_t reserved2;         /* makes the size a multiple of 4: no hidden padding */
} settings_t;

_Static_assert(sizeof(settings_t) == 36, "settings_t must not contain padding");

extern settings_t g_set;

/* Calibration state machine (holding register 531) */
enum {
    CAL_IDLE = 0,
    CAL_CAPTURING_A = 1,
    CAL_CAPTURING_B = 2,
    CAL_CAPTURED = 3,           /* last capture finished */
    CAL_COMPUTED = 4,           /* new R_ref applied in RAM, not saved */
    CAL_SAVED = 5,
    CAL_ERR_NO_A = 0x8001,
    CAL_ERR_NO_B = 0x8002,
    CAL_ERR_IMPLAUSIBLE = 0x8003,
    CAL_ERR_EEPROM = 0x8004,
    CAL_ERR_MEASUREMENT = 0x8005,
    CAL_ERR_BAD_COMMAND = 0x8006
};

/* Commands (write to holding register 530) */
#define CAL_CMD_CAPTURE_A   1U      /* sensor alone (repeat after B) */
#define CAL_CMD_CAPTURE_B   2U      /* with R_cal across the PT1000 */
#define CAL_CMD_COMPUTE     3U      /* R_ref from the captures */
#define CAL_CMD_CLEAR       4U      /* drop captures */
#define CAL_CMD_SAVE        0x5AFEU /* write settings to EEPROM */
#define CAL_CMD_DEFAULTS    0xDEF0U /* factory defaults into RAM (not saved); keeps the identity */
#define CAL_CMD_RELOAD      0x10ADU /* reload settings from EEPROM */

void calib_init(void);                  /* load settings (defaults if EEPROM invalid) */
int calib_loaded_from_eeprom(void);
int calib_dirty(void);
void calib_mark_dirty(void);

void calib_command(uint16_t cmd);
uint16_t calib_state(void);
uint16_t calib_last_command(void);
int calib_capturing(void);              /* 1 while A/B captures run (fast measurements) */
void calib_feed(int ok, int32_t code);  /* every finished conversion while capturing */

#endif /* CALIB_H */
