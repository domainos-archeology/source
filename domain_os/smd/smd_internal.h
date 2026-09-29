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

#include "smd/smd.h" /* base.h (via smd.h) supplies offsetof and boolean */
#include "ec/ec.h"
#include "fim/fim.h"
#include "ml/ml.h"
#include "proc1/proc1.h"
#include "term/term.h"   /* DTTE */
#include "time/time.h"   /* TIME_$CLOCKH */

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/*
 * Pointer to the display controller's memory-mapped registers.
 * Isolated behind a macro because the width/volatility of an MMIO pointer is
 * architecture policy; the m68k image stores a plain 32-bit address here
 * (0x00FF9800 on a SAU2) and pokes 16-bit registers through it, e.g.
 * SMD_$EOF_WAIT 0x00E6F408 movea.l (0x8,A3),A1 / move.w #0x21,(A1).
 */
#define SMD_HW_REG_PTR volatile uint16_t *

/* Maximum number of display units */
#define SMD_MAX_DISPLAY_UNITS 4

/* Display unit structure size */
#define SMD_DISPLAY_UNIT_SIZE 0x10C /* 268 bytes */

/* Display info entry size */
#define SMD_DISPLAY_INFO_SIZE 0x60 /* 96 bytes */

/*
 * Number of entries in the display info table at 0x00E27376.
 *
 * The image has exactly one: the table's single 0x60-byte entry ends at
 * 0x00E273D5 and SMD_TIME_$COM begins at 0x00E273D6.  This is deliberately
 * NOT SMD_MAX_DISPLAY_UNITS - the per-unit *record* block at 0x00E2E3FC is
 * sized for SMD_MAX_DISPLAY_UNITS, but the info/hardware table is not.
 */
#define SMD_DISPLAY_INFO_COUNT 1

/* Maximum ASIDs supported */
/*
 * ASID -> unit table length.  smd_globals_t.asid_to_unit runs from 0x48 up to
 * kbd_cursor_track_rect at 0xC0, i.e. 0x78 bytes = 60 word entries.
 */
#define SMD_MAX_ASIDS 60

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
/* Resolved (bead source-06qh): SMD_$INIT (0x00E34E00 / 0x00E34E0E) gives
 * type 1 max_x = 0x31F / max_y = 0x3FF (800x1024 - portrait) and type 2
 * max_x = 0x3FF / max_y = 0x31F (1024x800 - landscape), so the names below
 * used to be swapped.  Only the names were changed; the values are
 * load-bearing in comparisons in alloc_hdm.c, copy_font_to_md_hdm.c,
 * free_hdm.c, init.c and inq_disp_info.c. */
#define SMD_DISP_TYPE_MONO_PORTRAIT 1      /* 800x1024 portrait */
#define SMD_DISP_TYPE_MONO_LANDSCAPE 2     /* 1024x800 landscape */
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
/* status_$display_invalid_unit_number moved to smd/smd.h: tpad/ raises it
 * too.  status_$display_invalid_use_of_driver_procedure and
 * status_$display_unsupported_font_version moved there too: dtty/ raises both
 * (bead source-3uo). */
#define status_$display_font_not_loaded 0x00130002
#define status_$display_internal_font_table_full 0x00130003
#define status_$display_error_unloading_internal_table 0x00130006
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
#define status_$display_error_borrowing_from_screen_manager 0x0013000E
#define status_$display_unable_to_borrow_display_in_use 0x0013000F
#define status_$display_borrow_request_denied_by_screen_manager 0x00130010
#define status_$display_cant_return_not_borrowed 0x00130012
#define status_$display_already_borrowed_by_this_process 0x00130014
#define status_$display_invalid_scroll_displacement 0x00130019
#define status_$display_invalid_cursor_number 0x00130023
#define status_$display_invalid_event_count_key 0x00130026

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

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(smd_scroll_rect_t, x1) == 0x00, "smd_scroll_rect_t.x1");
_Static_assert(__builtin_offsetof(smd_scroll_rect_t, y1) == 0x02, "smd_scroll_rect_t.y1");
_Static_assert(__builtin_offsetof(smd_scroll_rect_t, x2) == 0x04, "smd_scroll_rect_t.x2");
_Static_assert(__builtin_offsetof(smd_scroll_rect_t, y2) == 0x06, "smd_scroll_rect_t.y2");
_Static_assert(sizeof(smd_scroll_rect_t) == 0x08, "smd_scroll_rect_t size");

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
  /* 0x20/0x21: a 16-bit field that is *set* one byte at a time
   * (SMD_$ACQ_DISPLAY 0x00E6EB98 "st (0x20,A3)", 0x00E6EBEE "clr.b (0x20,A3)")
   * but *cleared* as a whole word (SMD_$START_BLT 0x00E15D6E and
   * SMD_$START_SCROLL both do "clr.w (0x20,An)"). */
  boolean field_20;
  uint8_t field_21;
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
  /* 0x32: last cursor position shown on this display, packed as
   * (y << 16) | x -- see SMD_POS_X()/SMD_POS_Y().  SHOW_CURSOR reads and
   * writes it as one longword (0x00E6E250 move.l (0x32,A2),...). */
  uint32_t cursor_pos;
  int16_t cursor_number;    /* 0x36: Current cursor number (0-3) */
  boolean cursor_visible;   /* 0x38: Cursor visible flag (0xFF = visible) */
  uint8_t pad_39;           /* 0x39: Padding */
  uint16_t field_3a;        /* 0x3A: Unknown */
  uint8_t tracking_enabled; /* 0x3C: Tracking mouse enabled */
  uint8_t pad_3d;           /* 0x3D: Padding */
  uint8_t field_3e;         /* 0x3E: Unknown byte */
  uint8_t pad_3f;           /* 0x3F: Padding */
  ec_$eventcount_t cursor_ec; /* 0x40: Cursor event count */
  uint16_t field_4c;          /* 0x4C: Unknown */
  /*
   * Screen bounds and clip window.  Verified from SMD_$SET_CLIP_WINDOW
   * (0x00E6FE7E-0x00E6FEB0: +0x56/+0x58 are clamped against +0x4E/+0x50,
   * +0x5A/+0x5C against +0x52/+0x54) and from smd_$write_str_clip_impl
   * (0x00E70402/0x00E7040E), which tests the X coordinate against +0x56/+0x58
   * and the Y coordinate against +0x5A/+0x5C.  SMD_$INIT loads +0x50 with
   * 0x31F and +0x54 with 0x3FF for display type 1 (800x1024 portrait) and the
   * other way round for type 2 (1024x800 landscape).
   */
  int16_t min_x;              /* 0x4E: Minimum X (cleared in init) */
  int16_t max_x;              /* 0x50: Maximum X (display width - 1) */
  int16_t min_y;              /* 0x52: Minimum Y (cleared in init) */
  int16_t max_y;              /* 0x54: Maximum Y (display height - 1) */
  int16_t clip_x1;            /* 0x56: Clip window left */
  int16_t clip_x2;            /* 0x58: Clip window right */
  int16_t clip_y1;            /* 0x5A: Clip window top */
  int16_t clip_y2;            /* 0x5C: Clip window bottom */
  uint16_t field_5e;          /* 0x5E: Unknown (cleared in init) */
} smd_display_hw_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(smd_display_hw_t, display_type) == 0x00, "smd_display_hw_t.display_type");
_Static_assert(__builtin_offsetof(smd_display_hw_t, field_24) == 0x24, "smd_display_hw_t.field_24");
_Static_assert(__builtin_offsetof(smd_display_hw_t, scroll_x1) == 0x26, "smd_display_hw_t.scroll_x1");
_Static_assert(__builtin_offsetof(smd_display_hw_t, scroll_y1) == 0x28, "smd_display_hw_t.scroll_y1");
_Static_assert(__builtin_offsetof(smd_display_hw_t, scroll_x2) == 0x2A, "smd_display_hw_t.scroll_x2");
_Static_assert(__builtin_offsetof(smd_display_hw_t, scroll_y2) == 0x2C, "smd_display_hw_t.scroll_y2");
_Static_assert(__builtin_offsetof(smd_display_hw_t, scroll_dy) == 0x2E, "smd_display_hw_t.scroll_dy");
_Static_assert(__builtin_offsetof(smd_display_hw_t, scroll_dx) == 0x30, "smd_display_hw_t.scroll_dx");
_Static_assert(__builtin_offsetof(smd_display_hw_t, pad_39) == 0x39, "smd_display_hw_t.pad_39");
_Static_assert(__builtin_offsetof(smd_display_hw_t, field_3a) == 0x3A, "smd_display_hw_t.field_3a");
_Static_assert(__builtin_offsetof(smd_display_hw_t, tracking_enabled) == 0x3C, "smd_display_hw_t.tracking_enabled");
_Static_assert(__builtin_offsetof(smd_display_hw_t, pad_3d) == 0x3D, "smd_display_hw_t.pad_3d");
_Static_assert(__builtin_offsetof(smd_display_hw_t, field_3e) == 0x3E, "smd_display_hw_t.field_3e");
_Static_assert(__builtin_offsetof(smd_display_hw_t, pad_3f) == 0x3F, "smd_display_hw_t.pad_3f");
_Static_assert(__builtin_offsetof(smd_display_hw_t, field_4c) == 0x4C, "smd_display_hw_t.field_4c");
_Static_assert(__builtin_offsetof(smd_display_hw_t, clip_x2) == 0x58, "smd_display_hw_t.clip_x2");
_Static_assert(__builtin_offsetof(smd_display_hw_t, clip_y1) == 0x5A, "smd_display_hw_t.clip_y1");
_Static_assert(__builtin_offsetof(smd_display_hw_t, field_5e) == 0x5E, "smd_display_hw_t.field_5e");
#endif

/*
 * Packed cursor position helpers (SMD_POS_X / SMD_POS_Y / SMD_POS_MAKE) and
 * the smd_cursor_pos_t typedef they go with now live in smd/smd.h, where the
 * evidence for the layout is recorded.
 */

#if defined(ARCH_M68K)
_Static_assert(offsetof(smd_display_hw_t, lock_state) == 0x02, "hw lock_state");
_Static_assert(offsetof(smd_display_hw_t, lock_ec) == 0x04, "hw lock_ec");
/* SMD_$GET_EC 0x00E6FDFA "pea (0x10,A3)" and SMD_$START_BLT 0x00E15D72
 * "move.l (0x10,A2),(0x1c,A2)". */
_Static_assert(offsetof(smd_display_hw_t, op_ec) == 0x10, "hw op_ec");
_Static_assert(offsetof(smd_display_hw_t, field_1c) == 0x1C, "hw field_1c");
_Static_assert(offsetof(smd_display_hw_t, video_flags) == 0x22, "hw vflags");
/* SMD_$SEND_RESPONSE 0x00E6F500 "pea (-0x20,A2)" and SMD_$BORROW_DISPLAY
 * 0x00E6F60E "move.l (0x40,A3),D2". */
_Static_assert(offsetof(smd_display_hw_t, cursor_ec) == 0x40, "hw cursor_ec");
_Static_assert(offsetof(smd_display_hw_t, cursor_pos) == 0x32, "hw cursor_pos");
_Static_assert(offsetof(smd_display_hw_t, cursor_number) == 0x36, "hw cursor#");
_Static_assert(offsetof(smd_display_hw_t, cursor_visible) == 0x38, "hw vis");
_Static_assert(offsetof(smd_display_hw_t, min_x) == 0x4E, "hw min_x");
_Static_assert(offsetof(smd_display_hw_t, max_x) == 0x50, "hw max_x");
_Static_assert(offsetof(smd_display_hw_t, min_y) == 0x52, "hw min_y");
_Static_assert(offsetof(smd_display_hw_t, max_y) == 0x54, "hw max_y");
_Static_assert(offsetof(smd_display_hw_t, clip_x1) == 0x56, "hw clip_x1");
_Static_assert(offsetof(smd_display_hw_t, clip_y2) == 0x5C, "hw clip_y2");
_Static_assert(sizeof(smd_display_hw_t) == 0x60, "smd_display_hw_t size");
#endif

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

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(smd_hdm_block_t, offset) == 0x00, "smd_hdm_block_t.offset");
_Static_assert(__builtin_offsetof(smd_hdm_block_t, size) == 0x02, "smd_hdm_block_t.size");
_Static_assert(sizeof(smd_hdm_block_t) == 0x04, "smd_hdm_block_t size");

/*
 * ============================================================================
 * HDM Allocation List
 * ============================================================================
 * Header for the hidden display memory free list.
 */
/*
 * The free list has NO padding after the count: SMD_$ALLOC_HDM walks it with
 * A0 = list + 4 + 4*(k-1) and reads the block's size at (A0) and its offset
 * at (-0x2,A0) (0x00E6D98A / 0x00E6D992 / 0x00E6D9B4), so block k-1 starts at
 * list + 2.  SMD_$FREE_HDM agrees ((-0x6,A2,D2w) and (-0x4,A2,D2w) with
 * D2 = index*4 at 0x00E6DB0C).
 */
typedef struct smd_hdm_list_t {
  uint16_t count;            /* 0x00: Number of free blocks */
  smd_hdm_block_t blocks[1]; /* 0x02: Variable-length array of blocks */
} smd_hdm_list_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(smd_hdm_list_t, count) == 0x00, "smd_hdm_list_t.count");
#endif

#if defined(ARCH_M68K)
_Static_assert(offsetof(smd_hdm_list_t, blocks) == 0x02, "hdm list blocks");
#endif

/*
 * ============================================================================
 * Font Table Entry
 * ============================================================================
 * Per-display font table. Each display unit can have up to 8 loaded fonts.
 * The font table is accessed via the first pointer in the display unit data.
 */
#define SMD_MAX_FONTS_PER_UNIT 8

typedef struct smd_font_entry_t {
  void *font_ptr;        /* 0x00: Pointer to original font data */
  /* 0x04: where the font's bitmap lives in hidden display memory.
   * SMD_$LOAD_FONT hands its address straight to SMD_$ALLOC_HDM
   * (0x00E6DCCC "pea (-0x4,A2)") and then to SMD_$COPY_FONT_TO_HDM
   * (0x00E6DCF0); SMD_$UNLOAD_FONT hands it to SMD_$FREE_HDM (0x00E6DD80). */
  smd_hdm_pos_t hdm_pos;
} smd_font_entry_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(smd_font_entry_t, font_ptr) == 0x00, "smd_font_entry_t.font_ptr");
#endif

#if defined(ARCH_M68K)
_Static_assert(sizeof(smd_font_entry_t) == 8, "smd_font_entry_t size");
#endif

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
  uint16_t char_width;   /* 0x08: SMD_$COPY_FONT_TO_HDM reads it as the data
                          *       size (smd/copy_font_to_hdm.c:74) */
  uint16_t field_0a;     /* 0x0A: Unknown (was "char_spacing", a guess the
                          *       write loop contradicts - see 0x10) */
  uint16_t field_0c;     /* 0x0C: Unknown (was "unknown_char_width") */
  uint16_t field_0e;     /* 0x0E: Unknown */
  uint16_t char_spacing; /* 0x10: added to every glyph's advance, and to the
                          *       missing-glyph width; smd_$write_str_clip_impl
                          *       0x00E70614 `add.w (0x10,A2),D0w` and
                          *       0x00E706B8.  The v3 twin is at 0x5A. */
  uint16_t default_missing; /* 0x12: width used when the map yields index 0;
                             *       0x00E7066E `move.w (0x12,A2),D0w` and
                             *       0x00E706B4.  The v3 twin is at 0x6E. */
  uint16_t field_14;           /* 0x14: Unknown */
  uint16_t descent;            /* 0x16: Baseline descent */
  uint16_t ascent;             /* 0x18: Baseline ascent */
  uint8_t char_map[128];       /* 0x1A: Character index map (0x7F chars) */
                               /* Maps ASCII to glyph index in bitmap */
  /* Glyph metrics and bitmap data follow at offset 0x92 */
} smd_font_v1_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(smd_font_v1_t, version) == 0x00, "smd_font_v1_t.version");
_Static_assert(__builtin_offsetof(smd_font_v1_t, data_offset) == 0x02, "smd_font_v1_t.data_offset");
_Static_assert(__builtin_offsetof(smd_font_v1_t, field_04) == 0x04, "smd_font_v1_t.field_04");
_Static_assert(__builtin_offsetof(smd_font_v1_t, hdm_size) == 0x06, "smd_font_v1_t.hdm_size");
_Static_assert(__builtin_offsetof(smd_font_v1_t, char_width) == 0x08, "smd_font_v1_t.char_width");
_Static_assert(__builtin_offsetof(smd_font_v1_t, field_0a) == 0x0A, "smd_font_v1_t.field_0a");
_Static_assert(__builtin_offsetof(smd_font_v1_t, field_0c) == 0x0C, "smd_font_v1_t.field_0c");
_Static_assert(__builtin_offsetof(smd_font_v1_t, field_0e) == 0x0E, "smd_font_v1_t.field_0e");
_Static_assert(__builtin_offsetof(smd_font_v1_t, char_spacing) == 0x10, "smd_font_v1_t.char_spacing");
_Static_assert(__builtin_offsetof(smd_font_v1_t, default_missing) == 0x12, "smd_font_v1_t.default_missing");
_Static_assert(__builtin_offsetof(smd_font_v1_t, field_14) == 0x14, "smd_font_v1_t.field_14");
_Static_assert(__builtin_offsetof(smd_font_v1_t, descent) == 0x16, "smd_font_v1_t.descent");
_Static_assert(__builtin_offsetof(smd_font_v1_t, ascent) == 0x18, "smd_font_v1_t.ascent");
_Static_assert(__builtin_offsetof(smd_font_v1_t, char_map) == 0x1A, "smd_font_v1_t.char_map");

/*
 * ============================================================================
 * Font Header - Version 3
 * ============================================================================
 * Version 3 font format (more flexible, variable-width).
 * Indicated by version == 3 at offset 0x00.
 */
/* PACKED: m68k aligns 32-bit fields to 2 bytes, so the recovered offsets
 * below are only reproducible on a 4/8-byte-aligning host if the record is
 * packed.  Packing changes no m68k layout. */
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
  /* 0x1A/0x1E were guessed to be the map/glyph offsets, but SMD_$WRITE_STRING
   * reads those from 0x34 and 0x38 (see below), so these are unidentified. */
  uint32_t field_1a;          /* 0x1A: Unknown */
  uint32_t field_1e;          /* 0x1E: Unknown */
  uint16_t field_22;          /* 0x22: Unknown */
  uint16_t field_24;          /* 0x24: Unknown */
  uint16_t field_26;          /* 0x26: Unknown */
  uint32_t data_offset;       /* 0x28: Offset to font bitmap data */
  uint32_t data_size;         /* 0x2C: Size of font bitmap data */
  uint16_t field_30;          /* 0x30: Unknown */
  uint16_t field_32;          /* 0x32: Unknown */

  /*
   * The image reads the region from 0x34 two contradictory ways.  The drawing
   * path is the correct one and the width-measuring path carries an original
   * bug (bead source-2gs7):
   *
   *   - drawing (0x00E70426 `add.l (0x34,A2),D0` / 0x00E7042A
   *     `move.b (0x0,A2,D0*0x1),D1b`) treats 0x34 as a LONGWORD byte offset
   *     from the font base to the character map;
   *   - width measuring (0x00E70634 `move.b (0x34,A2,D0w*0x1),D1b`) reads the
   *     map INLINE at 0x34.
   *
   * Three things settle it in favour of the offset reading.  (1) Both paths
   * agree that 0x38 is a longword glyph-data offset -- 0x00E70438 and
   * 0x00E70646 are the identical `lea (-0x8,A2),A1` / `adda.l (0x38,A2),A1`
   * pair -- and 0x38 would be map[4..7] if the map were inline.  (2) Every
   * other v3 metric lies past 0x34 and would be buried inside a 256-byte
   * inline map: 0x42 hdm_size (SMD_$LOAD_FONT 0x00E6DCBE), 0x48 descent
   * (0x00E704FC, v1 0x16), 0x4A ascent (0x00E7056C, v1 0x18), 0x5A
   * char_spacing (0x00E7060E, v1 0x10) and 0x6E default_missing
   * (0x00E7063A, v1 0x12).  (3) The v1 layout is the same shape with the map
   * inline at 0x1A and the glyph records biased from 0x92, so v3's indirection
   * is exactly what a "more flexible" format would add.
   *
   * The measure path's inline read is therefore an original defect and is
   * preserved as such in smd/write_str_clip.c: characters 0..7 pick up the two
   * offset longwords and 8..255 pick up whatever follows them in the header.
   * It only affects SMD_$WRITE_STRING's "string does not fit the clip window"
   * arm, which measures the width instead of drawing.
   */
  uint32_t char_map_offset;   /* 0x34: byte offset to the 256-entry map */
  uint32_t glyph_data_offset; /* 0x38: byte offset to the glyph records */
  uint16_t field_3c;          /* 0x3C: Unknown */
  uint16_t field_3e;          /* 0x3E: Unknown */
  uint16_t field_40;          /* 0x40: Unknown */
  uint16_t hdm_size;          /* 0x42: HDM size needed */
  uint16_t field_44;          /* 0x44: Unknown */
  uint16_t field_46;          /* 0x46: Unknown */
  uint16_t descent;           /* 0x48: Baseline descent */
  uint16_t ascent;            /* 0x4A: Baseline ascent */
  uint8_t  gap_4c[0x0E];      /* 0x4C: Unknown */
  uint16_t char_spacing;      /* 0x5A: Character spacing */
  uint8_t  gap_5c[0x12];      /* 0x5C: Unknown */
  uint16_t default_missing;   /* 0x6E: Width for missing glyphs */
} __attribute__((packed)) smd_font_v3_t;

_Static_assert(__builtin_offsetof(smd_font_v3_t, char_map_offset) == 0x34, "smd_font_v3_t.char_map_offset");
_Static_assert(__builtin_offsetof(smd_font_v3_t, glyph_data_offset) == 0x38, "smd_font_v3_t.glyph_data_offset");
_Static_assert(__builtin_offsetof(smd_font_v3_t, hdm_size) == 0x42, "smd_font_v3_t.hdm_size");
_Static_assert(__builtin_offsetof(smd_font_v3_t, descent) == 0x48, "smd_font_v3_t.descent");
_Static_assert(__builtin_offsetof(smd_font_v3_t, ascent) == 0x4A, "smd_font_v3_t.ascent");
_Static_assert(__builtin_offsetof(smd_font_v3_t, char_spacing) == 0x5A, "smd_font_v3_t.char_spacing");
_Static_assert(__builtin_offsetof(smd_font_v3_t, default_missing) == 0x6E, "smd_font_v3_t.default_missing");

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(smd_font_v3_t, version) == 0x00, "smd_font_v3_t.version");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_02) == 0x02, "smd_font_v3_t.field_02");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_04) == 0x04, "smd_font_v3_t.field_04");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_06) == 0x06, "smd_font_v3_t.field_06");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_08) == 0x08, "smd_font_v3_t.field_08");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_0a) == 0x0A, "smd_font_v3_t.field_0a");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_0c) == 0x0C, "smd_font_v3_t.field_0c");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_0e) == 0x0E, "smd_font_v3_t.field_0e");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_10) == 0x10, "smd_font_v3_t.field_10");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_12) == 0x12, "smd_font_v3_t.field_12");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_14) == 0x14, "smd_font_v3_t.field_14");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_16) == 0x16, "smd_font_v3_t.field_16");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_18) == 0x18, "smd_font_v3_t.field_18");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_1a) == 0x1A, "smd_font_v3_t.field_1a");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_1e) == 0x1E, "smd_font_v3_t.field_1e");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_22) == 0x22, "smd_font_v3_t.field_22");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_24) == 0x24, "smd_font_v3_t.field_24");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_26) == 0x26, "smd_font_v3_t.field_26");
_Static_assert(__builtin_offsetof(smd_font_v3_t, data_offset) == 0x28, "smd_font_v3_t.data_offset");
_Static_assert(__builtin_offsetof(smd_font_v3_t, data_size) == 0x2C, "smd_font_v3_t.data_size");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_30) == 0x30, "smd_font_v3_t.field_30");
_Static_assert(__builtin_offsetof(smd_font_v3_t, field_32) == 0x32, "smd_font_v3_t.field_32");

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

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(smd_glyph_metrics_t, bearing_x) == 0x00, "smd_glyph_metrics_t.bearing_x");
_Static_assert(__builtin_offsetof(smd_glyph_metrics_t, width) == 0x01, "smd_glyph_metrics_t.width");
_Static_assert(__builtin_offsetof(smd_glyph_metrics_t, bearing_y) == 0x02, "smd_glyph_metrics_t.bearing_y");
_Static_assert(__builtin_offsetof(smd_glyph_metrics_t, height) == 0x03, "smd_glyph_metrics_t.height");
_Static_assert(__builtin_offsetof(smd_glyph_metrics_t, advance) == 0x04, "smd_glyph_metrics_t.advance");
_Static_assert(__builtin_offsetof(smd_glyph_metrics_t, bitmap_col) == 0x05, "smd_glyph_metrics_t.bitmap_col");
_Static_assert(__builtin_offsetof(smd_glyph_metrics_t, bitmap_row) == 0x06, "smd_glyph_metrics_t.bitmap_row");

#define SMD_FONT_VERSION_1 1
#define SMD_FONT_VERSION_3 3

/*
 * ============================================================================
 * Display Unit Record
 * ============================================================================
 * The compiler keeps a *biased* pointer to this record: everywhere in the
 * kernel the address is computed as
 *
 *     A3 = 0x00E2E3FC + unit * 0x10C
 *
 * and the record's fields are then reached at displacements -0xF4 .. +0x17.
 * The record therefore *starts* at (A3 - 0xF4) and is 0x10C bytes long, so
 * the record for unit N does *not* begin at 0x00E2E3FC + N*0x10C.  For the
 * only unit that exists (unit 1) the record lives at
 * 0x00E2E414 .. 0x00E2E51F, immediately after the two standalone eventcounts
 * SMD_EC_1 (0x00E2E3FC) and SMD_EC_2 (0x00E2E408) and immediately before
 * smd_$trk_rect_mutex.
 *
 * Field evidence (all offsets below are from the record base, i.e. A3-0xF4):
 *   0x00  SMD_$INIT 0x00E34DC2 movea.l (-0xf4,A3),A4       -> hw pointer
 *   0x04  SMD_$INIT 0x00E34DC6 clr.l   (-0xf0,A3)          -> one longword
 *         clear covering owner_asid (0x04) and borrowed_asid (0x06)
 *   0x06  SMD_$FREE_ASID 0x00E75272 cmp.w (-0xee,A0),D2w   -> borrowed_asid
 *   0x04  SMD_$DM_COND_EVENT_WAIT 0x00E6F02E cmp.w (-0xf0,A0),D2w
 *   0x08  SMD_$DM_COND_EVENT_WAIT 0x00E6F15E cmp.w (-0xec,A1),D2w
 *   0x0A  SMD_$INIT 0x00E34E1A clr.w   (-0xea,A3)
 *   0x10  SMD_$INIT 0x00E34E6C-76: 57 longs cleared from (-0xe4,A3) to (-4,A3)
 *         and SMD_$MAP_DISPLAY_U indexes them as (-0xe8 + asid*4), i.e. the
 *         array is 1-based on the ASID
 *   0xF4  SMD_$INIT case 0 (0x00E34D6E) move.l A2,(0x10c,A0) = &globals+0x1748
 *         -> the unit's 8-entry font table (SMD_$LOAD_FONT 0x00E6DC5A reads
 *         it as (A2) with A2 = 0xE2E3FC + unit*0x10C)
 *   0xF8  SMD_$INIT case 0 (0x00E34D7E) move.l A2,(0x110,A0) = &globals+0x1788
 *   0xFC  SMD_$INIT case 0 (0x00E34D8A) move.l #0x00FF9800,(0x114,A0)
 *         -> the display controller register base,
 *         later overwritten by io_$probe (0x00E34DCE pea (0x8,A3)) and read
 *         back by SMD_$EOF_WAIT (0x00E6F408 movea.l (0x8,A3),A1)
 *   0x100 SMD_$INIT case 0 (0x00E34D96) move.l (A2)+,(0x118,A0)
 *   0x104 SMD_$INIT case 0 (0x00E34D9A) move.l (A2)+,(0x11C,A0)
 *   0x108 SMD_$INIT case 0 (0x00E34D8A) move.l #0x00FC0000,(0x120,A0)
 *         -> display memory base; read by SMD_$DISPLAY_LOGO (0x00E70290
 *         movea.l (0x14,A0),A4) and SMD_$INVERT_S (0x00E6DE02)
 */
typedef struct smd_display_unit_t {
  smd_display_hw_t *hw;          /* 0x00  (A3-0xF4) */
  uint16_t owner_asid;           /* 0x04  (A3-0xF0) */
  uint16_t borrowed_asid;        /* 0x06  (A3-0xEE) */
  uint16_t field_08;             /* 0x08  (A3-0xEC) an ASID as well */
  uint16_t field_0a;             /* 0x0A  (A3-0xEA) cleared by SMD_$INIT */
  uint32_t field_0c;             /* 0x0C  (A3-0xE8) not touched by SMD_$INIT */
  /* 0x10 (A3-0xE4): per-ASID mapped display addresses, 1-based on the ASID
   * (the compiler indexes them as (-0xE8,A3) + asid*4). */
  uint32_t mapped_addresses[57];
  /* 0xF4 (A3+0x00): the unit's font table, 8 entries of 8 bytes living at
   * &SMD_GLOBALS + 0x1748.  Indexed 1-based: SMD_$LOAD_FONT reaches entry i
   * at (font_table + i*8) - 8 (0x00E6DCC4 lsl.l #3 / 0x00E6DCE2 move.l
   * (A0),(-0x8,A2)), SMD_$UNLOAD_FONT likewise at 0x00E6DD5A/0x00E6DD66, and
   * smd_$reset_unit_display clears all 8 font pointers (0x00E6D76E). */
  smd_font_entry_t *font_table;
  /* 0xF8 (A3+0x04): the unit's hidden-display-memory free list, living at
   * &SMD_GLOBALS + 0x1788.  SMD_$ALLOC_HDM (0x00E6D974) and SMD_$FREE_HDM
   * (0x00E6DA80) both read it as "movea.l (0x4,A0),A2", and
   * smd_$reset_unit_display seeds it with a single block
   * (0x00E6D77C-0x00E6D79A). */
  smd_hdm_list_t *hdm_list;
  /* 0xFC (A3+0x08): display controller register base (0x00FF9800). */
  SMD_HW_REG_PTR ctrl_regs;
  /* 0x100 (A3+0x0C): the UID of the display object.  SMD_$MAP_DISPLAY_U
   * (0x00E6F940 "pea (0xc,A3)") and SMD_$UNMAP_DISPLAY_U (0x00E6F9E4) pass
   * its address as MST_$MAP's / MST_$UNMAP's uid argument.  SMD_$INIT seeds
   * it from smd_$unit_init_params (0x00E173D4 = 00 00 04 00 00 00 00 00). */
  uid_t display_uid;
  uint32_t display_base;         /* 0x108 (A3+0x14) = 0x00FC0000 */
} smd_display_unit_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(smd_display_unit_t, field_08) == 0x08, "smd_display_unit_t.field_08");
_Static_assert(__builtin_offsetof(smd_display_unit_t, field_0c) == 0x0C, "smd_display_unit_t.field_0c");
#endif

#if defined(ARCH_M68K)
_Static_assert(offsetof(smd_display_unit_t, hw) == 0x00, "unit hw");
_Static_assert(offsetof(smd_display_unit_t, owner_asid) == 0x04, "unit owner");
_Static_assert(offsetof(smd_display_unit_t, borrowed_asid) == 0x06, "unit brw");
_Static_assert(offsetof(smd_display_unit_t, field_0a) == 0x0A, "unit 0x0a");
_Static_assert(offsetof(smd_display_unit_t, mapped_addresses) == 0x10,
               "unit mapped_addresses");
_Static_assert(offsetof(smd_display_unit_t, font_table) == 0xF4, "unit fonts");
_Static_assert(offsetof(smd_display_unit_t, hdm_list) == 0xF8, "unit hdm");
_Static_assert(offsetof(smd_display_unit_t, ctrl_regs) == 0xFC, "unit ctrl");
_Static_assert(offsetof(smd_display_unit_t, display_uid) == 0x100, "unit uid");
_Static_assert(offsetof(smd_display_unit_t, display_base) == 0x108,
               "unit display_base");
_Static_assert(sizeof(smd_display_unit_t) == SMD_DISPLAY_UNIT_SIZE,
               "smd_display_unit_t size");
#endif

/*
 * ============================================================================
 * Display Info Entry
 * ============================================================================
 * Resolved (bead source-fqne): the 0x60-byte "display info" entry at
 * 0x00E27376 and the per-display *hardware* record that
 * smd_display_unit_t::hw points at are one and the same object.  SMD_$INIT
 * stores the literal 0x00E27376 into unit 1's hw pointer
 * (0x00E34D96 "move.l #0xe27376,(0x18,A0)") and then reaches the same words
 * through A4 = rec->hw (0x00E34DE8 "clr.w (0x52,A4)", 0x00E34E06
 * "move.w #0x31f,(0x50,A4)").  Cross-checked from both sides:
 *
 *   +0x00  display_type  smd_$validate_unit 0x00E6D722 "tst.w (-0x60,A0,D1)"
 *                        vs SMD_$INIT 0x00E34E1E "clr.w (0x2,A4)" neighbours
 *   +0x32  cursor_pos    smd_$reset_display_globals 0x00E6D80E
 *                        "clr.l (-0x2e,A0)" vs SHOW_CURSOR 0x00E6E250
 *                        "move.l (0x32,A2),(-0x8,A6)"
 *   +0x36  cursor_number smd_$reset_display_globals 0x00E6D812 vs
 *                        SHOW_CURSOR 0x00E6E25C "move.w (0x36,A2),D6w"
 *   +0x38  cursor_visible smd_$reset_display_globals 0x00E6D816 vs
 *                        SMD_$INQ_KBD_CURSOR 0x00E6E112 "move.b (-0x28,A0),D0b"
 *   +0x40  cursor_ec     SMD_$SEND_RESPONSE 0x00E6F500 "pea (-0x20,A2)"
 *   +0x4E..+0x5C         SMD_$SET_CLIP_WINDOW 0x00E6FE7E-0x00E6FEB0 and
 *                        smd_$write_str_clip_impl 0x00E70402/0x00E7040E
 *
 * The old separate smd_display_info_t put the clip window at +0x0C..+0x1B,
 * which no instruction in the image agrees with, so it is gone: the name is
 * now an alias for the single recovered layout.
 *
 * The entry is 1-based on the unit number - every accessor computes
 * base + unit*0x60 and then subtracts 0x60 - so use smd_$unit_info(unit).
 */
typedef smd_display_hw_t smd_display_info_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(smd_display_info_t) == SMD_DISPLAY_INFO_SIZE,
               "smd_display_info_t size");
#endif

/*
 * ============================================================================
 * Event Queue Entry Structure
 * ============================================================================
 * Entry in the SMD event queue. Each entry is 16 bytes.
 * The queue is a circular buffer with 256 entries.
 */
typedef struct smd_event_entry_t {
  uint32_t pos;            /* 0x00: packed cursor position (y<<16 | x);
                            *       smd_$enqueue_event 0x00E6E91A stores it
                            *       with a single move.l */
  uint32_t timestamp;      /* 0x04: TIME_$CLOCK value */
  uint16_t field_08;       /* 0x08: Unknown field */
  uint16_t unit;           /* 0x0A: Display unit */
  uint16_t event_type;     /* 0x0C: Internal event type code */
  uint16_t button_or_char; /* 0x0E: Button state or character */
} smd_event_entry_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(smd_event_entry_t, timestamp) == 0x04, "smd_event_entry_t.timestamp");
_Static_assert(__builtin_offsetof(smd_event_entry_t, field_08) == 0x08, "smd_event_entry_t.field_08");
_Static_assert(__builtin_offsetof(smd_event_entry_t, unit) == 0x0A, "smd_event_entry_t.unit");
_Static_assert(__builtin_offsetof(smd_event_entry_t, event_type) == 0x0C, "smd_event_entry_t.event_type");
_Static_assert(__builtin_offsetof(smd_event_entry_t, button_or_char) == 0x0E, "smd_event_entry_t.button_or_char");

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

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(smd_idm_event_t, timestamp) == 0x00, "smd_idm_event_t.timestamp");
_Static_assert(__builtin_offsetof(smd_idm_event_t, field_04) == 0x04, "smd_idm_event_t.field_04");
_Static_assert(__builtin_offsetof(smd_idm_event_t, field_08) == 0x08, "smd_idm_event_t.field_08");

/*
 * Unit event data structure (14 bytes)
 * Returned by SMD_$GET_UNIT_EVENT
 */
/*
 * SMD_$GET_UNIT_EVENT copies the queue entry's first four fields into this
 * record at the SAME offsets (0x00E6EEEA-0x00E6EEFC), so the first longword
 * is the entry's packed cursor position and the second is its timestamp -
 * the names here used to be shifted by one longword (bead source-v5vu).
 * The 0x0C word is written only by the jump-table arms that have data for
 * it; on the arms that fall straight through to 0x00E6EF68 it is left
 * holding whatever was on the stack.
 */
typedef struct smd_unit_event_t {
  uint32_t pos;            /* 0x00: packed cursor position (y<<16 | x) */
  uint32_t timestamp;      /* 0x04: TIME_$CLOCK value */
  uint16_t field_08;       /* 0x08: Unknown */
  uint16_t unit;           /* 0x0A: Display unit */
  uint16_t button_or_char; /* 0x0C: Button state or character */
} smd_unit_event_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(smd_unit_event_t, pos) == 0x00, "smd_unit_event_t.pos");
_Static_assert(__builtin_offsetof(smd_unit_event_t, timestamp) == 0x04, "smd_unit_event_t.timestamp");
_Static_assert(__builtin_offsetof(smd_unit_event_t, field_08) == 0x08, "smd_unit_event_t.field_08");
_Static_assert(__builtin_offsetof(smd_unit_event_t, unit) == 0x0A, "smd_unit_event_t.unit");
_Static_assert(__builtin_offsetof(smd_unit_event_t, button_or_char) == 0x0C, "smd_unit_event_t.button_or_char");

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

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(smd_crsr_bitmap_t, width) == 0x00, "smd_crsr_bitmap_t.width");
_Static_assert(__builtin_offsetof(smd_crsr_bitmap_t, height) == 0x02, "smd_crsr_bitmap_t.height");
_Static_assert(__builtin_offsetof(smd_crsr_bitmap_t, hot_x) == 0x04, "smd_crsr_bitmap_t.hot_x");
_Static_assert(__builtin_offsetof(smd_crsr_bitmap_t, hot_y_offset) == 0x06, "smd_crsr_bitmap_t.hot_y_offset");
_Static_assert(__builtin_offsetof(smd_crsr_bitmap_t, bitmap) == 0x08, "smd_crsr_bitmap_t.bitmap");

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

/* Remaining documented offsets (bead source-pewa). */
_Static_assert(__builtin_offsetof(smd_request_entry_t, request_type) == 0x00, "smd_request_entry_t.request_type");
_Static_assert(__builtin_offsetof(smd_request_entry_t, param_count) == 0x02, "smd_request_entry_t.param_count");
_Static_assert(__builtin_offsetof(smd_request_entry_t, params) == 0x04, "smd_request_entry_t.params");

#define SMD_REQUEST_QUEUE_SIZE 40
#define SMD_REQUEST_QUEUE_MAX 0x28 /* 40 entries, 1-based */

/*
 * ============================================================================
 * SMD Globals Structure
 * ============================================================================
 * Global state for the SMD subsystem.
 * Base address: 0x00E82B8C
 */
/*
 * A per-unit cursor-blink routine.  Entry 1 is SMD_$BLINK_CURSOR_1
 * (0x00E2722C); the callback reaches it through
 * smd_globals_t.blink_func[default_unit].  There is room for two entries
 * before the code at SMD_GLOBALS + 0x1DA8.
 *
 * The table holds 32-bit TARGET addresses, so the field type is uint32_t
 * rather than a C function pointer: an 8-byte host pointer would push
 * sizeof(smd_globals_t) past 0x1DA8.  SMD_BLINK_CALL turns one back into
 * something callable.
 */
typedef void (*smd_blink_func_t)(void);
#define SMD_BLINK_FUNC_ENTRIES 2
#define SMD_BLINK_CALL(va) ((smd_blink_func_t)ARCH_VA_TO_PTR(va))

typedef struct smd_globals_t {
  /*
   * 0x00-0x47: mapping length per display type, indexed by
   * smd_display_hw_t.display_type.  SMD_$MAP_DISPLAY_U passes
   * "pea (0x0,A5,D1w*0x1)" with D1 = display_type * 4 as MST_$MAP's length
   * argument (0x00E6F930-0x00E6F938), and SMD_$UNMAP_DISPLAY_U passes the
   * same cell to MST_$UNMAP (0x00E6F9D4-0x00E6F9DC).
   */
  uint32_t display_map_length[18];      /* 0x00-0x47 */
  /*
   * 0x48: ASID -> display unit map, indexed by PROC1_$AS_ID with a *word*
   * scale (SMD_$SEND_RESPONSE 0x00E6F4CC: move.w PROC1_$AS_ID,D0 / add.w
   * D0w,D0w / move.w (0x48,A5,D0w*1),D0w).  The region ends where
   * kbd_cursor_track_rect starts (0xC0, proven by SMD_$CLEAR_KBD_CURSOR
   * 0x00E6E83C pea (0xc0,A5)), so it holds (0xC0-0x48)/2 = 60 entries.
   */
  uint16_t asid_to_unit[SMD_MAX_ASIDS]; /* 0x48 .. 0xBF */
  /* 0xC0: tracking rectangle used to hide the cursor behind the keyboard
   * cursor bitmap.  SMD_$CLEAR_KBD_CURSOR / SMD_$LOAD_CRSR_BITMAP pass its
   * address to SMD_$ADD_TRK_RECT / SMD_$DEL_TRK_RECT. */
  smd_track_rect_t kbd_cursor_track_rect;
  uint32_t blank_time;               /* 0xC8: TIME_$CLOCKH at blanking.
                                      *       SMD_$INIT 0x00E34E8E clr.l */
  uint32_t saved_cursor_pos;         /* 0xCC: last locator position reported
                                      *       (SMD_$LOC_EVENT 0x00E6E9D2 /
                                      *       0x00E6EA70 move.l) */
  uint32_t default_cursor_pos;       /* 0xD0: position of the cursor that is
                                      *       currently drawn on the screen
                                      *       (SHOW_CURSOR 0x00E6E342/0x00E6E41E) */
  int16_t cursor_button_state;       /* 0xD4: number of the cursor that is
                                      *       currently drawn (0x00E6E348) */
  int16_t last_button_state;         /* 0xD6: last mouse button state reported
                                      *       (SMD_$LOC_EVENT 0x00E6EA7A) */
  uint32_t blank_timeout;            /* 0xD8: SMD_$SET_BLANK_TIMEOUT
                                      *       0x00E6F1A2 move.l (A0),(0xd8,A5) */
  boolean blank_enabled;             /* 0xDC: blink callback 0x00E6FFB2 */
  boolean blank_pending;             /* 0xDD: blink callback 0x00E6FFAC/0x00E6FFF0 */
  int16_t tp_reporting;              /* 0xDE: SMD_$SET_TP_REPORTING 0x00E6E4C2 */
  boolean tracking_enabled;          /* 0xE0: SMD_$ENABLE_TRACKING 0x00E6E468 st,
                                      *       SMD_$DISABLE_TRACKING 0x00E6E48E clr.b */
  uint8_t pad_e1;                    /* 0xE1: Padding (never referenced) */
  int16_t tp_cursor_timeout;         /* 0xE2: SMD_$STOP_TP_CURSOR 0x00E6EAE2
                                      *       move.w #-1; SMD_$LOC_EVENT
                                      *       0x00E6EA6C clr.w */
  int16_t tracking_cursor_num;       /* 0xE4: cursor number used while tracking.
                                      *       SMD_$ENABLE_TRACKING 0x00E6E470
                                      *       stores it, SMD_$LOC_EVENT
                                      *       0x00E6EAB8 passes its address to
                                      *       SHOW_CURSOR as the cursor number */
  int16_t tracking_rect_count;       /* 0xE6: number of live tracking rects
                                      *       (SHOW_CURSOR 0x00E6E2DA,
                                      *       smd_$add_trk_rects_internal
                                      *       0x00E6E524/0x00E6E56C) */
  /* 0xE8: tracking rectangle array, stride 8.  smd_$add_trk_rects_internal
   * writes element k at A5 + 0xE0 + (k+1)*8 = A5 + 0xE8 + k*8 (0x00E6E55C). */
  smd_track_rect_t tracking_rects[SMD_MAX_TRACKING_RECTS];
  /* 200 * 8 = 0x640 bytes, ends at 0x728 */
  uint16_t event_queue_head; /* 0x728: write index (smd_$enqueue_event 0x00E6E8EA) */
  uint16_t event_queue_tail; /* 0x72A: read index  (smd_$enqueue_event 0x00E6E8F4) */
  smd_event_entry_t event_queue[SMD_EVENT_QUEUE_SIZE]; /* 0x72C: 256 * 16 */
  /* ends at 0x172C */
  uint8_t pad_172c[0x11];      /* 0x172C-0x173C: Unknown */
  /* 0x173D-0x173F: three flag bytes that only smd_$reset_display_globals
   * touches in the code that has been read so far (0x00E6D838 st, 0x00E6D83C
   * st, 0x00E6D840 clr.b).  Domain booleans, hence signed. */
  boolean field_173d;
  boolean field_173e;
  boolean field_173f;
  uint8_t pad_1740[4];         /* 0x1740-0x1743: Unknown */
  boolean cursor_pending_flag; /* 0x1744: cursor redraw pending
                                *         (SHOW_CURSOR 0x00E6E3D4/0x00E6E41A/
                                *          0x00E6E440) */
  uint8_t pad_1745[0x17F0 - 0x1745]; /* 0x1745-0x17EF: Unknown.  Holds the two
                                      * buffers whose addresses SMD_$INIT case 0
                                      * stores in the unit record (+0x1748 and
                                      * +0x1788). */
  int16_t request_queue_tail;  /* 0x17F0: read index  (0x00E6F248) */
  int16_t request_queue_head;  /* 0x17F2: write index (0x00E6F244) */
  /*
   * 0x17F4: request queue.  The original addresses an entry as
   *   A5 + 0x17D0 + index*36   with index in 1..40 (SMD_$SIGNAL 0x00E6F2A0,
   *   SMD_$DM_COND_EVENT_WAIT 0x00E6F0BA),
   * so entry `index` is request_queue[index - 1] here.
   */
  smd_request_entry_t request_queue[SMD_REQUEST_QUEUE_SIZE]; /* 40 * 36 = 0x5A0 */
  /* ends at 0x1D94 */
  uint32_t cursor_pos_sentinel; /* 0x1D94: "use the display's own position"
                                 * sentinel.  SHOW_CURSOR (0x00E6E24A) compares
                                 * its pos argument against it; every internal
                                 * caller passes its address as that argument
                                 * (e.g. 0x00E6E49A, 0x00E6E586, 0x00E6EB32). */
  /*
   * 0x1D98: the default/current display unit.  This is the *same word* the
   * disassembly also shows as the absolute address 0x00E84924: SMD_GLOBALS is
   * at 0x00E82B8C and 0x00E82B8C + 0x1D98 = 0x00E84924, and Ghidra's xrefs to
   * 0x00E84924 are exactly the "(0x1d98,A5)" instructions (0x00E6E1F0,
   * 0x00E6E0E8, 0x00E6E7BC, 0x00E6E7CA, 0x00E6FC4A, ...).  Bead source-nuan
   * assumed they were two globals; they are one, so the separate
   * SMD_DEFAULT_DISPLAY_UNIT object is gone and every caller uses this field.
   * Signed: SMD_$INQ_KBD_CURSOR 0x00E6E0F6 does "move.w (0x1d98,A5),D0w" then
   * "ext.l D0".
   */
  int16_t default_unit;
  /* 0x1D9A: per-unit "response pending" bytes.  SMD_$SEND_RESPONSE addresses
   * them as (0x1D99,A5 + unit) with unit 1-based (0x00E6F4FC), so unit N is
   * response_pending[N - 1]. */
  int8_t response_pending[2];
  int16_t previous_unit;       /* 0x1D9C: unit the cursor was last shown on
                                *         (SHOW_CURSOR 0x00E6E200/0x00E6E444) */
  uint16_t unit_change_count;  /* 0x1D9E: SMD_$SET_UNIT_CURSOR_POS 0x00E6E7C6 */
  /*
   * 0x1DA0..0x1DA7 is the per-unit blink-routine pointer table, indexed by
   * the unit number itself: SMD_$BLINK_CURSOR_CALLBACK does
   * 0x00E6FF84 `lea (0x0,A5,D0*0x1),A0` / 0x00E6FF88 `movea.l (0x1da0,A0),A1`
   * with D0 = SMD_GLOBALS.default_unit * 4.  The image confirms it: entry 1
   * at 0x00E84930 holds 0x00E2722C, which is SMD_$BLINK_CURSOR_1.
   *
   * Entry 0 is never dereferenced -- smd_$validate_unit accepts only unit 1
   * (0x00E6D70A `cmpi.w #0x1,D0w`) and default_unit is 1 in the image -- so
   * the module reuses its two words for unrelated state.  That is not an
   * overlap bug, it is how the storage is laid out; the union spells both.
   * (Bead source-q85g.)
   *
   * The record really does end at 0x1DA8: 0x00E84934 = SMD_GLOBALS + 0x1DA8
   * is a code trampoline (`lea (-0x2,PC),A0` / `jmp 0x00E702F4`) that
   * 0x00E6DCFE calls, and 0x00E84942 is smd_$write_str_clip_impl's.
   */
  union {
    uint32_t blink_func[SMD_BLINK_FUNC_ENTRIES];  /* 0x1DA0: target addresses */
    struct {
      uint16_t last_idm_button;    /* 0x1DA0: SMD_$GET_IDM_EVENT
                                    *         0x00E6EE7C / 0x00E6EE88 */
      boolean power_off_reported;  /* 0x1DA2: SMD_$DM_COND_EVENT_WAIT
                                    *         0x00E6F078 */
      uint8_t pad_1da3;            /* 0x1DA3: Padding */
    };
  };
} smd_globals_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(smd_globals_t, display_map_length) == 0x00, "smd_globals_t.display_map_length");
_Static_assert(__builtin_offsetof(smd_globals_t, pad_e1) == 0xE1, "smd_globals_t.pad_e1");
_Static_assert(__builtin_offsetof(smd_globals_t, pad_172c) == 0x172C, "smd_globals_t.pad_172c");
_Static_assert(__builtin_offsetof(smd_globals_t, pad_1740) == 0x1740, "smd_globals_t.pad_1740");
_Static_assert(__builtin_offsetof(smd_globals_t, pad_1da3) == 0x1DA3, "smd_globals_t.pad_1da3");
#endif

#if defined(ARCH_M68K)
_Static_assert(offsetof(smd_globals_t, asid_to_unit) == 0x48, "g asid_to_unit");
_Static_assert(offsetof(smd_globals_t, kbd_cursor_track_rect) == 0xC0, "g kbd");
_Static_assert(offsetof(smd_globals_t, blank_time) == 0xC8, "g blank_time");
_Static_assert(offsetof(smd_globals_t, saved_cursor_pos) == 0xCC, "g saved_pos");
_Static_assert(offsetof(smd_globals_t, default_cursor_pos) == 0xD0, "g def_pos");
_Static_assert(offsetof(smd_globals_t, cursor_button_state) == 0xD4, "g cbs");
_Static_assert(offsetof(smd_globals_t, last_button_state) == 0xD6, "g lbs");
_Static_assert(offsetof(smd_globals_t, blank_timeout) == 0xD8, "g blank_to");
_Static_assert(offsetof(smd_globals_t, blank_enabled) == 0xDC, "g blank_en");
_Static_assert(offsetof(smd_globals_t, blank_pending) == 0xDD, "g blank_pend");
_Static_assert(offsetof(smd_globals_t, tp_reporting) == 0xDE, "g tp_rep");
_Static_assert(offsetof(smd_globals_t, tracking_enabled) == 0xE0, "g trk_en");
_Static_assert(offsetof(smd_globals_t, tp_cursor_timeout) == 0xE2, "g tp_to");
_Static_assert(offsetof(smd_globals_t, tracking_cursor_num) == 0xE4, "g trk_csr");
_Static_assert(offsetof(smd_globals_t, tracking_rect_count) == 0xE6, "g trk_cnt");
_Static_assert(offsetof(smd_globals_t, tracking_rects) == 0xE8, "g trk_rects");
_Static_assert(sizeof(smd_track_rect_t) == 8, "smd_track_rect_t size");
_Static_assert(offsetof(smd_globals_t, event_queue_head) == 0x728, "g eq_head");
_Static_assert(offsetof(smd_globals_t, event_queue_tail) == 0x72A, "g eq_tail");
_Static_assert(offsetof(smd_globals_t, event_queue) == 0x72C, "g eq");
_Static_assert(offsetof(smd_globals_t, field_173d) == 0x173D, "g 173d");
_Static_assert(offsetof(smd_globals_t, cursor_pending_flag) == 0x1744, "g cpf");
_Static_assert(offsetof(smd_globals_t, request_queue_tail) == 0x17F0, "g rq_tail");
_Static_assert(offsetof(smd_globals_t, request_queue_head) == 0x17F2, "g rq_head");
_Static_assert(offsetof(smd_globals_t, request_queue) == 0x17F4, "g rq");
_Static_assert(sizeof(smd_request_entry_t) == 36, "smd_request_entry_t size");
_Static_assert(offsetof(smd_globals_t, cursor_pos_sentinel) == 0x1D94, "g sent");
_Static_assert(offsetof(smd_globals_t, default_unit) == 0x1D98, "g def_unit");
_Static_assert(offsetof(smd_globals_t, response_pending) == 0x1D9A, "g resp");
_Static_assert(offsetof(smd_globals_t, previous_unit) == 0x1D9C, "g prev_unit");
_Static_assert(offsetof(smd_globals_t, unit_change_count) == 0x1D9E, "g ucc");
_Static_assert(offsetof(smd_globals_t, last_idm_button) == 0x1DA0, "g idm");
_Static_assert(offsetof(smd_globals_t, power_off_reported) == 0x1DA2, "g poff");
_Static_assert(offsetof(smd_globals_t, blink_func) == 0x1DA0, "g blink_func");
_Static_assert(sizeof(smd_globals_t) == 0x1DA8, "smd_globals_t size");
#endif

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

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(smd_blt_params_t, flags) == 0x00, "smd_blt_params_t.flags");
_Static_assert(__builtin_offsetof(smd_blt_params_t, rop_mode) == 0x02, "smd_blt_params_t.rop_mode");
_Static_assert(__builtin_offsetof(smd_blt_params_t, pattern) == 0x03, "smd_blt_params_t.pattern");
_Static_assert(__builtin_offsetof(smd_blt_params_t, reserved) == 0x04, "smd_blt_params_t.reserved");
_Static_assert(__builtin_offsetof(smd_blt_params_t, src_x) == 0x08, "smd_blt_params_t.src_x");
_Static_assert(__builtin_offsetof(smd_blt_params_t, src_y) == 0x0A, "smd_blt_params_t.src_y");
_Static_assert(__builtin_offsetof(smd_blt_params_t, dst_x) == 0x0C, "smd_blt_params_t.dst_x");
_Static_assert(__builtin_offsetof(smd_blt_params_t, dst_y) == 0x0E, "smd_blt_params_t.dst_y");
_Static_assert(__builtin_offsetof(smd_blt_params_t, width) == 0x10, "smd_blt_params_t.width");
_Static_assert(__builtin_offsetof(smd_blt_params_t, height) == 0x12, "smd_blt_params_t.height");

/*
 * ============================================================================
 * SMD_TIME_$COM - the SMD_TIME module's common block
 * ============================================================================
 * Base address: 0x00E273D6 (Ghidra label SMD_TIME_$COM).  It is exactly six
 * bytes long: SMD_$DISPLAY_COM ends at 0x00E273D5 and MNK_$KTT_PTRS starts at
 * 0x00E273DC, and the only displacements the image ever uses are +0x00 (byte),
 * +0x02 (byte) and +0x04 (word):
 *
 *   SMD_$INIT_BLINK             00e34ec6  clr.b (A0) / st (0x2,A0) / clr.w (0x4,A0)
 *   smd_$reset_display_globals  00e6d81a  clr.b (A1) / clr.b (0x2,A1)
 *   SHOW_CURSOR                 00e6e38e  clr.b (A0) / tst.b (0x2,A0)
 *   SHOW_CURSOR                 00e6e432  move.b D4b,(A0) / st (0x2,A0)
 *                                         / move.w #0x7,(0x4,A0)
 *   SMD_$BLINK_CURSOR_CALLBACK  00e6ff72  tst.b (A2) / tst.w (0x4,A2)
 *                               00e6ff8e  tst.b (0x2,A2) / clr.w (0x4,A2)
 *
 * The two flags are Domain booleans: written with clr.b / st / seq and tested
 * with tst.b + bpl/bmi, so they must be signed.
 */
typedef struct smd_time_com_t {
  /*
   * 0x00: blinking is permitted for the cursor that is currently shown.
   * SHOW_CURSOR sets it to (cursor_number == 0) - `move.w D2w,(0xd4,A5)`
   * at 0x00E6E424 sets Z, `seq D4b` at 0x00E6E42C turns that into the
   * Domain boolean, and 0x00E6E432 stores it.  Everything else clears it.
   */
  boolean blink_enable;
  uint8_t pad_01;   /* 0x01: Padding */
  /*
   * 0x02: the cursor image is currently painted on screen.  SHOW_CURSOR uses
   * it to decide whether the old cursor has to be erased (0x00E6E390) and
   * sets it after painting the new one (0x00E6E434); the blink callback uses
   * it after calling the unit's blink routine to pick the long interval
   * (0x00E6FF8E).
   */
  boolean cursor_painted;
  uint8_t pad_03;   /* 0x03: Padding */
  /*
   * 0x04: skip-one-tick counter.  SHOW_CURSOR sets it to 7 (0x00E6E438) so
   * that the cursor it just painted survives the next blink tick; the
   * callback clears it unconditionally (0x00E6FF9C), so only the very next
   * tick is skipped whatever the value.
   */
  uint16_t blink_defer;
} smd_time_com_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(smd_time_com_t, pad_01) == 0x01, "smd_time_com_t.pad_01");
_Static_assert(__builtin_offsetof(smd_time_com_t, pad_03) == 0x03, "smd_time_com_t.pad_03");
#endif

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(smd_time_com_t, blink_enable) == 0x00,
               "smd_time_com_t.blink_enable at 0x00 (00e6ff72 tst.b (A2))");
_Static_assert(__builtin_offsetof(smd_time_com_t, cursor_painted) == 0x02,
               "smd_time_com_t.cursor_painted at 0x02 (00e6ff8e tst.b (0x2,A2))");
_Static_assert(__builtin_offsetof(smd_time_com_t, blink_defer) == 0x04,
               "smd_time_com_t.blink_defer at 0x04 (00e6ff76 tst.w (0x4,A2))");
_Static_assert(sizeof(smd_time_com_t) == 6,
               "SMD_TIME_$COM is 0x00E273D6..0x00E273DB (MNK_$KTT_PTRS follows)");
#endif

/*
 * ============================================================================
 * External Data Declarations
 * ============================================================================
 */

/* SMD globals at 0x00E82B8C */
extern smd_globals_t SMD_GLOBALS;

/* Display unit array at 0x00E2E3FC */
/*
 * The 0x00E2E3FC region.  It is *not* an array of unit records: the first
 * 0x18 bytes are the two standalone eventcounts SMD_EC_1 (0x00E2E3FC) and
 * SMD_EC_2 (0x00E2E408), and the per-unit records follow at 0x00E2E414.  It
 * is declared as one byte block so that smd_$unit_rec()'s arithmetic is
 * literally the arithmetic the binary performs, and so that the block covers
 * exactly units 1..SMD_MAX_DISPLAY_UNITS:
 *   record(SMD_MAX_DISPLAY_UNITS) ends at
 *   (SMD_MAX_DISPLAY_UNITS + 1) * 0x10C - 0xF4
 *   = SMD_MAX_DISPLAY_UNITS * 0x10C + 0x18.
 */
extern uint8_t SMD_DISPLAY_UNITS[SMD_MAX_DISPLAY_UNITS * SMD_DISPLAY_UNIT_SIZE +
                                 0x18];

/*
 * Display info / hardware record table at 0x00E27376, one 0x60-byte entry per
 * unit, 1-based (use smd_$unit_info()).
 *
 * The image holds exactly SMD_DISPLAY_INFO_COUNT (= 1) entry: the table runs
 * 0x00E27376..0x00E273D5 and SMD_TIME_$COM starts at 0x00E273D6.  Nothing
 * reaches entry 2, because smd_$validate_unit rejects every unit but 1
 * (0x00E6D70A "cmpi.w #0x1,D0w" / 0x00E6D70E "bne").
 */
/*
 * SMD_DISPLAY_INFO_SECTION - keep the table inside the SMD_WIRED code segment.
 *
 * The SAU2 map's SMD_WIRED segment (0xE26F20, size 0x5E0) holds SMD_$DISP1_INT
 * first and its constant tables last: SMD_$CURSOR_TABLE 0xE272C6,
 * SMD_$CURSOR_PTABLE 0xE27366, SMD_$DISPLAY_COM (= SMD_DISPLAY_INFO) 0xE27376.
 * SMD_$DISP1_INT reaches the table with a 16-bit PC-relative operand:
 * `43 fa 04 46  lea (0x446,PC),A1' at 0xE26F2E (0xE26F30 + 0x446 = 0xE27376).
 * Emitted as ordinary `.bss' it lands tens of kilobytes from
 * smd/sau2/disp1_int.o and the R_68K_PC16 relocation overflows (source-uwxz),
 * so give it a section of its own, which the generated build/sau2/layout.ld
 * links at SMD_$DISPLAY_COM's map position after the SMD_WIRED routines (its
 * ANCHORS table in tools/gen_layout_ld.py keys it at 0xE27376, since the map
 * name differs) - the same technique the SVC dispatch tables use
 * (source-a5t8).
 * This is a code-segment cell, not an A5 module block, so it is outside the
 * `.moddata.<name>` scheme of docs/design-per-process-data.md (source-0i3).
 */
#if defined(ARCH_M68K)
#define SMD_DISPLAY_INFO_SECTION  __attribute__((section(".text.smd_display_info")))
#else
#define SMD_DISPLAY_INFO_SECTION
#endif

/*
 * SMD_TIME_$COM_SECTION - the same technique for SMD_TIME_$COM (0xE273D6, 6
 * bytes), the object that closes the SMD_WIRED segment immediately after
 * SMD_DISPLAY_INFO's single 0x60-byte entry.
 *
 * smd/sau2/blink_cursor_1.s reaches its `cursor_painted' byte the way the
 * image does, with `43 fa 01 8e  lea (0x18e,PC),A1' at 0xE27248 (0xE2724A +
 * 0x18E = 0xE273D8 = SMD_TIME_$COM + 2).  Left in ordinary `.bss' that
 * R_68K_PC16 relocation overflows exactly as SMD_DISPLAY_INFO's did
 * (source-xlo9), so it gets a section of its own, which the generated
 * build/sau2/layout.ld links at its map position, directly after
 * `.text.smd_display_info'.
 */
#if defined(ARCH_M68K)
#define SMD_TIME_$COM_SECTION  __attribute__((section(".text.smd_time_com")))
#else
#define SMD_TIME_$COM_SECTION
#endif

extern smd_display_info_t SMD_DISPLAY_INFO[SMD_DISPLAY_INFO_COUNT];

/*
 * The two standalone eventcounts at 0x00E2E3FC and 0x00E2E408 (bead
 * source-ufwn).  They are not separate objects: they occupy the first 0x18
 * bytes of the 0x00E2E3FC block declared above, which is why unit 1's record
 * only starts at 0x00E2E414.  SMD_$INIT initialises them by absolute address
 * (0x00E34D34 "move.l #0xe2e3fc,-(SP)" and 0x00E34D42 "pea (0xe2e408).l"),
 * so they must alias the block rather than sit somewhere else.
 */
#define SMD_EC_1 (*(ec_$eventcount_t *)&SMD_DISPLAY_UNITS[0x00])
#define SMD_EC_2 (*(ec_$eventcount_t *)&SMD_DISPLAY_UNITS[0x0C])

/* SMD_TIME module common block at 0x00E273D6 */
extern smd_time_com_t SMD_TIME_$COM;

/*
 * Two initialiser longwords at 0x00E173D4 (0x00000400 and 0x00000000) that
 * SMD_$INIT's case-0 prologue copies into the display unit record's +0x100 and
 * +0x104 fields (0x00E34D96 movea.l #0xe173d4,A2 / move.l (A2)+,(0x118,A0) /
 * move.l (A2)+,(0x11c,A0)).
 */
extern const uint32_t smd_$unit_init_params[2];

/* TIME_$CLOCKH - high word of system clock */

/*
 * ============================================================================
 * Cursor Pattern
 * ============================================================================
 * One per cursor number; SMD_$CURSOR_PTABLE[n] points at it.
 *
 * Layout recovered from SMD_$LOAD_CRSR_BITMAP (0x00E6FCA0-0x00E6FCD0), which
 * validates each argument to 1..16 (width, height) or 0..16 (the hot spots)
 * and then stores:
 *     move.w D4w,(A2)            -> width
 *     move.w D2w,(0x2,A2)        -> height
 *     move.w D5w,(0x4,A2)        -> hot_x
 *     neg.w D6w / add.w (0x2,A2),D6w / subq.w #1,D6w / move.w D6w,(0x6,A2)
 *                                -> hot_y_adj = height - hot_y - 1
 * followed by `height` bitmap words starting at (0x8,A2) and a clear of the
 * remaining words up to index 16.  Each word is one raster line, which is why
 * the word count is the height.
 *
 * SMD_$SHOW_CURSOR uses them as
 *     x_left   = pos.x - hot_x                     (clamped to >= 0)
 *     x_right  = x_left + width                    (clamped to hw->max_x + 1)
 *     y_bottom = pos.y + hot_y_adj                 (clamped to hw->max_y)
 *     y_top    = y_bottom - height                 (clamped to -1)
 */
typedef struct smd_cursor_pattern_t {
  int16_t width;        /* 0x00: cursor width in pixels, 1..16 */
  int16_t height;       /* 0x02: cursor height in raster lines, 1..16 */
  int16_t hot_x;        /* 0x04: hot spot X offset */
  int16_t hot_y_adj;    /* 0x06: height - hot spot Y - 1 */
  uint16_t bitmap[16];  /* 0x08: one word per raster line */
} smd_cursor_pattern_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(smd_cursor_pattern_t, width) == 0x00, "smd_cursor_pattern_t.width");
_Static_assert(__builtin_offsetof(smd_cursor_pattern_t, height) == 0x02, "smd_cursor_pattern_t.height");
_Static_assert(__builtin_offsetof(smd_cursor_pattern_t, hot_x) == 0x04, "smd_cursor_pattern_t.hot_x");
#endif

#if defined(ARCH_M68K)
_Static_assert(offsetof(smd_cursor_pattern_t, hot_y_adj) == 0x06, "pat hot_y");
_Static_assert(offsetof(smd_cursor_pattern_t, bitmap) == 0x08, "pat bitmap");
#endif

/*
 * The four built-in cursor patterns, 0x00E272C6 (map: SMD_$CURSOR_TABLE),
 * 0x28 bytes apart, ending exactly where SMD_$CURSOR_PTABLE starts.
 */
extern smd_cursor_pattern_t SMD_$CURSOR_TABLE[4];

/*
 * Cursor pointer table, 0x00E27366 (map: SMD_$CURSOR_PTABLE; the tree used to
 * spell it SMD_CURSOR_PTABLE).  Four pointers, one per cursor number, running
 * 0x00E27366..0x00E27375 -- SMD_$DISPLAY_COM starts at 0x00E27376.  The image
 * values are &SMD_$CURSOR_TABLE[0..3].
 */
extern smd_cursor_pattern_t *SMD_$CURSOR_PTABLE[4];

/*
 * The blink-routine pointer table is smd_globals_t.blink_func -- see the union
 * at the end of that record.  There is no second object at SMD_GLOBALS +
 * 0x1DA0 (bead source-q85g).
 */

/* Request lock ID for cursor operations */
#define smd_$request_lock 8

/* Lock data used by SMD_$ACQ_DISPLAY / KBD_$* / TERM_$CONTROL calls.
 * Address: 0x00E6D92C - a 16-bit word containing 0x0000, located in the
 * code segment (read-only in the original).  Several call sites pass its
 * address as an int16_t or uint16_t pointer ("line"/"lock" argument). */
extern uint16_t SMD_ACQ_LOCK_DATA;

/* Lock data for synchronous BLT / cursor operations.
 * Address: 0x00E6DFF8 - a 16-bit word containing 0x0001 (code segment). */
extern int16_t SMD_SYNC_LOCK_DATA;

/*
 * Three more code-region constant cells that the original passes by reference
 * (bead source-2c9v).  Their *values* were read with `gsk read`; six SMD files
 * used to declare a variable holding the cell's ADDRESS and pass that
 * variable's address instead, so the callee read the low half of 0x00E6E59A
 * rather than 0xFFFF.
 *
 *   0x00E6E59A: "ff ff" - the word -1.  SHOW_CURSOR takes its second argument
 *               as a word (0x00E6E1EA "move.w (A1),D6w") and treats -1 as
 *               "keep the display's current cursor number" (0x00E6E256
 *               "cmpi.w #-0x1,D6w").
 *   0x00E6E458: "ff"    - the Domain boolean true.  SHOW_CURSOR takes its
 *               third argument as a byte (0x00E6E1EE "move.b (A2),D5b") and
 *               uses it as the "block until the display lock is ours" flag
 *               (0x00E6E35E).  Also SMD_$XOR_CURSOR's erase flag.
 *   0x00E6E45A: "00"    - the Domain boolean false (SMD_$XOR_CURSOR's draw
 *               flag, SHOW_CURSOR 0x00E6E3FA "pea (0x5e,PC)").
 */
extern const int16_t SMD_MINUS_ONE_DATA;
extern const boolean SMD_TRUE_DATA;
extern const boolean SMD_FALSE_DATA;

/* Constant word 0x0001 at 0x00E6D92A (code segment, read with gsk).  Passed
 * by reference as TERM_$SET_REAL_LINE_DISCIPLINE's `discipline` argument
 * (SMD_$ASSOC 0x00E6D8B8 "pea (0x70,PC)") and as SMD_$ACQ_DISPLAY's lock word
 * (SMD_$LOAD_FONT 0x00E6DCE6 "pea (-0x3be,PC)"). */
extern int16_t SMD_ONE_LOCK_DATA;

/* Exclusion lock protecting the tracking-rectangle list and cursor state.
 * Address: 0x00E2E520 (ml_$exclusion_t, 0x12 bytes).  Initialised by
 * SMD_$INIT via ML_$EXCLUSION_INIT.  It sits immediately after
 * SMD_DISPLAY_UNITS inside the map segment "D35 E2E3FC SMD_$WIRED_DATA
 * loaded at 12FBFC, size = 13C": 0x00E2E3FC + 0x124 = 0x00E2E520, and the
 * segment ends at 0x00E2E538. */
extern ml_$exclusion_t smd_$trk_rect_mutex;

/* SMD_$DISP1_INT - display-1 BLT/scroll interrupt handler.
 * Original address: 0x00E26F20, emitted as smd/sau2/disp1_int.s.
 *
 * Not a C-callable routine: it is an interrupt vector that raises the IPL,
 * dispatches on the hardware record's lock_state through a jump table and
 * leaves through PROC1_$INT_EXIT / PROC1_$INT_ADVANCE instead of an `rts`.
 * The prototype exists only so that SMD_$INTERRUPT_INIT can take its address
 * and so that SMD_$START_BLT's trampoline can name it.
 *
 * Resolved (bead source-8xb): the label conflict was an off-by-two.
 * SMD_$INTERRUPT_INIT loads it with "lea (-0x366,PC),A0" at 0x00E27284, and
 * 0x00E27286 - 0x366 = 0x00E26F20, which is where the handler's
 * "movem.l {...},-(SP)" starts.  0x00E26F1E is the byte immediately before
 * it and really is ROUTE_$ROUTING (see route/route_internal.h). */
void SMD_$DISP1_INT(void);

/*
 * The four CRASH_SYSTEM status constants in SMD_$DISP1_INT's code region,
 * 0x00E27026..0x00E27035.  They are defined once, in smd/sau2/disp1_int.s,
 * at exactly those offsets, because both SMD_$DISP1_INT and the scroll-BLT
 * setup subroutine reach them with `pea (d16,PC)`.
 *
 *   0x00E27026  0x00130007  SMD_Invalid_Direction_From_SM_Err
 *   0x00E2702A  0x00130008  SMD_Invalid_BLT_In_Use_Err
 *   0x00E2702E  0x0013001C  SMD_Invalid_BLT_Done_Interrupt_Err
 *   0x00E27032  0x0013001D  SMD_Invalid_Interrupt_Routine_State_Err
 */
extern const status_$t SMD_Invalid_Direction_From_SM_Err;
extern const status_$t SMD_Invalid_BLT_In_Use_Err;
extern const status_$t SMD_Invalid_BLT_Done_Interrupt_Err;
extern const status_$t SMD_Invalid_Interrupt_Routine_State_Err;

/*
 * smd_$setup_scroll_blt - SAU-specific scroll BLT register setup, 0x00E27070.
 * Emitted byte for byte in smd/sau2/disp1_int.s, where it shares its body with
 * the interrupt-level entry smd_$disp1_setup_blt at 0x00E27036.
 *
 * NOT C-CALLABLE.  Its arguments arrive in REGISTERS - A0 = the BLT register
 * block, A1 = the display hardware record - and its only caller is
 * SMD_$START_SCROLL's own assembly, `jsr (0x150,A5)` at 0x00E15C8A with
 * A5 = 0x00E26F20.  The prototype exists so the symbol has a declaration;
 * C code must not call it (bead source-a2ip).
 */
uint16_t smd_$setup_scroll_blt(SMD_HW_REG_PTR blt_regs, smd_display_hw_t *hw);

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
 * smd_$reset_unit_display - Reset one unit's display state to defaults.
 *
 * Resets the clip window to the full screen, drops every loaded font, writes
 * the unit's parameter block, and (when the unit is not owned, or is
 * borrowed, and `full` is true) clears the bottom of display memory.
 *
 * Parameters:
 *   unit - display unit number (1-based)
 *   full - Domain boolean; when true the display-memory clear is performed
 *
 * Original address: 0x00E6D736
 */
void smd_$reset_unit_display(int16_t unit, boolean full);

/*
 * smd_$reset_display_globals - Reset the module-wide cursor/tracking state.
 *
 * Parameters:
 *   unit - display unit number (1-based)
 *   full - Domain boolean; when true the locator and event-queue state is
 *          reset as well
 *
 * Original address: 0x00E6D7E2
 */
void smd_$reset_display_globals(int16_t unit, boolean full);

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
void SMD_$START_SCROLL(smd_display_hw_t *hw, SMD_HW_REG_PTR ctrl_regs);

/*
 * SMD_$CONTINUE_SCROLL - Continue scroll operation
 *
 * Continues a hardware scroll operation. If the remaining scroll amount
 * is zero, marks the scroll as complete. Otherwise, sets up the next
 * scroll step.
 *
 * Parameters:
 *   hw        - Display hardware info
 *   ctrl_regs - Display controller register base (the unit record's +0xFC
 *               field; SMD_$REL_DISPLAY pushes it by value at 0x00E6EC46,
 *               and the implementation at 0x00E15C9C writes the BLT control
 *               word straight through it)
 *
 * Original address: 0x00E272B2
 */
void SMD_$CONTINUE_SCROLL(smd_display_hw_t *hw, SMD_HW_REG_PTR ctrl_regs);

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
void SMD_$START_BLT(uint16_t *params, smd_display_hw_t *hw,
                    SMD_HW_REG_PTR hw_regs);

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

/* SMD_$COPY_FONT_TO_MD_HDM (0x00E1D750): declared in smd/smd.h -- DTTY_$LOAD_FONT
 * calls it from outside smd/ (bead source-3uo). */

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

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(smd_hw_blt_regs_t, bit_pos) == 0x02, "smd_hw_blt_regs_t.bit_pos");
_Static_assert(__builtin_offsetof(smd_hw_blt_regs_t, mask) == 0x04, "smd_hw_blt_regs_t.mask");
_Static_assert(__builtin_offsetof(smd_hw_blt_regs_t, pattern) == 0x06, "smd_hw_blt_regs_t.pattern");
_Static_assert(__builtin_offsetof(smd_hw_blt_regs_t, y_extent) == 0x08, "smd_hw_blt_regs_t.y_extent");
_Static_assert(__builtin_offsetof(smd_hw_blt_regs_t, x_extent) == 0x0A, "smd_hw_blt_regs_t.x_extent");
_Static_assert(__builtin_offsetof(smd_hw_blt_regs_t, y_start) == 0x0C, "smd_hw_blt_regs_t.y_start");
_Static_assert(__builtin_offsetof(smd_hw_blt_regs_t, x_start) == 0x0E, "smd_hw_blt_regs_t.x_start");
_Static_assert(sizeof(smd_hw_blt_regs_t) == 0x10, "smd_hw_blt_regs_t size");

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
  uint32_t reserved;     /* 0x00: never written by SMD_$UTIL_INIT */
  /* 0x04: the unit record's display memory base (record +0x108).
   * 0x00E6DF08 "move.l (0x14,A1,D1*0x1),(0x4,A0)" with A1+D1 = the biased
   * record pointer, so (0x14,...) is record offset 0x108. */
  uint32_t display_base;
  /* 0x08: the unit record's display controller registers (record +0xFC).
   * 0x00E6DF0E "move.l (0x8,A1,D1*0x1),(0x8,A0)".  This is the pointer that
   * SMD_$CLEAR_WINDOW and SMD_$DRAW_BOX use as the BLT register block. */
  smd_hw_blt_regs_t *ctrl_regs;
  /* 0x0C: the unit's hardware info record (record +0x00).
   * 0x00E6DF18 "move.l (-0xf4,A1),(0xc,A0)". */
  smd_display_hw_t *hw;
  status_$t status;      /* 0x10: Status code */
} smd_util_ctx_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(smd_util_ctx_t, reserved) == 0x00, "smd_util_ctx_t.reserved");
#endif

#if defined(ARCH_M68K)
_Static_assert(offsetof(smd_util_ctx_t, display_base) == 0x04, "ctx base");
_Static_assert(offsetof(smd_util_ctx_t, ctrl_regs) == 0x08, "ctx regs");
_Static_assert(offsetof(smd_util_ctx_t, hw) == 0x0C, "ctx hw");
_Static_assert(offsetof(smd_util_ctx_t, status) == 0x10, "ctx status");
_Static_assert(sizeof(smd_util_ctx_t) == 0x14, "smd_util_ctx_t size");
#endif

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
 *   param7  - Read into A0 at 0x00E707B4 ("movea.l (0x3c,SP),A0") and then
 *             never used; SMD_$DRAW_BOX passes ctx.hw
 *
 * Original address: 0x00E84974
 */
void SMD_$VERT_LINE(int16_t *x, int16_t *y1, int16_t *y2, void *param4,
                    smd_hw_blt_regs_t *hw_regs, uint16_t *control,
                    void *param7);

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
void SHOW_CURSOR(const uint32_t *pos, const int16_t *cursor_num,
                 const boolean *blocking);

/*
 * SMD_$XOR_CURSOR / SMD_$OR_CURSOR - Low-level cursor drawing
 *
 * Called by the blink and show-cursor paths to draw or erase the cursor
 * directly in display memory: SMD_$XOR_CURSOR combines the pattern with the
 * frame buffer with EOR, SMD_$OR_CURSOR with OR.
 *
 * Both are ten-byte hand-written trampolines in the map's
 * "D E26F20 SMD_WIRED size = 5E0" block - `lea (d16,PC),A0` (A0 = 0x00E26F20,
 * the block base) followed by `jmp` into the module's code segment - and they
 * share one body at 0x00E15B90 (XOR entry) / 0x00E15B9A (OR entry).  All of
 * it is emitted in smd/sau2/cursor_thunks.s; there is no C model, so these
 * prototypes resolve only in the SAU2 build.
 *
 * Original addresses: 0x00E2720E (XOR), 0x00E27218 (OR)
 */
/*
 * The last two arguments are pushed as *values* by every caller
 * (SHOW_CURSOR 0x00E6E396/0x00E6E39A and 0x00E6E3F2/0x00E6E3F6:
 * `move.l (0x8,A3),-(SP)` / `move.l (0x14,A3),-(SP)`), not by reference.
 * They are the display unit record's controller register base (+0xFC) and
 * display memory base (+0x108).
 */
boolean SMD_$XOR_CURSOR(int16_t *cursor_num, uint32_t *cursor_pos,
                                  int16_t *bounds, smd_display_hw_t *hw,
                                  const boolean *erase_flag,
                                  uint32_t display_base,
                                  SMD_HW_REG_PTR ctrl_regs);

boolean SMD_$OR_CURSOR(int16_t *cursor_num, uint32_t *cursor_pos,
                                  int16_t *bounds, smd_display_hw_t *hw,
                                  const boolean *erase_flag,
                                  uint32_t display_base,
                                  SMD_HW_REG_PTR ctrl_regs);

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

/*
 * SMD_$DTTE_EC - the "display transfer" eventcount SMD_$GET_EC key 0 returns.
 *
 * SMD_$GET_EC loads A4 = 0xE2DC90 (00e6fdc6) and pushes it for key 0
 * (00e6fdf4).  That address is TERM_$DATA.dtte[0], so the leading 12 bytes of
 * the first DTTE entry (dtte_t.reserved_00) double as an ec_$eventcount_t.
 * The old `extern ec_$eventcount_t DTTE;` here collided with the DTTE array
 * declarations in term/ and sio/ (bead source-3uo).
 */
#define SMD_$DTTE_EC (*(ec_$eventcount_t *)&DTTE[0])

/* FIM_$QUIT_EC / FIM_$QUIT_VALUE come from fim/fim.h */

/*
 * smd_$unit_rec - the real per-unit display record for `unit_num`.
 *
 * Mirrors the address computation every SMD function performs:
 *     movea.l #0xe2e3fc,A0 ; muls.w #0x10c,D0 ; lea (0,A0,D0*1),A3
 * and then reaches the record through displacements starting at -0xF4.
 * `unit_num` is signed because the original uses a *signed* multiply
 * (muls.w #0x10c, e.g. 0x00E6E218 in SMD_$SHOW_CURSOR).
 */
static inline smd_display_unit_t *smd_$unit_rec(int16_t unit_num) {
  return (smd_display_unit_t *)(SMD_DISPLAY_UNITS +
                                (int32_t)unit_num * SMD_DISPLAY_UNIT_SIZE -
                                0xF4);
}

/*
 * smd_$unit_info - the display info entry for `unit_num`.
 *
 * The info table is addressed exactly like the unit records: the original
 * computes base + unit*0x60 and then subtracts 0x60, i.e. the table is
 * 1-based on the unit number (smd_$validate_unit 0x00E6D722
 * "tst.w (-0x60,A0,D1*0x1)", SMD_$INQ_DISP_TYPE 0x00E6DE4C,
 * SMD_$INQ_DISP_INFO 0x00E70172).
 */
static inline smd_display_info_t *smd_$unit_info(int16_t unit_num) {
  return &SMD_DISPLAY_INFO[unit_num - 1];
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

/* The "auxiliary" block *is* the display unit record; see
 * smd_display_unit_t above, which describes all 0x10C bytes of it, and
 * smd_$unit_rec(), which is the only accessor. */

/* Lock ID for respond/borrow operations */
#define smd_$respond_lock 7

/* Uppercase aliases for lock constants (used in some code) */
#define SMD_REQUEST_LOCK smd_$request_lock
#define SMD_RESPOND_LOCK smd_$respond_lock

/*
 * The borrow-signalling eventcount is SMD_EC_2 itself: SMD_$BORROW_DISPLAY
 * advances it by absolute address (0x00E6F61E "pea (0xe2e408).l"), and
 * SMD_$INIT initialises 0x00E2E408 as one of its two eventcounts
 * (0x00E34D42).  It is an alias, not a second object.
 */
#define SMD_BORROW_EC SMD_EC_2

/*
 * The per-unit borrow-response bytes are SMD_GLOBALS.response_pending: both
 * writer and reader address them as (0x1d99,A5) indexed by the 1-based unit
 * number (SMD_$SEND_RESPONSE 0x00E6F4F8/0x00E6F4FC and SMD_$BORROW_DISPLAY
 * 0x00E6F64C/0x00E6F650), i.e. SMD_GLOBALS + 0x1D99 + unit, which is
 * response_pending[unit - 1].  The old SMD_BORROW_RESPONSE extern claimed a
 * separate table at 0x00E84924 - that address is SMD_GLOBALS.default_unit.
 */

/*
 * The value SMD_$BORROW_DISPLAY hands CRASH_SYSTEM when SMD_$CLEAR_WINDOW
 * fails is NOT a string: 0x00E6F6E4 "pea (0x16,PC)" resolves to 0x00E6F6FC,
 * a constant longword 0x0013000E in the code region.  It is emitted as a
 * file-static status cell in smd/borrow_display.c, so no extern is needed.
 */

/*
 * smd_$init_display_state - Initialize display state for borrow/associate
 *
 * The whole body of the exported SMD_$INIT_STATE wrapper (0x00E6F818), and
 * also called by SMD_$BORROW_DISPLAY (0x00E6F69C).  Acquires the display,
 * pokes the SMD_$ACQ_DISPLAY result into the unit's controller register,
 * calls smd_$reset_unit_display(unit, full), re-enables video when `full`
 * is true, and releases the display.  Runs on the caller's A5.
 *
 * Parameters:
 *   options    - Init options flag (negative = full init)
 *   status_ret - Status return
 *
 * Original address: 0x00E6F514
 */
void smd_$init_display_state(int8_t options, status_$t *status_ret);

/* Request queue event counts */
extern ec_$eventcount_t SMD_REQUEST_EC_WAIT;   /* At 0x00E2E3FC - wait for space */
extern ec_$eventcount_t SMD_REQUEST_EC_SIGNAL; /* At 0x00E2E408 - signal new request */

#endif /* SMD_INTERNAL_H */
