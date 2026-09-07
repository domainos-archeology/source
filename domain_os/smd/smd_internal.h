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

/*
 * Packed cursor position helpers (SMD_POS_X / SMD_POS_Y / SMD_POS_MAKE) and
 * the smd_cursor_pos_t typedef they go with now live in smd/smd.h, where the
 * evidence for the layout is recorded.
 */

#if defined(ARCH_M68K)
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
 * ml_$exclusion_t_00e2e520.
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
  uint8_t pad_1c[0x16]; /* 0x1C-0x31: Unknown */
  /* 0x32: keyboard cursor position, packed (see smd_cursor_pos_t).
   * SMD_$INQ_KBD_CURSOR 0x00E6E116 "move.l (-0x2e,A0),(A1)" with
   * A0 = 0xE27376 + unit*0x60, and smd_$reset_display_globals 0x00E6D80E
   * clears it. */
  smd_cursor_pos_t kbd_cursor_pos;
  uint16_t field_36;    /* 0x36: cleared by smd_$reset_display_globals
                         *       (0x00E6D812 clr.w (-0x2a,A0)) */
  /* 0x38: keyboard cursor type, returned by SMD_$INQ_KBD_CURSOR
   * (0x00E6E112 "move.b (-0x28,A0),D0b") and cleared by
   * smd_$reset_display_globals (0x00E6D816). */
  uint8_t kbd_cursor_type;
  uint8_t pad_39[0x27]; /* 0x39-0x5F: Remaining fields */
} smd_display_info_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(smd_display_info_t, kbd_cursor_pos) == 0x32, "di kbdpos");
_Static_assert(offsetof(smd_display_info_t, field_36) == 0x36, "di 0x36");
_Static_assert(offsetof(smd_display_info_t, kbd_cursor_type) == 0x38, "di kbdty");
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
  int16_t default_unit;        /* 0x1D98: unit SHOW_CURSOR validates (0x00E6E1F0) */
  /* 0x1D9A: per-unit "response pending" bytes.  SMD_$SEND_RESPONSE addresses
   * them as (0x1D99,A5 + unit) with unit 1-based (0x00E6F4FC), so unit N is
   * response_pending[N - 1]. */
  int8_t response_pending[2];
  int16_t previous_unit;       /* 0x1D9C: unit the cursor was last shown on
                                *         (SHOW_CURSOR 0x00E6E200/0x00E6E444) */
  uint16_t unit_change_count;  /* 0x1D9E: SMD_$SET_UNIT_CURSOR_POS 0x00E6E7C6 */
  uint16_t last_idm_button;    /* 0x1DA0: SMD_$GET_IDM_EVENT 0x00E6EE7C/0x00E6EE88 */
  boolean power_off_reported;  /* 0x1DA2: SMD_$DM_COND_EVENT_WAIT 0x00E6F078 */
  uint8_t pad_1da3;            /* 0x1DA3: Padding */
} smd_globals_t;

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
_Static_assert(sizeof(smd_globals_t) == 0x1DA4, "smd_globals_t size");
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

/*
 * ============================================================================
 * Cursor Blink State
 * ============================================================================
 * State for cursor blinking.
 * Base address: 0x00E273D6
 */
typedef struct smd_blink_state_t {
  /* Domain booleans: written with clr.b / st / seq and tested with
   * tst.b + bpl/bmi (SMD_$BLINK_CURSOR_CALLBACK 0x00E6FF72 / 0x00E6FF8E,
   * SHOW_CURSOR 0x00E6E390), so they must be signed. */
  boolean smd_time_com;   /* 0x00: Time communication flag */
  uint8_t pad_01;         /* 0x01: Padding */
  boolean blink_flag;     /* 0x02: Blink state (0xFF = enabled) */
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

/* Display info table at 0x00E27376 */
extern smd_display_info_t SMD_DISPLAY_INFO[];

/* Event counts at 0x00E2E3FC, 0x00E2E408 */
extern ec_$eventcount_t SMD_EC_1;
extern ec_$eventcount_t SMD_EC_2;

/* Blink state at 0x00E273D6 */
extern smd_blink_state_t SMD_BLINK_STATE;

/*
 * Two initialiser longwords at 0x00E173D4 (0x00000400 and 0x00000000) that
 * SMD_$INIT's case-0 prologue copies into the display unit record's +0x100 and
 * +0x104 fields (0x00E34D92 movea.l #0xe173d4,A2 / move.l (A2)+,(0x118,A0) /
 * move.l (A2)+,(0x11c,A0)).
 */
extern const uint32_t smd_$unit_init_params[2];

/* Default display unit at 0x00E84924 */
extern uint16_t SMD_DEFAULT_DISPLAY_UNIT;

/* TIME_$CLOCKH - high word of system clock */
extern uint32_t TIME_$CLOCKH;

/*
 * ============================================================================
 * Cursor Pattern
 * ============================================================================
 * One per cursor number; SMD_CURSOR_PTABLE[n] points at it.
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

#if defined(ARCH_M68K)
_Static_assert(offsetof(smd_cursor_pattern_t, hot_y_adj) == 0x06, "pat hot_y");
_Static_assert(offsetof(smd_cursor_pattern_t, bitmap) == 0x08, "pat bitmap");
#endif

/* Cursor pointer table at 0x00E27366 - 4 pointers to cursor patterns */
extern smd_cursor_pattern_t *SMD_CURSOR_PTABLE[4];

/* Blink function pointer table at SMD_GLOBALS + 0x1DA0 */
typedef void (*smd_blink_func_t)(void);
extern smd_blink_func_t SMD_BLINK_FUNC_PTABLE[SMD_MAX_DISPLAY_UNITS];

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

/* Constant word 0x0001 at 0x00E6D92A (code segment, read with gsk).  Passed
 * by reference as TERM_$SET_REAL_LINE_DISCIPLINE's `discipline` argument
 * (SMD_$ASSOC 0x00E6D8B8 "pea (0x70,PC)") and as SMD_$ACQ_DISPLAY's lock word
 * (SMD_$LOAD_FONT 0x00E6DCE6 "pea (-0x3be,PC)"). */
extern int16_t SMD_ONE_LOCK_DATA;

/* Exclusion lock protecting the tracking-rectangle list and cursor state.
 * Address: 0x00E2E520 (ml_$exclusion_t, 18 bytes).  Initialised by
 * SMD_$INIT via ML_$EXCLUSION_INIT. */
extern ml_$exclusion_t ml_$exclusion_t_00e2e520;

/* SMD_$DISP1_INT - display interrupt handler (assembly, not yet emitted).
 * Original address: 0x00E26F20.
 *
 * Resolved (bead source-8xb): the label conflict was an off-by-two.
 * SMD_$INTERRUPT_INIT loads it with "lea (-0x366,PC),A0" at 0x00E27284, and
 * 0x00E27286 - 0x366 = 0x00E26F20, which is where the handler's
 * "movem.l {...},-(SP)" starts.  0x00E26F1E is the byte immediately before
 * it and really is ROUTE_$ROUTING (see route/route_internal.h). */
void SMD_$DISP1_INT(void);

/* smd_$setup_scroll_blt - SAU-specific scroll BLT register setup.
 * Implemented in smd/sau2/scroll_blt_setup.s.  Original address: 0x00E27070 */
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
void smd_$send_loc_event(uint16_t unit, uint16_t type, uint32_t pos,
                         uint16_t buttons);

/*
 * SMD_$XOR_CURSOR - Low-level cursor drawing
 *
 * Called by blink routines to actually draw/erase cursor.
 *
 * Original address: 0x00E2720E
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
