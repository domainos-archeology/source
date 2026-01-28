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

#include "ec/ec.h"
#include "ml/ml.h"
#include "proc1/proc1.h"
#include "smd/smd.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Maximum number of display units */
#define SMD_MAX_DISPLAY_UNITS 4

/* Display unit structure size */
#define SMD_DISPLAY_UNIT_SIZE 0x10C /* 268 bytes */

/* Display info entry size */
#define SMD_DISPLAY_INFO_SIZE 0x60 /* 96 bytes */

/* Maximum ASIDs supported */
#define SMD_MAX_ASIDS 256

/* Maximum HDM free list entries */
#define SMD_HDM_MAX_ENTRIES 25

/* Tracking rectangle list size (max 200 rectangles) */
#define SMD_MAX_TRACKING_RECTS 200

/*
 * ============================================================================
 * Display Types
 * ============================================================================
 * Display type codes returned by SMD_$INQ_DISP_TYPE
 */
#define SMD_DISP_TYPE_MONO_LANDSCAPE 1     /* 1024x800 landscape */
#define SMD_DISP_TYPE_MONO_PORTRAIT 2      /* 800x1024 portrait */
#define SMD_DISP_TYPE_COLOR_1024x2048 3    /* Color 1024x2048 */
#define SMD_DISP_TYPE_COLOR_1024x2048_B 4  /* Color 1024x2048 variant */
#define SMD_DISP_TYPE_HI_RES_2048x1024 5   /* Hi-res 2048x1024 */
#define SMD_DISP_TYPE_MONO_1024x1024_A 6   /* Mono 1024x1024 */
#define SMD_DISP_TYPE_MONO_1024x1024_B 8   /* Mono 1024x1024 variant */
#define SMD_DISP_TYPE_HI_RES_2048x1024_B 9 /* Hi-res variant */
#define SMD_DISP_TYPE_MONO_1024x1024_C 10  /* Mono 1024x1024 variant */
#define SMD_DISP_TYPE_MONO_1024x1024_D 11  /* Mono 1024x1024 variant */

/*
 * ============================================================================
 * Status Codes (module 0x13)
 * ============================================================================
 */
#define status_$display_invalid_unit_number 0x00130001
#define status_$display_font_not_loaded 0x00130002
#define status_$display_internal_font_table_full 0x00130003
#define status_$display_invalid_use_of_driver_procedure 0x00130004
#define status_$display_error_unloading_internal_table 0x00130006
#define status_$display_unsupported_font_version 0x0013000B
#define status_$display_invalid_position_argument 0x00130015
#define status_$display_invalid_blt_mode_register 0x0013001A
#define status_$display_invalid_blt_control_register 0x0013001B
#define status_$display_invalid_screen_coordinates_in_blt 0x0013001E
#define status_$display_memory_not_mapped 0x00130021
#define status_$display_hidden_display_memory_full 0x00130024
#define status_$display_invalid_blt_op 0x00130028
#define status_$display_nonconforming_blts_unsupported                         \
  0x00130028 /* Same as invalid_blt_op */
#define status_$display_invalid_buffer_size 0x0013000C
#define status_$display_bad_tracking_rectangle 0x00130030
#define status_$display_tracking_list_full 0x00130031
#define status_$display_borrow_request_denied_by_screen_manager 0x00130010
#define status_$display_cant_return_not_borrowed 0x00130012
#define status_$display_already_borrowed_by_this_process 0x00130014
#define status_$display_invalid_scroll_displacement 0x00130019
#define status_$display_invalid_cursor_number 0x00130023

/*
 * ============================================================================
 * Lock States
 * ============================================================================
 * Display lock state machine values
 */
#define SMD_LOCK_STATE_UNLOCKED 0
#define SMD_LOCK_STATE_LOCKED_REG 1  /* Locked by regular caller */
#define SMD_LOCK_STATE_SCROLL 2      /* Scroll operation in progress */
#define SMD_LOCK_STATE_SCROLL_DONE 3 /* Scroll operation complete */
#define SMD_LOCK_STATE_LOCKED_4 4    /* Post-scroll lock state */
#define SMD_LOCK_STATE_LOCKED_5 5    /* Initial lock state */

/*
 * ============================================================================
 * Scroll Direction Constants
 * ============================================================================
 * Values for scroll_dx field indicating scroll direction
 */
#define SMD_SCROLL_DIR_DOWN 0  /* Scroll down (content moves up) */
#define SMD_SCROLL_DIR_UP 1    /* Scroll up (content moves down) */
#define SMD_SCROLL_DIR_RIGHT 2 /* Scroll right (content moves left) */
#define SMD_SCROLL_DIR_LEFT 3  /* Scroll left (content moves right) */

/*
 * ============================================================================
 * Scroll Rectangle Structure
 * ============================================================================
 * Defines the region to scroll for SMD_$SOFT_SCROLL.
 * Size: 8 bytes
 */
typedef struct smd_scroll_rect_t {
  uint16_t x1; /* 0x00: Left X coordinate */
  uint16_t y1; /* 0x02: Top Y coordinate */
  uint16_t x2; /* 0x04: Right X coordinate */
  uint16_t y2; /* 0x06: Bottom Y coordinate */
} smd_scroll_rect_t;

/*
 * ============================================================================
 * Display Hardware Info Structure
 * ============================================================================
 * Per-display hardware state and parameters.
 * Pointed to from display_unit_t at offset +0x18 (-0xF4 from end).
 * Size: approximately 0x60 bytes
 */
typedef struct smd_display_hw_t {
  uint16_t display_type;    /* 0x00: Display type code */
  uint16_t lock_state;      /* 0x02: Current lock state */
  ec_$eventcount_t lock_ec; /* 0x04: Lock event count (12 bytes) */
  ec_$eventcount_t op_ec;   /* 0x10: Operation complete event count */
  uint32_t field_1c;        /* 0x1C: Unknown (cleared in init) */
  uint8_t field_20;         /* 0x20: Unknown byte flag */
  uint8_t pad_21;           /* 0x21: Padding */
  uint16_t video_flags;     /* 0x22: Video control flags */
                            /*       bit 0: video enable */
  uint16_t field_24;        /* 0x24: Unknown */
  /* Scroll parameters */
  uint16_t scroll_x1;       /* 0x26: Scroll region x1 */
  uint16_t scroll_y1;       /* 0x28: Scroll region y1 */
  uint16_t scroll_x2;       /* 0x2A: Scroll region x2 */
  uint16_t scroll_y2;       /* 0x2C: Scroll region y2 */
  uint16_t scroll_dy;       /* 0x2E: Scroll delta y */
  uint16_t scroll_dx;       /* 0x30: Scroll delta x */
  uint16_t field_32;        /* 0x32: Unknown */
  uint16_t field_34;        /* 0x34: Unknown */
  uint16_t cursor_number;   /* 0x36: Current cursor number (0-3) */
  uint8_t cursor_visible;   /* 0x38: Cursor visible flag (negative = visible) */
  uint8_t pad_39;           /* 0x39: Padding */
  uint16_t field_3a;        /* 0x3A: Unknown */
  uint8_t tracking_enabled; /* 0x3C: Tracking mouse enabled */
  uint8_t pad_3d;           /* 0x3D: Padding */
  uint8_t field_3e;         /* 0x3E: Unknown byte */
  uint8_t pad_3f;           /* 0x3F: Padding */
  ec_$eventcount_t cursor_ec; /* 0x40: Cursor event count */
  uint16_t field_4c;          /* 0x4C: Unknown */
  uint16_t field_4e;          /* 0x4E: Unknown (cleared in init) */
  uint16_t height;            /* 0x50: Display height - 1 */
  uint16_t field_52;          /* 0x52: Unknown (cleared in init) */
  uint16_t width;             /* 0x54: Display width - 1 */
  uint16_t field_56;          /* 0x56: Unknown */
  uint16_t field_58;          /* 0x58: Unknown */
  uint16_t field_5a;          /* 0x5A: Unknown */
  uint16_t field_5c;          /* 0x5C: Unknown */
  uint16_t field_5e;          /* 0x5E: Unknown (cleared in init) */
} smd_display_hw_t;

/*
 * ============================================================================
 * HDM (Hidden Display Memory) Free Block Entry
 * ============================================================================
 * Tracks free regions of off-screen display memory.
 * Size: 4 bytes
 */
typedef struct smd_hdm_block_t {
  uint16_t offset; /* 0x00: Start offset in HDM */
  uint16_t size;   /* 0x02: Size of free block */
} smd_hdm_block_t;

/*
 * ============================================================================
 * HDM Allocation List
 * ============================================================================
 * Header for the hidden display memory free list.
 */
typedef struct smd_hdm_list_t {
  uint16_t count;            /* 0x00: Number of free blocks */
  uint16_t pad;              /* 0x02: Padding */
  smd_hdm_block_t blocks[1]; /* 0x04: Variable-length array of blocks */
} smd_hdm_list_t;

/*
 * ============================================================================
 * Font Table Entry
 * ============================================================================
 * Per-display font table. Each display unit can have up to 8 loaded fonts.
 * The font table is accessed via the first pointer in the display unit data.
 */
#define SMD_MAX_FONTS_PER_UNIT 8

typedef struct smd_font_entry_t {
  void *font_ptr;      /* 0x00: Pointer to original font data */
  uint16_t hdm_offset; /* 0x04: HDM position (encoded) */
  uint16_t pad;        /* 0x06: Padding */
} smd_font_entry_t;

/*
 * ============================================================================
 * Font Header - Version 1
 * ============================================================================
 * Version 1 font format (simpler, fixed-width assumed).
 * Indicated by version == 1 at offset 0x00.
 */
typedef struct smd_font_v1_t {
  uint16_t version;      /* 0x00: Font version (1) */
  uint16_t data_offset;  /* 0x02: Offset to glyph data from header start */
  uint16_t field_04;     /* 0x04: Unknown */
  uint16_t hdm_size;     /* 0x06: Size needed in HDM (scanlines) */
  uint16_t char_width;   /* 0x08: Default character width */
  uint16_t char_spacing; /* 0x0A: Character spacing */
  uint16_t unknown_char_width; /* 0x0C: Width for unknown characters */
  uint16_t field_0e;           /* 0x0E: Unknown */
  uint16_t cell_height;        /* 0x10: Character cell height */
  uint16_t default_missing;    /* 0x12: Default character for missing glyphs */
  uint16_t field_14;           /* 0x14: Unknown */
  uint16_t descent;            /* 0x16: Baseline descent */
  uint16_t ascent;             /* 0x18: Baseline ascent */
  uint8_t char_map[128];       /* 0x1A: Character index map (0x7F chars) */
                               /* Maps ASCII to glyph index in bitmap */
  /* Glyph metrics and bitmap data follow at offset 0x92 */
} smd_font_v1_t;

/*
 * ============================================================================
 * Font Header - Version 3
 * ============================================================================
 * Version 3 font format (more flexible, variable-width).
 * Indicated by version == 3 at offset 0x00.
 */
typedef struct smd_font_v3_t {
  uint16_t version;           /* 0x00: Font version (3) */
  uint16_t field_02;          /* 0x02: Unknown */
  uint16_t field_04;          /* 0x04: Unknown */
  uint16_t field_06;          /* 0x06: Unknown */
  uint16_t field_08;          /* 0x08: Unknown */
  uint16_t field_0a;          /* 0x0A: Unknown */
  uint16_t field_0c;          /* 0x0C: Unknown */
  uint16_t field_0e;          /* 0x0E: Unknown */
  uint16_t field_10;          /* 0x10: Unknown */
  uint16_t field_12;          /* 0x12: Unknown */
  uint16_t field_14;          /* 0x14: Unknown */
  uint16_t field_16;          /* 0x16: Unknown */
  uint16_t field_18;          /* 0x18: Unknown */
  uint32_t char_map_offset;   /* 0x1A: Offset to character map */
  uint32_t glyph_data_offset; /* 0x1E: Offset to glyph data */
  uint16_t field_22;          /* 0x22: Unknown */
  uint16_t field_24;          /* 0x24: Unknown */
  uint16_t field_26;          /* 0x26: Unknown */
  uint32_t data_offset;       /* 0x28: Offset to font bitmap data */
  uint32_t data_size;         /* 0x2C: Size of font bitmap data */
  uint16_t field_30;          /* 0x30: Unknown */
  uint16_t field_32;          /* 0x32: Unknown */
  uint8_t char_map[256];      /* 0x34: Full 8-bit character map */
  uint16_t hdm_size;          /* 0x42 (after map): HDM size needed */
                              /* More fields and glyph data follow */
} smd_font_v3_t;

/*
 * ============================================================================
 * Font Glyph Metrics
 * ============================================================================
 * Per-character glyph metrics, 8 bytes per glyph.
 */
typedef struct smd_glyph_metrics_t {
  int8_t bearing_x;    /* 0x00: X bearing (left offset) */
  int8_t width;        /* 0x01: Glyph width in pixels */
  int8_t bearing_y;    /* 0x02: Y bearing (top offset) */
  int8_t height;       /* 0x03: Glyph height in pixels */
  int8_t advance;      /* 0x04: Advance width */
  uint8_t bitmap_col;  /* 0x05: Column in bitmap */
  uint16_t bitmap_row; /* 0x06: Row in bitmap */
} smd_glyph_metrics_t;

#define SMD_FONT_VERSION_1 1
#define SMD_FONT_VERSION_3 3

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
  union {
    ec_$eventcount_t event_count_1; /* 0x00: Event count (12 bytes) */
    struct {
      uint32_t ec_value; /* 0x00: EC value */
      uint32_t ec_head;  /* 0x04: EC waiter list head */
      uint32_t field_08; /* 0x08: EC waiter list tail / scroll_ec ptr */
    };
  };
  smd_hdm_list_t *hdm_list_ptr;  /* 0x0C: Pointer to HDM free list */
  uint16_t field_10;             /* 0x10: Unknown */
  uint16_t asid;                 /* 0x12: Associated address space ID */
  uint16_t field_14;             /* 0x14: Unknown */
  uint16_t field_16;             /* 0x16: Unknown */
  smd_display_hw_t *hw;          /* 0x18: Pointer to hardware info */
  uint32_t field_1c;             /* 0x1C: Unknown */
  uint32_t field_20;             /* 0x20: Unknown */
  uint32_t mapped_addresses[58]; /* 0x24: Per-ASID mapped display addresses */
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
  uint16_t display_type; /* 0x00: Display type code */
  uint16_t field_02;     /* 0x02: Unknown */
  uint16_t field_04;     /* 0x04: Unknown */
  uint16_t field_06;     /* 0x06: Unknown */
  uint16_t field_08;     /* 0x08: Unknown */
  uint16_t field_0a;     /* 0x0A: Unknown */
  /* Clipping window - default bounds */
  int16_t clip_x1_default; /* 0x0C: Default clip x1 */
  int16_t clip_y1_default; /* 0x0E: Default clip y1 */
  int16_t clip_x2_default; /* 0x10: Default clip x2 */
  int16_t clip_y2_default; /* 0x12: Default clip y2 */
  /* Clipping window - current bounds */
  int16_t clip_x1;   /* 0x14: Current clip x1 */
  int16_t clip_y1;   /* 0x16: Current clip y1 */
  int16_t clip_x2;   /* 0x18: Current clip x2 */
  int16_t clip_y2;   /* 0x1A: Current clip y2 */
  uint8_t pad[0x44]; /* 0x1C-0x5F: Remaining fields */
} smd_display_info_t;

/*
 * ============================================================================
 * Event Queue Entry Structure
 * ============================================================================
 * Entry in the SMD event queue. Each entry is 16 bytes.
 * The queue is a circular buffer with 256 entries.
 */
typedef struct smd_event_entry_t {
  smd_cursor_pos_t pos;    /* 0x00: Cursor position */
  uint32_t timestamp;      /* 0x04: TIME_$CLOCK value */
  uint16_t field_08;       /* 0x08: Unknown field */
  uint16_t unit;           /* 0x0A: Display unit */
  uint16_t event_type;     /* 0x0C: Internal event type code */
  uint16_t button_or_char; /* 0x0E: Button state or character */
} smd_event_entry_t;

/*
 * Internal event type codes (in the queue):
 *   0x00 = key press with meta key (returns as keystroke, char only)
 *   0x07 = key press with meta key
 *   0x08 = button down
 *   0x0B = special event type
 *   0x0C = key press normal (returns as keystroke, char + modifier)
 *   0x0D = button down variant
 *   0x0E = button up
 *   0x0F = pointer up
 */
#define SMD_EVTYPE_INT_KEY_META0 0x00
#define SMD_EVTYPE_INT_KEY_META 0x07
#define SMD_EVTYPE_INT_BUTTON_DOWN 0x08
#define SMD_EVTYPE_INT_SPECIAL 0x0B
#define SMD_EVTYPE_INT_KEY_NORMAL 0x0C
#define SMD_EVTYPE_INT_BUTTON_DOWN2 0x0D
#define SMD_EVTYPE_INT_BUTTON_UP 0x0E
#define SMD_EVTYPE_INT_POINTER_UP 0x0F

/*
 * Public event type codes (returned to callers):
 */
#define SMD_EVTYPE_NONE 0
#define SMD_EVTYPE_BUTTON_DOWN 1
#define SMD_EVTYPE_BUTTON_UP 2
#define SMD_EVTYPE_KEYSTROKE 3
#define SMD_EVTYPE_SPECIAL 4
#define SMD_EVTYPE_POINTER_UP 5
#define SMD_EVTYPE_POWER_OFF 6
#define SMD_EVTYPE_SIGNAL 9

/*
 * Event queue size (circular buffer)
 */
#define SMD_EVENT_QUEUE_SIZE 256
#define SMD_EVENT_QUEUE_MASK 0xFF

/*
 * ============================================================================
 * Event Data Structures (returned by GET_*_EVENT functions)
 * ============================================================================
 */

/*
 * IDM event data structure (12 bytes)
 * Returned by SMD_$GET_IDM_EVENT
 */
typedef struct smd_idm_event_t {
  uint32_t timestamp; /* 0x00: Event timestamp */
  uint32_t field_04;  /* 0x04: Unknown */
  uint16_t field_08;  /* 0x08: Unknown */
  union {
    uint16_t data; /* 0x0A: Event-specific data (button/char) */
    struct {
      uint8_t char_code; /* 0x0A: Character code */
      uint8_t modifier;  /* 0x0B: Modifier flags */
    };
  };
} smd_idm_event_t;

/*
 * Unit event data structure (14 bytes)
 * Returned by SMD_$GET_UNIT_EVENT
 */
typedef struct smd_unit_event_t {
  uint32_t timestamp;      /* 0x00: Event timestamp */
  uint32_t field_04;       /* 0x04: Unknown */
  uint16_t field_08;       /* 0x08: Unknown */
  uint16_t unit;           /* 0x0A: Display unit */
  uint16_t button_or_char; /* 0x0C: Button state or character */
} smd_unit_event_t;

/* Alias for compatibility */
typedef smd_unit_event_t smd_event_data_t;

/*
 * Cursor bitmap structure (40 bytes)
 * Used by SMD_$LOAD_CRSR_BITMAP and SMD_$READ_CRSR_BITMAP
 */
typedef struct smd_crsr_bitmap_t {
  int16_t width;        /* 0x00: Cursor width (1-16) */
  int16_t height;       /* 0x02: Cursor height (1-16) */
  int16_t hot_x;        /* 0x04: Hot spot X */
  int16_t hot_y_offset; /* 0x06: height-1-hot_y */
  int16_t bitmap[16];   /* 0x08: Bitmap data */
} smd_crsr_bitmap_t;

/*
 * ============================================================================
 * Request Queue Entry Structure
 * ============================================================================
 * Entry in the SMD request queue. Each entry is 0x24 (36) bytes.
 * The queue is a circular buffer with 40 entries.
 */
typedef struct smd_request_entry_t {
  uint16_t request_type; /* 0x00: Request type code */
  uint16_t param_count;  /* 0x02: Number of parameters */
  uint16_t params[16];   /* 0x04: Parameter array (max 16) */
} smd_request_entry_t;

#define SMD_REQUEST_QUEUE_SIZE 40
#define SMD_REQUEST_QUEUE_MAX 0x28 /* 40 entries, 1-based */

/*
 * ============================================================================
 * SMD Globals Structure
 * ============================================================================
 * Global state for the SMD subsystem.
 * Base address: 0x00E82B8C
 */
typedef struct smd_globals_t {
  uint8_t pad_00[0x48];                 /* 0x00-0x47: Unknown */
  uint16_t asid_to_unit[SMD_MAX_ASIDS]; /* 0x48: ASID to display unit map */
  /* Each ASID maps to a display unit number */
  uint8_t pad_248[0x78]; /* 0x248-0xBF: Unknown */
  smd_track_rect_t
      kbd_cursor_track_rect;         /* 0xC0: Keyboard cursor tracking rect */
  uint32_t blank_time;               /* 0xC8: Time when blanking occurred */
                                     /*       (TIME_$CLOCKH value) */
  smd_cursor_pos_t saved_cursor_pos; /* 0xCC: Last saved cursor position */
  smd_cursor_pos_t
      default_cursor_pos;       /* 0xD0: Default cursor position (A5+0x1D94) */
  uint16_t cursor_button_state; /* 0xD4: Current cursor button state */
  uint16_t last_button_state;   /* 0xD6: Last reported button state */
  uint32_t blank_timeout;       /* 0xD8: Blank timeout value */
  int8_t blank_enabled;         /* 0xDC: Blanking enabled flag */
  int8_t blank_pending;         /* 0xDD: Blanking pending flag */
  uint16_t tp_reporting;        /* 0xDE: Trackpad reporting mode */
  int8_t tracking_enabled;      /* 0xE0: Tracking enabled flag (0xFF=enabled) */
  int8_t tp_cursor_active;      /* 0xE1: TP cursor active flag */
  int16_t tp_cursor_timeout;    /* 0xE2: TP cursor timeout counter */
  uint16_t cursor_tracking_count; /* 0xE4: Cursor tracking count for event
                                     coalescing */
  uint16_t tracking_window_id; /* 0xE6: Tracking window ID (from ENABLE_TRACKING
                                  param) */
  uint16_t tracking_rect_count; /* 0xE8: Number of tracking rectangles */
  smd_track_rect_t
      tracking_rects[SMD_MAX_TRACKING_RECTS]; /* 0xE8: Tracking rect array */
  /* 200 * 8 = 1600 bytes, ends at 0x728 */
  uint16_t event_queue_head; /* 0x728: Event queue write index */
  uint16_t event_queue_tail; /* 0x72A: Event queue read index */
  smd_event_entry_t event_queue[SMD_EVENT_QUEUE_SIZE]; /* 0x72C: Event queue */
  /* 256 * 16 = 4096 bytes, ends at 0x172C */
  uint8_t pad_172c[0x18];      /* 0x172C-0x1743: Unknown */
  uint8_t cursor_pending_flag; /* 0x1744: Cursor update pending */
  uint8_t pad_1745[0x2B];      /* 0x1745-0x176F: Unknown */
  smd_request_entry_t
      request_queue[SMD_REQUEST_QUEUE_SIZE]; /* 0x17D0: Request queue */
                                             /* 40 * 36 = 1440 bytes (0x5A0) */
  uint16_t
      request_queue_tail; /* 0x17F0: Request queue read index (after queue) */
  uint16_t request_queue_head; /* 0x17F2: Request queue write index */
  uint8_t pad_17f4[0x5A4];     /* 0x17F4-0x1D97: Unknown */
  uint16_t default_unit;       /* 0x1D98: Default display unit */
  uint16_t pad_1d9a;           /* 0x1D9A: Padding */
  uint16_t previous_unit;      /* 0x1D9C: Previous display unit */
  uint16_t unit_change_count;  /* 0x1D9E: Unit change counter */
  uint16_t last_idm_button;    /* 0x1DA0: Last IDM button state */
  int8_t power_off_reported;   /* 0x1DA2: Power-off event reported flag */
  uint8_t pad_1da3;            /* 0x1DA3: Padding */
} smd_globals_t;

/*
 * ============================================================================
 * BLT (Bit Block Transfer) Parameters
 * ============================================================================
 * Parameters for SMD_$BLT operations.
 */
typedef struct smd_blt_params_t {
  uint16_t flags;    /* 0x00: Operation flags */
                     /*       bit 7: sign bit (direction) */
                     /*       bit 6: invalid op */
                     /*       bit 5: use alternate rop */
                     /*       bit 4: async operation */
                     /*       bit 3: invalid op */
                     /*       bit 2: mask enable */
                     /*       bit 1: src enable */
                     /*       bit 0: dest enable */
  uint8_t rop_mode;  /* 0x02: ROP mode byte */
  uint8_t pattern;   /* 0x03: Pattern byte */
  uint32_t reserved; /* 0x04: Reserved */
  uint16_t src_x;    /* 0x08: Source X */
  uint16_t src_y;    /* 0x0A: Source Y */
  uint16_t dst_x;    /* 0x0C: Destination X */
  uint16_t dst_y;    /* 0x0E: Destination Y */
  uint16_t width;    /* 0x10: Width */
  uint16_t height;   /* 0x12: Height (low nibble: plane) */
} smd_blt_params_t;

/*
 * ============================================================================
 * Cursor Blink State
 * ============================================================================
 * State for cursor blinking.
 * Base address: 0x00E273D6
 */
typedef struct smd_blink_state_t {
  uint8_t smd_time_com;   /* 0x00: Time communication flag */
  uint8_t pad_01;         /* 0x01: Padding */
  uint8_t blink_flag;     /* 0x02: Blink state (0xFF = enabled) */
  uint8_t pad_03;         /* 0x03: Padding */
  uint16_t blink_counter; /* 0x04: Blink counter */
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

/* Cursor pointer table at 0x00E27366 - 4 pointers to cursor bitmap data */
extern int16_t *SMD_CURSOR_PTABLE[4];

/* Blink function pointer table at SMD_GLOBALS + 0x1DA0 */
typedef void (*smd_blink_func_t)(void);
extern smd_blink_func_t SMD_BLINK_FUNC_PTABLE[SMD_MAX_DISPLAY_UNITS];

/* Request lock ID for cursor operations */
#define smd_$request_lock 8

/* Lock data used by SMD_$ACQ_DISPLAY for scroll operations
 * Address: 0x00E6D92C */
extern uint32_t SMD_ACQ_LOCK_DATA;

/*
 * ============================================================================
 * Internal Function Prototypes
 * ============================================================================
 */

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
 * SMD_$CONTINUE_SCROLL - Continue scroll operation
 *
 * Continues a hardware scroll operation. If the remaining scroll amount
 * is zero, marks the scroll as complete. Otherwise, sets up the next
 * scroll step.
 *
 * Parameters:
 *   hw - Display hardware info
 *   ec - Event count for completion signaling
 *
 * Original address: 0x00E272B2
 */
void SMD_$CONTINUE_SCROLL(smd_display_hw_t *hw, ec_$eventcount_t *ec);

/*
 * SMD_$START_BLT - Start BLT operation
 *
 * Initiates a hardware BLT operation.
 *
 * Parameters:
 *   params  - BLT parameters (16-bit words)
 *   hw      - Display hardware info
 *   hw_regs - Hardware BLT register block
 *
 * Original address: 0x00E272BC
 */
void SMD_$START_BLT(uint16_t *params, smd_display_hw_t *hw, uint16_t *hw_regs);

/*
 * SMD_$INTERRUPT_INIT - Initialize SMD interrupt handling
 *
 * Sets up interrupt handlers for display hardware.
 *
 * Original address: 0x00E27284
 */
void SMD_$INTERRUPT_INIT(void);

/*
 * SMD_$COPY_FONT_TO_HDM - Copy font data to hidden display memory
 *
 * Copies font bitmap data from system memory to the hidden display memory.
 * The copy includes XOR'ing with a mask value for display hardware
 * compatibility.
 *
 * Parameters:
 *   display_base - Base address of display memory
 *   font         - Pointer to font data
 *   hdm_pos      - Pointer to HDM position
 *
 * Original address: 0x00E84934 (trampoline), 0x00E702F4 (implementation)
 */
void SMD_$COPY_FONT_TO_HDM(uint32_t display_base, void *font,
                           smd_hdm_pos_t *hdm_pos);

/*
 * SMD_$COPY_FONT_TO_MD_HDM - Copy font to main display hidden memory
 *
 * Copies font data to a fixed location in the main display's hidden memory.
 * Used for mono display types (1 and 2) to store a default system font.
 *
 * Parameters:
 *   font       - Pointer to font data
 *   status_ret - Status return
 *
 * Original address: 0x00E1D750
 */
void SMD_$COPY_FONT_TO_MD_HDM(void **font, status_$t *status_ret);

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
 * ============================================================================
 * Hardware BLT Register Block
 * ============================================================================
 * Memory-mapped hardware registers for Apollo display BLT operations.
 * Writing to offset 0x00 with bit 15 set starts the operation.
 * Polling offset 0x00 until bit 15 clears indicates completion.
 * Size: 16 bytes
 */
typedef struct smd_hw_blt_regs_t {
  volatile uint16_t control; /* 0x00: Control/status register
                              *       bit 15: busy (write 1 to start, poll for
                              * 0) bits 0-3: operation code (0xE = draw)
                              */
  uint16_t bit_pos;          /* 0x02: Bit position within word (x & 0xF) */
  uint16_t mask;             /* 0x04: Pixel mask (0x3FF typical) */
  uint16_t pattern;          /* 0x06: Pattern/ROP (0x3C0=draw, 0x380=clear) */
  uint16_t y_extent;         /* 0x08: Height - 1 (0xFFFF for single row) */
  uint16_t x_extent; /* 0x0A: Width in words - 1 (0xFFFF for single col) */
  uint16_t y_start;  /* 0x0C: Starting Y coordinate */
  uint16_t x_start;  /* 0x0E: Starting X coordinate */
} smd_hw_blt_regs_t;

/* BLT control register command codes */
#define SMD_BLT_CMD_START 0x8000 /* Bit 15: start operation */
#define SMD_BLT_CMD_DRAW 0x000E  /* Draw operation code */
#define SMD_BLT_CMD_START_DRAW (SMD_BLT_CMD_START | SMD_BLT_CMD_DRAW)

/* BLT pattern values */
#define SMD_BLT_PATTERN_DRAW 0x03C0  /* Pattern for line drawing */
#define SMD_BLT_PATTERN_CLEAR 0x0380 /* Pattern for clearing */

/* BLT extent for single line */
#define SMD_BLT_SINGLE_LINE 0xFFFF /* Use for single row/column */

/* Default mask value */
#define SMD_BLT_DEFAULT_MASK 0x03FF

/*
 * ============================================================================
 * Utility Init Result Structure
 * ============================================================================
 * Result structure populated by SMD_$UTIL_INIT.
 * Size: 20 bytes (0x14)
 */
typedef struct smd_util_ctx_t {
  uint32_t reserved;          /* 0x00: Reserved/padding */
  uint32_t field_04;          /* 0x04: From display unit +0x14 */
  uint32_t field_08;          /* 0x08: From display unit +0x08 */
  smd_hw_blt_regs_t *hw_regs; /* 0x0C: Hardware BLT register pointer */
  status_$t status;           /* 0x10: Status code */
} smd_util_ctx_t;

/*
 * SMD_$UTIL_INIT - Initialize utility context
 *
 * Sets up context for drawing operations. Must be called before
 * using hardware BLT registers for drawing.
 *
 * Parameters:
 *   ctx - Pointer to utility context structure to fill
 *
 * Original address: 0x00E6DED4
 */
void SMD_$UTIL_INIT(smd_util_ctx_t *ctx);

/*
 * SMD_$HORIZ_LINE - Draw horizontal line (internal)
 *
 * Low-level hardware-accelerated horizontal line drawing.
 *
 * Parameters:
 *   y       - Y coordinate
 *   x1      - Starting X coordinate
 *   x2      - Ending X coordinate
 *   param4  - Unused
 *   hw_regs - Hardware BLT register pointer
 *   control - Pointer to control value from ACQ_DISPLAY
 *   param7  - Unused
 *
 * Original address: 0x00E8496A
 */
void SMD_$HORIZ_LINE(int16_t *y, int16_t *x1, int16_t *x2, void *param4,
                     smd_hw_blt_regs_t *hw_regs, uint16_t *control,
                     void *param7);

/*
 * SMD_$VERT_LINE - Draw vertical line (internal)
 *
 * Low-level hardware-accelerated vertical line drawing.
 *
 * Parameters:
 *   x       - X coordinate
 *   y1      - Starting Y coordinate
 *   y2      - Ending Y coordinate
 *   param4  - Unused
 *   hw_regs - Hardware BLT register pointer
 *   control - Pointer to control value from ACQ_DISPLAY
 *
 * Original address: 0x00E84974
 */
void SMD_$VERT_LINE(int16_t *x, int16_t *y1, int16_t *y2, void *param4,
                    smd_hw_blt_regs_t *hw_regs, uint16_t *control);

/*
 * SMD_$INVERT_DISP - Invert display region (internal)
 *
 * Low-level function that inverts a fixed region of display memory.
 * Called by SMD_$INVERT_S after acquiring display lock.
 *
 * Parameters:
 *   display_base - Base address of display memory
 *   display_info - Pointer to display info (may be offset for hw config)
 *
 * Original address: 0x00E70376
 */
void SMD_$INVERT_DISP(uint32_t display_base, smd_display_info_t *display_info);

/*
 * ============================================================================
 * Cursor Internal Function Prototypes
 * ============================================================================
 */

/*
 * smd_$cursor_op - Internal cursor display/clear operation
 *
 * Common implementation for SMD_$DISPLAY_CURSOR and SMD_$CLEAR_CURSOR.
 *
 * Parameters:
 *   unit       - Display unit number
 *   pos        - Cursor position (packed as uint32_t: x in low 16, y in high
 * 16) clear_flag - 0 = display cursor, 0xFF = clear cursor status_ret - Status
 * return pointer
 *
 * Original address: 0x00E6DFFA
 */
void smd_$cursor_op(uint16_t unit, uint32_t pos, uint16_t clear_flag,
                    status_$t *status_ret);

/*
 * smd_$validate_unit - Validate display unit number
 *
 * Checks if the specified unit number is valid (currently only unit 1 is
 * valid).
 *
 * Parameters:
 *   unit - Display unit number
 *
 * Returns:
 *   Negative value (0xFF) if valid, 0 if invalid
 *
 * Original address: 0x00E6D700
 */
int8_t smd_$validate_unit(uint16_t unit);

/*
 * SHOW_CURSOR - Internal cursor show/update function
 *
 * Updates cursor state and display. Called when cursor position or
 * visibility changes.
 *
 * Parameters:
 *   pos        - Pointer to cursor position
 *   lock_data1 - First lock data pointer
 *   lock_data2 - Second lock data pointer
 *
 * Returns:
 *   Result code (negative if cursor was updated)
 *
 * Original address: 0x00E6E1CC
 */
int8_t SHOW_CURSOR(uint32_t *pos, int16_t *lock_data1, int8_t *lock_data2);

/*
 * smd_$check_tp_state - Check trackpad state
 *
 * Checks if trackpad cursor tracking is currently active.
 *
 * Returns:
 *   Negative value if active, positive if not
 *
 * Original address: 0x00E6E84C
 */
int8_t smd_$check_tp_state(void);

/*
 * smd_$send_loc_event - Send location event
 *
 * Queues a location event for processing.
 *
 * Parameters:
 *   unit    - Display unit
 *   type    - Event type
 *   pos     - Cursor position
 *   buttons - Button state
 *
 * Original address: 0x00E6E8D6
 */
void smd_$send_loc_event(uint16_t unit, uint16_t type, smd_cursor_pos_t pos,
                         uint16_t buttons);

/*
 * smd_$draw_cursor_internal - Low-level cursor drawing
 *
 * Called by blink routines to actually draw/erase cursor.
 *
 * Original address: 0x00E2720E
 */
int8_t smd_$draw_cursor_internal(int16_t *cursor_num, uint32_t *cursor_pos,
                                 void *hw_offset, void *display_comm,
                                 int8_t *cursor_flag, uint32_t *ec_1,
                                 uint32_t *ec_2);

/*
 * smd_$reschedule_blink_timer - Reschedule cursor blink timer
 *
 * Parameters:
 *   interval - Timer interval in microseconds
 *
 * Original address: 0x00E72690
 */
void smd_$reschedule_blink_timer(uint32_t interval);

/*
 * smd_$poll_keyboard - Poll keyboard for input events
 *
 * Checks the keyboard for pending input and adds events to the queue.
 * Called before reading from the event queue to ensure fresh data.
 *
 * Returns:
 *   Negative value (0xFF) if events were added, 0 otherwise
 *
 * Original address: 0x00E6E84C
 */
int8_t smd_$poll_keyboard(void);

/*
 * smd_$enqueue_event - Add event to the event queue
 *
 * Adds a location/input event to the circular event queue.
 * Timestamps the event and signals the display event count.
 *
 * Parameters:
 *   unit    - Display unit number
 *   type    - Internal event type code
 *   pos     - Cursor position
 *   buttons - Button state or character value
 *
 * Original address: 0x00E6E8D6
 */
void smd_$enqueue_event(uint16_t unit, uint16_t type, uint32_t pos,
                        uint16_t buttons);

/*
 * smd_$add_trk_rects_internal - Internal function to add tracking rectangles
 *
 * Common implementation for SMD_$CLR_AND_LOAD_TRK_RECT and SMD_$ADD_TRK_RECT.
 * If clear_flag is set (negative), clears existing rectangles first.
 *
 * Parameters:
 *   clear_flag - If negative (0xFF), clear existing rects first
 *   rects      - Pointer to array of tracking rectangles
 *   count      - Number of rectangles to add
 *
 * Returns:
 *   Negative value (0xFF) if successful, 0 if list full
 *
 * Original address: 0x00E6E4D4
 */
int8_t smd_$add_trk_rects_internal(int8_t clear_flag, smd_track_rect_t *rects,
                                   uint16_t count);

/* Display Transfer Table Event count at 0x00E2DC90 */
extern ec_$eventcount_t DTTE;

/* FIM quit event count and value arrays */
extern ec_$eventcount_t FIM_$QUIT_EC[];
extern uint32_t FIM_$QUIT_VALUE[];

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

/*
 * ============================================================================
 * Display Borrow/Return Auxiliary Data
 * ============================================================================
 * These fields are stored in the display unit array at specific offsets
 * relative to the base address. For unit N (1-based), offsets are computed
 * from (base + N * 0x10C):
 *
 *   -0xF4: hw pointer (smd_display_hw_t *)
 *   -0xF0: owner_asid (ASID of the display owner, 0 if not owned)
 *   -0xEE: borrowed_asid (ASID of borrower, 0 if not borrowed)
 *
 * Base address: 0x00E2E3FC
 * Auxiliary base: 0x00E2E308 (base - 0xF4)
 */
#define SMD_UNIT_AUX_BASE 0x00E2E308

/* Per-unit auxiliary structure (stored before each unit block) */
typedef struct smd_unit_aux_t {
  smd_display_hw_t *hw;   /* 0x00: Hardware info pointer */
  uint16_t owner_asid;    /* 0x04: Owner process ASID */
  uint16_t borrowed_asid; /* 0x06: Borrower process ASID */
  /* More fields follow, total 0x10C bytes per unit slot */
} smd_unit_aux_t;

/* Access helper - gets auxiliary data for unit N */
static inline smd_unit_aux_t *smd_get_unit_aux(uint16_t unit_num) {
  /* Returns pointer to auxiliary data block for this unit */
  return (smd_unit_aux_t *)((uint8_t *)SMD_UNIT_AUX_BASE +
                            unit_num * SMD_DISPLAY_UNIT_SIZE);
}

/* Lock ID for respond/borrow operations */
#define smd_$respond_lock 7

/* Uppercase aliases for lock constants (used in some code) */
#define SMD_REQUEST_LOCK smd_$request_lock
#define SMD_RESPOND_LOCK smd_$respond_lock

/* Secondary event count at 0x00E2E408 (used for borrow signaling) */
extern ec_$eventcount_t SMD_BORROW_EC;

/* Borrow response table at 0x00E84924 */
extern int8_t SMD_BORROW_RESPONSE[];

/* Error string for borrow failures */
extern const char SMD_Error_Borrowing_Display_Err[];

/*
 * smd_$init_display_state - Initialize display state for borrow/associate
 *
 * Initializes the display state when borrowing or associating.
 *
 * Parameters:
 *   options    - Init options flag (negative = full init)
 *   status_ret - Status return
 *
 * Original address: 0x00E6F514
 */
void smd_$init_display_state(int8_t options, status_$t *status_ret);

/*
 * smd_$reset_display_state - Reset display hardware and cursor state
 *
 * Resets the display hardware state and optionally clears cursor state.
 *
 * Parameters:
 *   unit    - Display unit number
 *   flag    - Reset flag (negative = full reset including cursor state)
 *
 * Original address: 0x00E6D736
 */
void smd_$reset_display_state(uint16_t unit, int8_t flag);

/*
 * smd_$reset_tracking_state - Reset tracking and event state
 *
 * Resets the global tracking and event queue state.
 *
 * Parameters:
 *   unit    - Display unit number
 *   flag    - Reset flag (negative = full reset)
 *
 * Original address: 0x00E6D7E2
 */
void smd_$reset_tracking_state(uint16_t unit, int8_t flag);

/* Request queue event counts */
extern ec_$eventcount_t SMD_REQUEST_EC_WAIT;   /* At 0x00E2E3FC - wait for space */
extern ec_$eventcount_t SMD_REQUEST_EC_SIGNAL; /* At 0x00E2E408 - signal new request */

#endif /* SMD_INTERNAL_H */
