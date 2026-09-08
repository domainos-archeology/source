/*
 * tpad/tpad.h - Trackpad/Pointing Device Module Public API
 *
 * Provides support for various pointing devices on Apollo workstations:
 *   - Touchpad (resistive touch surface)
 *   - Bitpad (digitizer tablet)
 *   - Mouse (relative motion device)
 *
 * The module handles coordinate translation, scaling, acceleration,
 * and mode selection (absolute vs relative positioning).
 *
 * Original addresses: 0x00E33570 - 0x00E69B7A
 * Data area: 0x00E8245C - 0x00E825DF
 */

#ifndef TPAD_H
#define TPAD_H

#include "base/base.h"

/*
 * ============================================================================
 * Device Type Enumeration
 * ============================================================================
 * Identifies the type of pointing device currently detected.
 */
typedef enum tpad_$dev_type_t {
    tpad_$unknown       = 0,    /* No device detected or unknown type */
    tpad_$have_touchpad = 1,    /* Resistive touchpad */
    tpad_$have_mouse    = 2,    /* Mouse with relative motion */
    tpad_$have_bitpad   = 3     /* Digitizer tablet (bitpad) */
} tpad_$dev_type_t;

/*
 * ============================================================================
 * Operating Mode Enumeration
 * ============================================================================
 * Determines how raw device coordinates are translated to cursor position.
 */
typedef enum tpad_$mode_t {
    tpad_$absolute  = 0,    /* Absolute positioning - cursor follows device exactly */
    tpad_$relative  = 1,    /* Relative positioning - cursor moves by deltas */
    tpad_$scaled    = 2     /* Scaled absolute - device range maps to display range */
} tpad_$mode_t;

/*
 * ============================================================================
 * Position Type
 * ============================================================================
 * Represents a screen position as used by TPAD and SMD subsystems.
 * Note: Stored as (y, x) in memory for big-endian 32-bit access.
 */
typedef union smd_$pos_t {
    int32_t raw;            /* 32-bit combined value */
    struct {
        int16_t y;          /* Y coordinate (offset 0) */
        int16_t x;          /* X coordinate (offset 2) */
    };
} smd_$pos_t;

/*
 * ============================================================================
 * Pointing-Device Data Packet
 * ============================================================================
 * One entry of the terminal driver's tpad queue.  TERM_$ENQUEUE_TPAD hands
 * TPAD_$DATA the address of a 16-byte slot: 0x00E7248E "lsl.w #0x4,D0w" then
 * 0x00E72490 "pea (0x4,A3,D0w*0x1)".
 *
 * Field evidence, all from TPAD_$DATA's own accesses:
 *   0x00  elapsed     `cmpi.l #0x1e848,(A1)` 0x00E6951C and
 *                     `cmpi.l #0x7a12,(A1)` 0x00E695D6, both with UNSIGNED
 *                     branches (bls / shi); cleared with `clr.l (A1)` at
 *                     0x00E6975E once the packet has been consumed.
 *   0x04  clock_high  `move.l (0x4,A1),(0x164,A5)` 0x00E6967E
 *   0x08  clock_low   `move.w (0x8,A1),(0x168,A5)` 0x00E69684
 *                     Together the 48-bit Domain clock: its low 32 bits are
 *                     read as one longword at +0x06 (0x00E69640).
 *   0x0A  dev_id      `move.b (0xa,A1),D0b` 0x00E691DC
 *   0x0B  flags       `and.b (0xb,A1),..`   0x00E691F0 / 0x00E6921C /
 *                     0x00E692A4 / 0x00E693BA / 0x00E693DC
 *   0x0C  b0          `move.b (0xc,A1),..`  0x00E69228 (mouse dx),
 *                     0x00E69372 (bitpad x low), 0x00E693E0 (touchpad)
 *   0x0D  b1          `move.b (0xd,A1),..`  0x00E692AC (mouse dy),
 *                     0x00E6936E (bitpad x high), 0x00E693F6 (touchpad)
 *   0x0E  b2          `move.b (0xe,A1),D5b` 0x00E69386 (bitpad y low)
 *   0x0F  b3          `move.b (0xf,A1),D3b` 0x00E69382 (bitpad y high)
 *
 * Packed so the 48-bit clock keeps its six-byte m68k layout on a host where
 * a uint32/uint16 pair would otherwise be padded to eight.
 */
typedef struct __attribute__((packed)) tpad_$data_packet_t {
    uint32_t    elapsed;        /* 0x00: microseconds since the last packet */
    uint32_t    clock_high;     /* 0x04: upper 32 bits of the 48-bit clock */
    uint16_t    clock_low;      /* 0x08: lower 16 bits of the 48-bit clock */
    uint8_t     dev_id;         /* 0x0a: 0xDF mouse, 0x01 bitpad, else touchpad */
    uint8_t     flags;          /* 0x0b: buttons / overflow bits */
    uint8_t     b0;             /* 0x0c: device-specific data byte */
    uint8_t     b1;             /* 0x0d: device-specific data byte */
    uint8_t     b2;             /* 0x0e: device-specific data byte */
    uint8_t     b3;             /* 0x0f: device-specific data byte */
} tpad_$data_packet_t;

_Static_assert(__builtin_offsetof(tpad_$data_packet_t, clock_high) == 0x04,
               "tpad_$data_packet_t.clock_high");
_Static_assert(__builtin_offsetof(tpad_$data_packet_t, clock_low) == 0x08,
               "tpad_$data_packet_t.clock_low");
_Static_assert(__builtin_offsetof(tpad_$data_packet_t, dev_id) == 0x0A,
               "tpad_$data_packet_t.dev_id");
_Static_assert(__builtin_offsetof(tpad_$data_packet_t, flags) == 0x0B,
               "tpad_$data_packet_t.flags");
_Static_assert(__builtin_offsetof(tpad_$data_packet_t, b0) == 0x0C,
               "tpad_$data_packet_t.b0");
_Static_assert(__builtin_offsetof(tpad_$data_packet_t, b3) == 0x0F,
               "tpad_$data_packet_t.b3");
_Static_assert(sizeof(tpad_$data_packet_t) == 0x10,
               "tpad_$data_packet_t is one 16-byte queue slot");

/*
 * ============================================================================
 * Per-Unit Device Configuration
 * ============================================================================
 * Each display unit can have independent pointing device settings.
 * Size: 44 bytes (0x2c)
 */
typedef struct tpad_$unit_config_t {
    int32_t     sample_count;       /* 0x00: Samples collected for auto-ranging */
    int16_t     mode;               /* 0x04: Operating mode (tpad_$mode_t) */
    int16_t     x_scale;            /* 0x06: X axis scaling factor */
    int16_t     y_scale;            /* 0x08: Y axis scaling factor */
    int16_t     x_range;            /* 0x0a: X raw coordinate range */
    int16_t     y_range;            /* 0x0c: Y raw coordinate range */
    int16_t     x_min;              /* 0x0e: X minimum raw value (auto-ranging) */
    int16_t     y_min;              /* 0x10: Y minimum raw value (auto-ranging) */
    int16_t     x_min_disp;         /* 0x12: X minimum display boundary */
    int16_t     x_max_disp;         /* 0x14: X maximum display boundary */
    int16_t     y_min_disp;         /* 0x16: Y minimum display boundary */
    int16_t     y_max_disp;         /* 0x18: Y maximum display boundary */
    int16_t     hysteresis;         /* 0x1a: Movement threshold for noise filtering */
    int16_t     x_factor;           /* 0x1c: Computed X conversion factor */
    int16_t     y_factor;           /* 0x1e: Computed Y conversion factor */
    int16_t     cursor_offset_y;    /* 0x20: Y offset between cursor and raw position */
    int16_t     cursor_offset_x;    /* 0x22: X offset between cursor and raw position */
    smd_$pos_t  origin;             /* 0x24: Origin point for relative mode */
    int16_t     punch_impact;       /* 0x28: Edge detection threshold */
    int16_t     _pad;               /* 0x2a: Padding for alignment */
} tpad_$unit_config_t;

/*
 * ============================================================================
 * Global TPAD State
 * ============================================================================
 * Contains current state of the pointing device subsystem.
 * Located at offset 0x160 from the per-unit config array base.
 */
typedef struct tpad_$globals_t {
    int16_t         cursor_y;       /* 0x00: Current cursor Y position */
    int16_t         cursor_x;       /* 0x02: Current cursor X position */
    clock_t         last_clock;     /* 0x04: Full 48-bit timestamp (6 bytes) */
    int16_t         touchpad_max;   /* 0x0a: Touchpad coordinate maximum (1500 default) */
    int16_t         dev_type;       /* 0x0c: Current device type (tpad_$dev_type_t) */
    int16_t         raw_y;          /* 0x0e: Raw Y coordinate from device */
    int16_t         raw_x;          /* 0x10: Raw X coordinate from device */
    int16_t         button_state;   /* 0x12: Current button/stylus state */
    int16_t         delta_y;        /* 0x14: Y movement delta */
    int16_t         delta_x;        /* 0x16: X movement delta */
    int16_t         accum_y;        /* 0x18: Accumulated Y fractional movement */
    int16_t         accum_x;        /* 0x1a: Accumulated X fractional movement */
    int16_t         unit;           /* 0x1c: Current display unit number */
    int8_t          re_origin_flag; /* 0x1e: Flag to re-establish origin on next packet */
    int8_t          _pad;           /* 0x1f: Padding */
} tpad_$globals_t;

/*
 * The record is 0x20 bytes with the 48-bit clock at +0x04: TPAD_$DATA reads
 * it back as `move.l (0x164,A5),D3` / `move.w (0x168,A5),D4w` and stores it
 * with `move.l (0x4,A1),(0x164,A5)` / `move.w (0x8,A1),(0x168,A5)`, i.e. the
 * globals sit at +0x160 of the config array and last_clock is six bytes.
 * Checked on every target now that clock_t carries a packed spelling.
 * (source-no75)
 */
_Static_assert(sizeof(tpad_$globals_t) == 0x20, "tpad_$globals_t size");
_Static_assert(__builtin_offsetof(tpad_$globals_t, cursor_y)     == 0x00, "tpad_globals.cursor_y");
_Static_assert(__builtin_offsetof(tpad_$globals_t, cursor_x)     == 0x02, "tpad_globals.cursor_x");
_Static_assert(__builtin_offsetof(tpad_$globals_t, last_clock)   == 0x04, "tpad_globals.last_clock");
_Static_assert(__builtin_offsetof(tpad_$globals_t, touchpad_max) == 0x0A, "tpad_globals.touchpad_max");
_Static_assert(__builtin_offsetof(tpad_$globals_t, dev_type)     == 0x0C, "tpad_globals.dev_type");
_Static_assert(__builtin_offsetof(tpad_$globals_t, raw_y)        == 0x0E, "tpad_globals.raw_y");
_Static_assert(__builtin_offsetof(tpad_$globals_t, raw_x)        == 0x10, "tpad_globals.raw_x");
_Static_assert(__builtin_offsetof(tpad_$globals_t, button_state) == 0x12, "tpad_globals.button_state");
_Static_assert(__builtin_offsetof(tpad_$globals_t, delta_y)      == 0x14, "tpad_globals.delta_y");
_Static_assert(__builtin_offsetof(tpad_$globals_t, delta_x)      == 0x16, "tpad_globals.delta_x");
_Static_assert(__builtin_offsetof(tpad_$globals_t, accum_y)      == 0x18, "tpad_globals.accum_y");
_Static_assert(__builtin_offsetof(tpad_$globals_t, accum_x)      == 0x1A, "tpad_globals.accum_x");
_Static_assert(__builtin_offsetof(tpad_$globals_t, unit)         == 0x1C, "tpad_globals.unit");
_Static_assert(__builtin_offsetof(tpad_$globals_t, re_origin_flag) == 0x1E, "tpad_globals.re_origin_flag");

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */
#define TPAD_$MAX_UNITS             8       /* Maximum number of display units */
#define TPAD_$DEFAULT_CURSOR_Y      512     /* Default cursor Y (0x200) */
#define TPAD_$DEFAULT_CURSOR_X      400     /* Default cursor X (0x190) */
#define TPAD_$DEFAULT_TOUCHPAD_MAX  1500    /* Default touchpad max coordinate (0x5dc) */
#define TPAD_$FACTOR_DEFAULT        0x400   /* Default scale factor (1024) */
#define TPAD_$RANGING_SAMPLES       1000    /* Number of samples for auto-ranging */
#define TPAD_$RANGING_MARGIN        50      /* Margin for auto-ranging (0x32) */
#define TPAD_$INITIAL_RANGE         0x200   /* Initial coordinate range */
#define TPAD_$INITIAL_MIN           0x100   /* Initial minimum coordinate */

/* Mouse packet identifiers */
#define TPAD_$MOUSE_ID              0xDF    /* Mouse data packet identifier */
#define TPAD_$BITPAD_ID             0x01    /* Bitpad data packet identifier */

/* Bitpad/touchpad raw coordinate scaling */
#define TPAD_$BITPAD_SCALE          0x898   /* Bitpad raw to scaled divisor (2200) */
#define TPAD_$TOUCHPAD_INVERTED     0x1000  /* Touchpad inverted coordinate threshold */

/*
 * ============================================================================
 * Public Functions
 * ============================================================================
 */

/*
 * TPAD_$INIT - Initialize the pointing device subsystem
 *
 * Initializes all per-unit configurations based on display dimensions
 * and sets default global state. Called during system startup.
 *
 * Original address: 0x00E33570
 */
void TPAD_$INIT(void);

/*
 * TPAD_$DATA - Process pointing device data packet
 *
 * Called by the keyboard/input driver when a pointing device packet
 * is received. Processes the raw data and updates cursor position.
 *
 * Parameters:
 *   packet - Pointer to raw device data packet (format depends on device type)
 *
 * Original address: 0x00E691BC
 */
void TPAD_$DATA(tpad_$data_packet_t *packet);

/*
 * TPAD_$SET_MODE - Set pointing device mode
 *
 * Sets the operating mode and scaling parameters for the current unit.
 *
 * Parameters:
 *   new_modep    - Pointer to new mode value
 *   xsp          - Pointer to X scale factor
 *   ysp          - Pointer to Y scale factor
 *   hysteresisp  - Pointer to hysteresis value
 *   originp      - Pointer to origin position (for relative mode)
 *
 * Original address: 0x00E697BE
 */
void TPAD_$SET_MODE(tpad_$mode_t *new_modep, int16_t *xsp, int16_t *ysp,
                    int16_t *hysteresisp, smd_$pos_t *originp);

/*
 * TPAD_$SET_UNIT_MODE - Set mode for specific unit
 *
 * Parameters:
 *   unitp        - Pointer to unit number
 *   new_modep    - Pointer to new mode value
 *   xsp          - Pointer to X scale factor
 *   ysp          - Pointer to Y scale factor
 *   hysteresisp  - Pointer to hysteresis value
 *   originp      - Pointer to origin position
 *   status_ret   - Status return
 *
 * Original address: 0x00E697F0
 */
void TPAD_$SET_UNIT_MODE(int16_t *unitp, tpad_$mode_t *new_modep, int16_t *xsp,
                         int16_t *ysp, int16_t *hysteresisp, smd_$pos_t *originp,
                         status_$t *status_ret);

/*
 * TPAD_$SET_CURSOR - Set cursor position
 *
 * Provides feedback from the display manager to re-origin relative mode
 * when the DM sets a new cursor position. Called by smd_$move_kbd_cursor.
 *
 * Parameters:
 *   new_crsr - Pointer to new cursor position
 *
 * Original address: 0x00E698A0
 */
void TPAD_$SET_CURSOR(smd_$pos_t *new_crsr);

/*
 * TPAD_$SET_UNIT_CURSOR - Set cursor position for specific unit
 *
 * Parameters:
 *   unitp      - Pointer to unit number
 *   new_crsr   - Pointer to new cursor position
 *   status_ret - Status return
 *
 * Original address: 0x00E698C2
 */
void TPAD_$SET_UNIT_CURSOR(int16_t *unitp, smd_$pos_t *new_crsr, status_$t *status_ret);

/*
 * TPAD_$INQUIRE - Inquire current mode settings
 *
 * Returns the current mode settings so a program can save and restore them.
 *
 * Parameters:
 *   cur_modep    - Pointer to receive current mode
 *   xsp          - Pointer to receive X scale
 *   ysp          - Pointer to receive Y scale
 *   hysteresisp  - Pointer to receive hysteresis
 *   originp      - Pointer to receive origin
 *
 * Original address: 0x00E6993A
 */
void TPAD_$INQUIRE(tpad_$mode_t *cur_modep, int16_t *xsp, int16_t *ysp,
                   int16_t *hysteresisp, smd_$pos_t *originp);

/*
 * TPAD_$INQUIRE_UNIT - Inquire mode settings for specific unit
 *
 * Parameters:
 *   unitp        - Pointer to unit number
 *   cur_modep    - Pointer to receive current mode
 *   xsp          - Pointer to receive X scale
 *   ysp          - Pointer to receive Y scale
 *   hysteresisp  - Pointer to receive hysteresis
 *   originp      - Pointer to receive origin
 *   status_ret   - Status return
 *
 * Original address: 0x00E6996C
 */
void TPAD_$INQUIRE_UNIT(int16_t *unitp, tpad_$mode_t *cur_modep, int16_t *xsp,
                        int16_t *ysp, int16_t *hysteresisp, smd_$pos_t *originp,
                        status_$t *status_ret);

/*
 * TPAD_$SET_UNIT - Set current display unit
 *
 * Associates the pointing device with a display unit.
 *
 * Parameters:
 *   unitnum - Pointer to unit number
 *
 * Original address: 0x00E699DC
 */
void TPAD_$SET_UNIT(int16_t *unitnum);

/*
 * TPAD_$RE_RANGE - Re-establish touchpad coordinate range
 *
 * Initiates auto-ranging to recalibrate the touchpad coordinate range
 * over the next 1000 data points. Also done at system boot.
 *
 * Original address: 0x00E69A0E
 */
void TPAD_$RE_RANGE(void);

/*
 * TPAD_$RE_RANGE_UNIT - Re-range for specific unit
 *
 * Parameters:
 *   unitp      - Pointer to unit number
 *   status_ret - Status return
 *
 * Original address: 0x00E69A2C
 */
void TPAD_$RE_RANGE_UNIT(int16_t *unitp, status_$t *status_ret);

/*
 * TPAD_$INQ_DTYPE - Inquire device type
 *
 * Returns the last detected pointing device type.
 *
 * Returns:
 *   Device type (tpad_$dev_type_t)
 *
 * Original address: 0x00E69AC4
 */
tpad_$dev_type_t TPAD_$INQ_DTYPE(void);

/*
 * TPAD_$SET_PUNCH_IMPACT - Set edge impact threshold
 *
 * Sets the threshold for detecting edge impacts (stylus punches).
 *
 * Parameters:
 *   unitp      - Pointer to unit number
 *   impact     - Pointer to new impact threshold
 *   status_ret - Status return
 *
 * Returns:
 *   Previous impact threshold value
 *
 * Original address: 0x00E69ADC
 */
int16_t TPAD_$SET_PUNCH_IMPACT(int16_t *unitp, int16_t *impact, status_$t *status_ret);

/*
 * TPAD_$INQ_PUNCH_IMPACT - Inquire edge impact threshold
 *
 * Parameters:
 *   unitp      - Pointer to unit number
 *   impact     - Pointer to receive current threshold
 *   status_ret - Status return
 *
 * Original address: 0x00E69B2E
 */
void TPAD_$INQ_PUNCH_IMPACT(int16_t *unitp, int16_t *impact, status_$t *status_ret);

#endif /* TPAD_H */
