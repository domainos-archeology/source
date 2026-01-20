/*
 * smd/smd_internal.h - Screen Management Display Module Internal Definitions
 *
 * Internal data structures and functions for the SMD subsystem.
 * SMD manages display hardware, cursors, fonts, and screen operations.
 *
 * Memory layout (m68k):
 *   - SMD globals:        0x00E82B8C
 *   - Display unit array: 0x00E2E3FC (each unit is 0x10C bytes)
 *   - Display info table: 0x00E27376 (each entry is 0x60 bytes)
 *   - Event counts:       0x00E2E3FC, 0x00E2E408
 */

#ifndef SMD_INTERNAL_H
#define SMD_INTERNAL_H

#include "smd/smd.h"
#include "ec/ec.h"
#include "proc1/proc1.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Maximum number of display units */
#define SMD_MAX_DISPLAY_UNITS       4

/* Display unit structure size */
#define SMD_DISPLAY_UNIT_SIZE       0x10C   /* 268 bytes */

/* Display info entry size */
#define SMD_DISPLAY_INFO_SIZE       0x60    /* 96 bytes */

/* Maximum ASIDs supported */
#define SMD_MAX_ASIDS               256

/* Maximum HDM free list entries */
#define SMD_HDM_MAX_ENTRIES         25

/* Tracking rectangle list size */
#define SMD_MAX_TRACKING_RECTS      16

/*
 * ============================================================================
 * Display Types
 * ============================================================================
 * Display type codes returned by SMD_$INQ_DISP_TYPE
 */
#define SMD_DISP_TYPE_MONO_LANDSCAPE    1   /* 1024x800 landscape */
#define SMD_DISP_TYPE_MONO_PORTRAIT     2   /* 800x1024 portrait */
#define SMD_DISP_TYPE_COLOR_1024x2048   3   /* Color 1024x2048 */
#define SMD_DISP_TYPE_COLOR_1024x2048_B 4   /* Color 1024x2048 variant */
#define SMD_DISP_TYPE_HI_RES_2048x1024  5   /* Hi-res 2048x1024 */
#define SMD_DISP_TYPE_MONO_1024x1024_A  6   /* Mono 1024x1024 */
#define SMD_DISP_TYPE_MONO_1024x1024_B  8   /* Mono 1024x1024 variant */
#define SMD_DISP_TYPE_HI_RES_2048x1024_B 9  /* Hi-res variant */
#define SMD_DISP_TYPE_MONO_1024x1024_C  10  /* Mono 1024x1024 variant */
#define SMD_DISP_TYPE_MONO_1024x1024_D  11  /* Mono 1024x1024 variant */

/*
 * ============================================================================
 * Status Codes (module 0x13)
 * ============================================================================
 */
#define status_$display_invalid_unit_number                 0x00130001
#define status_$display_invalid_use_of_driver_procedure     0x00130004
#define status_$display_error_unloading_internal_table      0x00130006
#define status_$display_invalid_position_argument           0x00130015
#define status_$display_invalid_blt_mode_register           0x0013001A
#define status_$display_invalid_blt_control_register        0x0013001B
#define status_$display_invalid_screen_coordinates_in_blt   0x0013001E
#define status_$display_memory_not_mapped                   0x00130021
#define status_$display_hidden_display_memory_full          0x00130024
#define status_$display_invalid_blt_op                      0x00130028
#define status_$display_nonconforming_blts_unsupported      0x00130028  /* Same as invalid_blt_op */
#define status_$display_tracking_list_full                  0x00130031

/*
 * ============================================================================
 * Lock States
 * ============================================================================
 * Display lock state machine values
 */
#define SMD_LOCK_STATE_UNLOCKED     0
#define SMD_LOCK_STATE_LOCKED_REG   1   /* Locked by regular caller */
#define SMD_LOCK_STATE_SCROLL       3   /* Scroll operation in progress */
#define SMD_LOCK_STATE_LOCKED_4     4   /* Post-scroll lock state */
#define SMD_LOCK_STATE_LOCKED_5     5   /* Initial lock state */

/*
 * ============================================================================
 * Display Hardware Info Structure
 * ============================================================================
 * Per-display hardware state and parameters.
 * Pointed to from display_unit_t at offset +0x18 (-0xF4 from end).
 * Size: approximately 0x60 bytes
 */
typedef struct smd_display_hw_t {
    uint16_t    display_type;       /* 0x00: Display type code */
    uint16_t    lock_state;         /* 0x02: Current lock state */
    ec_$eventcount_t lock_ec;       /* 0x04: Lock event count (12 bytes) */
    ec_$eventcount_t op_ec;         /* 0x10: Operation complete event count */
    uint32_t    field_1c;           /* 0x1C: Unknown (cleared in init) */
    uint8_t     field_20;           /* 0x20: Unknown byte flag */
    uint8_t     pad_21;             /* 0x21: Padding */
    uint16_t    video_flags;        /* 0x22: Video control flags */
                                    /*       bit 0: video enable */
    uint16_t    field_24;           /* 0x24: Unknown */
    /* Scroll parameters */
    uint16_t    scroll_x1;          /* 0x26: Scroll region x1 */
    uint16_t    scroll_y1;          /* 0x28: Scroll region y1 */
    uint16_t    scroll_x2;          /* 0x2A: Scroll region x2 */
    uint16_t    scroll_y2;          /* 0x2C: Scroll region y2 */
    uint16_t    scroll_dy;          /* 0x2E: Scroll delta y */
    uint16_t    scroll_dx;          /* 0x30: Scroll delta x */
    uint16_t    field_32;           /* 0x32: Unknown */
    uint16_t    field_34;           /* 0x34: Unknown */
    uint16_t    field_36;           /* 0x36: Unknown */
    uint16_t    field_38;           /* 0x38: Unknown */
    uint16_t    field_3a;           /* 0x3A: Unknown */
    uint8_t     tracking_enabled;   /* 0x3C: Tracking mouse enabled */
    uint8_t     pad_3d;             /* 0x3D: Padding */
    uint8_t     field_3e;           /* 0x3E: Unknown byte */
    uint8_t     pad_3f;             /* 0x3F: Padding */
    ec_$eventcount_t cursor_ec;     /* 0x40: Cursor event count */
    uint16_t    field_4c;           /* 0x4C: Unknown */
    uint16_t    field_4e;           /* 0x4E: Unknown (cleared in init) */
    uint16_t    height;             /* 0x50: Display height - 1 */
    uint16_t    field_52;           /* 0x52: Unknown (cleared in init) */
    uint16_t    width;              /* 0x54: Display width - 1 */
    uint16_t    field_56;           /* 0x56: Unknown */
    uint16_t    field_58;           /* 0x58: Unknown */
    uint16_t    field_5a;           /* 0x5A: Unknown */
    uint16_t    field_5c;           /* 0x5C: Unknown */
    uint16_t    field_5e;           /* 0x5E: Unknown (cleared in init) */
} smd_display_hw_t;

/*
 * ============================================================================
 * HDM (Hidden Display Memory) Free Block Entry
 * ============================================================================
 * Tracks free regions of off-screen display memory.
 * Size: 4 bytes
 */
typedef struct smd_hdm_block_t {
    uint16_t    offset;             /* 0x00: Start offset in HDM */
    uint16_t    size;               /* 0x02: Size of free block */
} smd_hdm_block_t;

/*
 * ============================================================================
 * HDM Allocation List
 * ============================================================================
 * Header for the hidden display memory free list.
 */
typedef struct smd_hdm_list_t {
    uint16_t    count;              /* 0x00: Number of free blocks */
    uint16_t    pad;                /* 0x02: Padding */
    smd_hdm_block_t blocks[1];      /* 0x04: Variable-length array of blocks */
} smd_hdm_list_t;

/*
 * ============================================================================
 * Display Unit Structure
 * ============================================================================
 * Per-display unit state. Each unit is 0x10C bytes.
 * Base address: 0x00E2E3FC
 *
 * IMPORTANT: The original code uses 1-based unit numbers in API calls.
 * When accessing data, offsets are computed from (base + unit * 0x10c).
 * This means some fields are accessed with negative offsets (from the
 * "previous" slot). The layout below reflects logical organization.
 *
 * For unit N (1-based), accessed offsets from (base + N*0x10c):
 *   -0xf4: hw pointer (in slot N-1)
 *   -0xe8 + ASID*4: mapped_addresses[ASID] (in slot N-1)
 *   +0x04: hdm_list_ptr (in slot N)
 *   +0x0c: UID for MST mapping (in slot N)
 */
typedef struct smd_display_unit_t {
    ec_$eventcount_t    event_count_1;  /* 0x00: Event count (12 bytes) */
    smd_hdm_list_t      *hdm_list_ptr;  /* 0x0C: Pointer to HDM free list */
    uint16_t            field_10;       /* 0x10: Unknown */
    uint16_t            asid;           /* 0x12: Associated address space ID */
    uint16_t            field_14;       /* 0x14: Unknown */
    uint16_t            field_16;       /* 0x16: Unknown */
    smd_display_hw_t    *hw;            /* 0x18: Pointer to hardware info */
    uint32_t            field_1c;       /* 0x1C: Unknown */
    uint32_t            field_20;       /* 0x20: Unknown */
    uint32_t            mapped_addresses[58]; /* 0x24: Per-ASID mapped display addresses */
                                        /* 58 = MST_MAX_ASIDS from mst.h */
                                        /* 58 * 4 = 0xe8 bytes, ends at 0x10c */
} smd_display_unit_t;

/*
 * ============================================================================
 * Display Info Entry
 * ============================================================================
 * Per-display configuration. Each entry is 0x60 bytes.
 * Base address: 0x00E27376
 */
typedef struct smd_display_info_t {
    uint16_t    display_type;       /* 0x00: Display type code */
    uint16_t    field_02;           /* 0x02: Unknown */
    uint16_t    field_04;           /* 0x04: Unknown */
    uint16_t    field_06;           /* 0x06: Unknown */
    uint16_t    field_08;           /* 0x08: Unknown */
    uint16_t    field_0a;           /* 0x0A: Unknown */
    /* Clipping window - default bounds */
    int16_t     clip_x1_default;    /* 0x0C: Default clip x1 */
    int16_t     clip_y1_default;    /* 0x0E: Default clip y1 */
    int16_t     clip_x2_default;    /* 0x10: Default clip x2 */
    int16_t     clip_y2_default;    /* 0x12: Default clip y2 */
    /* Clipping window - current bounds */
    int16_t     clip_x1;            /* 0x14: Current clip x1 */
    int16_t     clip_y1;            /* 0x16: Current clip y1 */
    int16_t     clip_x2;            /* 0x18: Current clip x2 */
    int16_t     clip_y2;            /* 0x1A: Current clip y2 */
    uint8_t     pad[0x44];          /* 0x1C-0x5F: Remaining fields */
} smd_display_info_t;

/*
 * ============================================================================
 * SMD Globals Structure
 * ============================================================================
 * Global state for the SMD subsystem.
 * Base address: 0x00E82B8C
 */
typedef struct smd_globals_t {
    uint8_t     pad_00[0x48];           /* 0x00-0x47: Unknown */
    uint16_t    asid_to_unit[SMD_MAX_ASIDS]; /* 0x48: ASID to display unit map */
                                        /* Each ASID maps to a display unit number */
    uint8_t     pad_248[0x80];          /* 0x248-0x2C7: Unknown */
    uint32_t    blank_time;             /* 0xC8: Time when blanking occurred */
                                        /*       (TIME_$CLOCKH value) */
    uint8_t     pad_cc[0x11];           /* 0xCC-0xDC: Unknown */
    int8_t      blank_enabled;          /* 0xDD: Blanking enabled flag */
                                        /*       negative = blanked */
    uint8_t     pad_de[0x1ABA];         /* 0xDE-0x1D97: Unknown */
    uint16_t    default_unit;           /* 0x1D98: Default display unit */
} smd_globals_t;

/*
 * ============================================================================
 * BLT (Bit Block Transfer) Parameters
 * ============================================================================
 * Parameters for SMD_$BLT operations.
 */
typedef struct smd_blt_params_t {
    uint16_t    flags;              /* 0x00: Operation flags */
                                    /*       bit 7: sign bit (direction) */
                                    /*       bit 6: invalid op */
                                    /*       bit 5: use alternate rop */
                                    /*       bit 4: async operation */
                                    /*       bit 3: invalid op */
                                    /*       bit 2: mask enable */
                                    /*       bit 1: src enable */
                                    /*       bit 0: dest enable */
    uint8_t     rop_mode;           /* 0x02: ROP mode byte */
    uint8_t     pattern;            /* 0x03: Pattern byte */
    uint32_t    reserved;           /* 0x04: Reserved */
    uint16_t    src_x;              /* 0x08: Source X */
    uint16_t    src_y;              /* 0x0A: Source Y */
    uint16_t    dst_x;              /* 0x0C: Destination X */
    uint16_t    dst_y;              /* 0x0E: Destination Y */
    uint16_t    width;              /* 0x10: Width */
    uint16_t    height;             /* 0x12: Height (low nibble: plane) */
} smd_blt_params_t;

/*
 * ============================================================================
 * Cursor Blink State
 * ============================================================================
 * State for cursor blinking.
 * Base address: 0x00E273D6
 */
typedef struct smd_blink_state_t {
    uint8_t     smd_time_com;       /* 0x00: Time communication flag */
    uint8_t     pad_01;             /* 0x01: Padding */
    uint8_t     blink_flag;         /* 0x02: Blink state (0xFF = enabled) */
    uint8_t     pad_03;             /* 0x03: Padding */
    uint16_t    blink_counter;      /* 0x04: Blink counter */
} smd_blink_state_t;

/*
 * ============================================================================
 * External Data Declarations
 * ============================================================================
 */

/* SMD globals at 0x00E82B8C */
extern smd_globals_t SMD_GLOBALS;

/* Display unit array at 0x00E2E3FC */
extern smd_display_unit_t SMD_DISPLAY_UNITS[];

/* Display info table at 0x00E27376 */
extern smd_display_info_t SMD_DISPLAY_INFO[];

/* Event counts at 0x00E2E3FC, 0x00E2E408 */
extern ec_$eventcount_t SMD_EC_1;
extern ec_$eventcount_t SMD_EC_2;

/* Blink state at 0x00E273D6 */
extern smd_blink_state_t SMD_BLINK_STATE;

/* Default display unit at 0x00E84924 */
extern uint16_t SMD_DEFAULT_DISPLAY_UNIT;

/* TIME_$CLOCKH - high word of system clock */
extern uint32_t TIME_$CLOCKH;

/*
 * ============================================================================
 * Internal Function Prototypes
 * ============================================================================
 */

/*
 * SMD_$ACQ_DISPLAY - Acquire display lock
 *
 * Acquires exclusive access to the display for the calling process.
 * Blocks if another process holds the lock.
 *
 * Parameters:
 *   lock_data - Pointer to lock-specific data
 *
 * Returns:
 *   Lock acquisition result
 *
 * Original address: 0x00E6EB42
 */
uint16_t SMD_$ACQ_DISPLAY(void *lock_data);

/*
 * SMD_$REL_DISPLAY - Release display lock
 *
 * Releases the display lock acquired by SMD_$ACQ_DISPLAY.
 *
 * Original address: 0x00E6EC10
 */
void SMD_$REL_DISPLAY(void);

/*
 * SMD_$START_SCROLL - Start scroll operation
 *
 * Initiates a hardware scroll operation.
 *
 * Parameters:
 *   hw - Display hardware info
 *   ec - Event count for completion signaling
 *
 * Original address: 0x00E272A8
 */
void SMD_$START_SCROLL(smd_display_hw_t *hw, ec_$eventcount_t *ec);

/*
 * SMD_$START_BLT - Start BLT operation
 *
 * Initiates a hardware BLT operation.
 *
 * Parameters:
 *   params - BLT parameters
 *   hw - Display hardware info
 *   ec - Event count for completion signaling
 *
 * Original address: 0x00E272BC
 */
void SMD_$START_BLT(smd_blt_params_t *params, smd_display_hw_t *hw, ec_$eventcount_t *ec);

/*
 * SMD_$INTERRUPT_INIT - Initialize SMD interrupt handling
 *
 * Sets up interrupt handlers for display hardware.
 *
 * Original address: 0x00E27284
 */
void SMD_$INTERRUPT_INIT(void);

/*
 * SMD_$WRITE_STR_CLIP - Write string with clipping
 *
 * Internal string writing function that respects clipping bounds.
 *
 * Original address: 0x00E8493E
 */
void SMD_$WRITE_STR_CLIP(uint32_t *pos, void *font, void *buffer,
                         uint16_t *length, void *param5, status_$t *status_ret);

/*
 * smd_$is_valid_blt_ctl - Validate BLT control register value
 *
 * Checks if a BLT control register contains one of the valid magic values.
 *
 * Parameters:
 *   ctl_reg - Control register value to validate
 *
 * Returns:
 *   0xFF (-1) if valid, 0 if invalid
 *
 * Valid values are: 0x02020020, 0x02020060, 0x06060020, 0x06060060
 *
 * Original address: 0x00E6FAA8
 */
uint8_t smd_$is_valid_blt_ctl(uint32_t ctl_reg);

/*
 * Helper to get display unit pointer from unit number
 */
static inline smd_display_unit_t *smd_get_unit(uint16_t unit_num) {
    /* Base address + unit_num * unit_size */
    return &SMD_DISPLAY_UNITS[unit_num];
}

/*
 * Helper to get display info pointer from unit number
 */
static inline smd_display_info_t *smd_get_info(uint16_t unit_num) {
    return &SMD_DISPLAY_INFO[unit_num];
}

/*
 * Helper to get current process's display unit
 */
static inline uint16_t smd_get_current_unit(void) {
    return SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID];
}

#endif /* SMD_INTERNAL_H */
