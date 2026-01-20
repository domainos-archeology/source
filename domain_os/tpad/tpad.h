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
void TPAD_$DATA(uint32_t *packet);

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
