/*
 * smd/smd.h - Screen Management Display Module Public API
 *
 * Provides screen/display management functions for Domain/OS.
 * This module handles:
 *   - Display initialization and configuration
 *   - Video control (blanking, enable/disable)
 *   - Cursor management (keyboard cursor, mouse/trackpad cursor)
 *   - Font loading and text rendering
 *   - BLT (bit block transfer) operations
 *   - Hidden display memory (HDM) allocation
 *   - Tracking rectangles for mouse events
 *   - Scrolling operations
 *
 * Original addresses: 0x00E15CCE - 0x00E84974 (scattered)
 */

#ifndef SMD_H
#define SMD_H

#include "base/base.h"

/*
 * ============================================================================
 * Display Info Structure (returned by SMD_$INQ_DISP_INFO)
 * ============================================================================
 * Size: 10 bytes
 */
typedef struct smd_disp_info_result_t {
    uint16_t    display_type;       /* 0x00: Display type code */
    uint16_t    bits_per_pixel;     /* 0x02: Bits per pixel (4, 8, etc.) */
    uint16_t    num_planes;         /* 0x04: Number of planes (4, 8, etc.) */
    uint16_t    height;             /* 0x06: Display height in pixels */
    uint16_t    width;              /* 0x08: Display width in pixels */
} smd_disp_info_result_t;

/*
 * ============================================================================
 * HDM Position Structure (returned by SMD_$ALLOC_HDM)
 * ============================================================================
 * Represents a position in hidden display memory.
 * Size: 4 bytes
 */
typedef struct smd_hdm_pos_t {
    uint16_t    x;                  /* 0x00: X coordinate */
    uint16_t    y;                  /* 0x02: Y coordinate */
} smd_hdm_pos_t;

/*
 * ============================================================================
 * Tracking Rectangle Structure
 * ============================================================================
 * Defines a region for mouse tracking events.
 * Size: 8 bytes
 */
typedef struct smd_track_rect_t {
    int16_t     x1;                 /* 0x00: Left X */
    int16_t     y1;                 /* 0x02: Top Y */
    int16_t     x2;                 /* 0x04: Right X */
    int16_t     y2;                 /* 0x06: Bottom Y */
} smd_track_rect_t;

/*
 * ============================================================================
 * Cursor Position Structure
 * ============================================================================
 * Size: 4 bytes
 */
typedef struct smd_cursor_pos_t {
    int16_t     x;                  /* 0x00: X position */
    int16_t     y;                  /* 0x02: Y position */
} smd_cursor_pos_t;

/*
 * ============================================================================
 * Video Control Flags
 * ============================================================================
 */
#define SMD_VIDEO_ENABLE        0x80    /* Video output enabled */
#define SMD_VIDEO_DISABLE       0x00    /* Video output disabled */

/*
 * ============================================================================
 * Initialization Functions
 * ============================================================================
 */

/*
 * SMD_$INIT - Initialize SMD subsystem
 *
 * Performs full initialization of the display management subsystem.
 * Called during system startup.
 *
 * Original address: 0x00E34D2C
 */
void SMD_$INIT(void);

/*
 * SMD_$COLD_INIT - Cold initialization
 *
 * Minimal initialization for cold boot (currently a no-op stub).
 *
 * Original address: 0x00E34F00
 */
void SMD_$COLD_INIT(void);

/*
 * SMD_$INIT_BLINK - Initialize cursor blinking
 *
 * Sets up the cursor blink timer and state.
 *
 * Original address: 0x00E34EB2
 */
void SMD_$INIT_BLINK(void);

/*
 * SMD_$WS_INIT - Workstation initialization
 *
 * Per-workstation SMD initialization.
 *
 * Original address: 0x00E6DE58
 */
void SMD_$WS_INIT(void);

/*
 * SMD_$UTIL_INIT - Utility initialization
 *
 * Initialize SMD utility functions.
 *
 * Original address: 0x00E6DED4
 */
void SMD_$UTIL_INIT(void);

/*
 * ============================================================================
 * Display Association Functions
 * ============================================================================
 */

/*
 * SMD_$ASSOC - Associate display with process
 *
 * Associates a display unit with the current process's address space.
 *
 * Parameters:
 *   unit - Display unit number
 *   asid - Address space ID to associate (NULL uses current)
 *   status_ret - Status return
 *
 * Original address: 0x00E6D882
 */
void SMD_$ASSOC(uint16_t *unit, uint16_t *asid, status_$t *status_ret);

/*
 * SMD_$DISSOC - Dissociate display from process
 *
 * Removes display association from a process.
 *
 * Original address: 0x00E70106
 */
void SMD_$DISSOC(void);

/*
 * SMD_$ACQ_DISPLAY - Acquire display for exclusive access
 *
 * Original address: 0x00E6EB42
 */
uint16_t SMD_$ACQ_DISPLAY(void *lock_data);

/*
 * SMD_$REL_DISPLAY - Release display lock
 *
 * Original address: 0x00E6EC10
 */
void SMD_$REL_DISPLAY(void);

/*
 * SMD_$BORROW_DISPLAY - Temporarily borrow display
 *
 * Original address: 0x00E6F584
 */
void SMD_$BORROW_DISPLAY(uint16_t *unit, status_$t *status_ret);

/*
 * SMD_$RETURN_DISPLAY - Return borrowed display
 *
 * Original address: 0x00E6F700
 */
void SMD_$RETURN_DISPLAY(status_$t *status_ret);

/*
 * ============================================================================
 * Display Information Functions
 * ============================================================================
 */

/*
 * SMD_$INQ_DISP_TYPE - Inquire display type
 *
 * Returns the display type code for a unit.
 *
 * Parameters:
 *   unit - Display unit number
 *
 * Returns:
 *   Display type code (0 if invalid)
 *
 * Original address: 0x00E6DE1C
 */
uint16_t SMD_$INQ_DISP_TYPE(uint16_t *unit);

/*
 * SMD_$INQ_DISP_INFO - Inquire display information
 *
 * Returns detailed display information.
 *
 * Parameters:
 *   unit - Display unit number
 *   info - Pointer to info structure to fill
 *   status_ret - Status return
 *
 * Original address: 0x00E70124
 */
void SMD_$INQ_DISP_INFO(uint16_t *unit, smd_disp_info_result_t *info, status_$t *status_ret);

/*
 * SMD_$INQ_DISP_UID - Inquire display UID
 *
 * Original address: 0x00E70062
 */
void SMD_$INQ_DISP_UID(uint16_t *unit, uid_t *uid, status_$t *status_ret);

/*
 * SMD_$N_DEVICES - Get number of display devices
 *
 * Original address: 0x00E70024
 */
uint16_t SMD_$N_DEVICES(void);

/*
 * ============================================================================
 * Hidden Display Memory Functions
 * ============================================================================
 */

/*
 * SMD_$ALLOC_HDM - Allocate hidden display memory
 *
 * Allocates a region of off-screen display memory.
 *
 * Parameters:
 *   size - Size to allocate (in scanlines)
 *   pos - Returned position in HDM
 *   status_ret - Status return
 *
 * Original address: 0x00E6D92E
 */
void SMD_$ALLOC_HDM(uint16_t *size, smd_hdm_pos_t *pos, status_$t *status_ret);

/*
 * SMD_$FREE_HDM - Free hidden display memory
 *
 * Original address: 0x00E6DA3A
 */
void SMD_$FREE_HDM(smd_hdm_pos_t *pos, status_$t *status_ret);

/*
 * ============================================================================
 * Video Control Functions
 * ============================================================================
 */

/*
 * SMD_$VIDEO_CTL - Video control
 *
 * Enables or disables video output.
 *
 * Parameters:
 *   flags - Video control flags (SMD_VIDEO_ENABLE/SMD_VIDEO_DISABLE)
 *   status_ret - Status return
 *
 * Original address: 0x00E6F838
 */
void SMD_$VIDEO_CTL(uint8_t *flags, status_$t *status_ret);

/*
 * SMD_$UNBLANK - Unblank display
 *
 * Restores display output after screen blanking.
 *
 * Original address: 0x00E6EFB4
 */
void SMD_$UNBLANK(void);

/*
 * SMD_$SET_BLANK_TIMEOUT - Set blank timeout
 *
 * Sets the timeout before display blanking occurs.
 *
 * Original address: 0x00E6F192
 */
void SMD_$SET_BLANK_TIMEOUT(uint32_t *timeout, status_$t *status_ret);

/*
 * SMD_$INQ_BLANK_TIMEOUT - Inquire blank timeout
 *
 * Original address: 0x00E6F1B4
 */
void SMD_$INQ_BLANK_TIMEOUT(uint32_t *timeout, status_$t *status_ret);

/*
 * ============================================================================
 * Cursor Functions
 * ============================================================================
 */

/*
 * SMD_$DISPLAY_CURSOR - Display cursor
 *
 * Makes the cursor visible at the specified position.
 *
 * Original address: 0x00E6E080
 */
void SMD_$DISPLAY_CURSOR(uint16_t *unit, smd_cursor_pos_t *pos, void *param3);

/*
 * SMD_$CLEAR_CURSOR - Clear cursor
 *
 * Hides the cursor.
 *
 * Original address: 0x00E6E0AA
 */
void SMD_$CLEAR_CURSOR(uint16_t *unit);

/*
 * SMD_$SET_CURSOR_POS - Set cursor position
 *
 * Original address: 0x00E6E766
 */
void SMD_$SET_CURSOR_POS(smd_cursor_pos_t *pos);

/*
 * SMD_$LOAD_CRSR_BITMAP - Load cursor bitmap
 *
 * Original address: 0x00E6FBC6
 */
void SMD_$LOAD_CRSR_BITMAP(void *bitmap, status_$t *status_ret);

/*
 * SMD_$READ_CRSR_BITMAP - Read cursor bitmap
 *
 * Original address: 0x00E6FD16
 */
void SMD_$READ_CRSR_BITMAP(void *bitmap, status_$t *status_ret);

/*
 * SMD_$BLINK_CURSOR_1 - Blink cursor (variant 1)
 *
 * Original address: 0x00E2722C
 */
void SMD_$BLINK_CURSOR_1(void);

/*
 * SMD_$BLINK_CURSOR_CALLBACK - Cursor blink callback
 *
 * Timer callback for cursor blinking.
 *
 * Original address: 0x00E6FF56
 */
void SMD_$BLINK_CURSOR_CALLBACK(void);

/*
 * ============================================================================
 * Keyboard Cursor Functions
 * ============================================================================
 */

/*
 * SMD_$INQ_KBD_CURSOR - Inquire keyboard cursor position
 *
 * Original address: 0x00E6E0D4
 */
void SMD_$INQ_KBD_CURSOR(smd_cursor_pos_t *pos, status_$t *status_ret);

/*
 * SMD_$MOVE_KBD_CURSOR - Move keyboard cursor
 *
 * Original address: 0x00E6E7FA
 */
void SMD_$MOVE_KBD_CURSOR(smd_cursor_pos_t *pos, status_$t *status_ret);

/*
 * SMD_$CLEAR_KBD_CURSOR - Clear keyboard cursor
 *
 * Original address: 0x00E6E828
 */
void SMD_$CLEAR_KBD_CURSOR(status_$t *status_ret);

/*
 * SMD_$INQ_KBD_TYPE - Inquire keyboard type
 *
 * Original address: 0x00E6E122
 */
void SMD_$INQ_KBD_TYPE(uint16_t *kbd_type, status_$t *status_ret);

/*
 * SMD_$SET_KBD_TYPE - Set keyboard type
 *
 * Original address: 0x00E6E1AE
 */
void SMD_$SET_KBD_TYPE(uint16_t *kbd_type, status_$t *status_ret);

/*
 * ============================================================================
 * Tracking/Mouse Functions
 * ============================================================================
 */

/*
 * SMD_$ENABLE_TRACKING - Enable mouse tracking
 *
 * Original address: 0x00E6E45C
 */
void SMD_$ENABLE_TRACKING(status_$t *status_ret);

/*
 * SMD_$DISABLE_TRACKING - Disable mouse tracking
 *
 * Original address: 0x00E6E482
 */
void SMD_$DISABLE_TRACKING(status_$t *status_ret);

/*
 * SMD_$SET_TP_REPORTING - Set trackpad reporting mode
 *
 * Original address: 0x00E6E4B0
 */
void SMD_$SET_TP_REPORTING(void *params, status_$t *status_ret);

/*
 * SMD_$SET_TP_CURSOR - Set trackpad cursor
 *
 * Original address: 0x00E6E964
 */
void SMD_$SET_TP_CURSOR(void *params, status_$t *status_ret);

/*
 * SMD_$STOP_TP_CURSOR - Stop trackpad cursor
 *
 * Original address: 0x00E6EACE
 */
void SMD_$STOP_TP_CURSOR(status_$t *status_ret);

/*
 * SMD_$CLR_AND_LOAD_TRK_RECT - Clear and load tracking rectangles
 *
 * Original address: 0x00E6E59C
 */
void SMD_$CLR_AND_LOAD_TRK_RECT(smd_track_rect_t *rect, uint16_t *id, status_$t *status_ret);

/*
 * SMD_$ADD_TRK_RECT - Add tracking rectangle
 *
 * Original address: 0x00E6E5D8
 */
void SMD_$ADD_TRK_RECT(smd_track_rect_t *rect, uint16_t *id, status_$t *status_ret);

/*
 * SMD_$DEL_TRK_RECT - Delete tracking rectangle
 *
 * Original address: 0x00E6E614
 */
void SMD_$DEL_TRK_RECT(uint16_t *id, status_$t *status_ret);

/*
 * SMD_$CLR_TRK_RECT - Clear all tracking rectangles
 *
 * Original address: 0x00E6E718
 */
void SMD_$CLR_TRK_RECT(status_$t *status_ret);

/*
 * SMD_$LOC_EVENT - Location event
 *
 * Original address: 0x00E6E9A0
 */
void SMD_$LOC_EVENT(void *params, status_$t *status_ret);

/*
 * ============================================================================
 * BLT Control Structure
 * ============================================================================
 * Used by SMD_$BLT and SMD_$BLT_U for block transfer operations.
 * Size: 26 bytes
 */
typedef struct smd_blt_ctl_t {
    uint16_t    mode;           /* 0x00: Mode register
                                 *       Bit 7: Must be 0 (reserved)
                                 *       Bit 6: Must be 0 (reserved)
                                 */
    uint32_t    ctl_reg_1;      /* 0x02: Control register 1
                                 *       Valid values: 0x2020020, 0x2020060,
                                 *                     0x6060020, 0x6060060
                                 */
    uint32_t    ctl_reg_2;      /* 0x06: Control register 2
                                 *       Valid values: same as ctl_reg_1
                                 */
    int16_t     src_y;          /* 0x0A: Source Y coordinate (>= 0) */
    int16_t     src_x;          /* 0x0C: Source X coordinate (>= 0) */
    int16_t     dst_y;          /* 0x0E: Destination Y coordinate (>= 0) */
    int16_t     dst_x;          /* 0x10: Destination X coordinate (>= 0) */
    uint16_t    src_width;      /* 0x12: Source width (<= 0x3FF) */
    uint16_t    src_height;     /* 0x14: Source height (<= 0x3FF) */
    uint16_t    dst_width;      /* 0x16: Destination width (<= 0x3FF) */
    uint16_t    dst_height;     /* 0x18: Destination height (<= 0x3FF) */
} __attribute__((packed)) smd_blt_ctl_t;

/* Valid BLT control register values */
#define SMD_BLT_CTL_VALID_1     0x02020020
#define SMD_BLT_CTL_VALID_2     0x02020060
#define SMD_BLT_CTL_VALID_3     0x06060020
#define SMD_BLT_CTL_VALID_4     0x06060060

/* BLT mode register bit masks */
#define SMD_BLT_MODE_RESERVED_MASK  0x00C0  /* Bits 6-7 must be clear */

/* Maximum coordinate value for BLT operations */
#define SMD_BLT_MAX_COORD       0x03FF

/*
 * ============================================================================
 * BLT and Drawing Functions
 * ============================================================================
 */

/*
 * SMD_$BLT - Bit block transfer
 *
 * Performs a hardware-accelerated bit block transfer.
 *
 * Parameters:
 *   params - BLT operation parameters
 *   src - Source coordinates
 *   dst - Destination coordinates
 *   status_ret - Status return
 *
 * Original address: 0x00E6EC6E
 */
void SMD_$BLT(void *params, void *src, void *dst, status_$t *status_ret);

/*
 * SMD_$BLT_U - User-mode bit block transfer
 *
 * Validates BLT parameters and performs a block transfer.
 * This is the user-callable wrapper around SMD_$BLT that performs
 * parameter validation before initiating the transfer.
 *
 * Parameters:
 *   blt_ctl - Pointer to BLT control structure
 *   status_ret - Status return
 *
 * Validation performed:
 *   - Mode register bits 6 and 7 must be clear
 *   - Control registers must contain valid values
 *   - All coordinates must be non-negative
 *   - Width/height values must be <= 0x3FF (1023)
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$display_invalid_blt_mode_register - Invalid mode bits
 *   status_$display_invalid_bIt_control_register - Invalid control register
 *   status_$display_invalid_screen_coordinates_in_bIt - Invalid coordinates
 *
 * Original address: 0x00E6FAE2
 */
void SMD_$BLT_U(smd_blt_ctl_t *blt_ctl, status_$t *status_ret);

/*
 * SMD_$SOFT_SCROLL - Software scroll
 *
 * Performs a scroll operation on a region.
 *
 * Original address: 0x00E6F326
 */
void SMD_$SOFT_SCROLL(void *region, uint16_t *dx, uint16_t *dy, status_$t *status_ret);

/*
 * SMD_$CLEAR_SCREEN - Clear screen
 *
 * Original address: 0x00E70022
 */
void SMD_$CLEAR_SCREEN(status_$t *status_ret);

/*
 * SMD_$CLEAR_WINDOW - Clear window region
 *
 * Original address: 0x00E8495C
 */
void SMD_$CLEAR_WINDOW(void *region, status_$t *status_ret);

/*
 * SMD_$INVERT_S - Invert screen region
 *
 * Original address: 0x00E6DDA6
 */
void SMD_$INVERT_S(void *region, status_$t *status_ret);

/*
 * SMD_$INVERT_DISP - Invert display
 *
 * Original address: 0x00E70376
 */
void SMD_$INVERT_DISP(status_$t *status_ret);

/*
 * SMD_$DRAW_BOX - Draw box
 *
 * Original address: 0x00E6DF2A
 */
void SMD_$DRAW_BOX(void *region, status_$t *status_ret);

/*
 * SMD_$HORIZ_LINE - Draw horizontal line
 *
 * Original address: 0x00E8496A
 */
void SMD_$HORIZ_LINE(int16_t *y, int16_t *x1, int16_t *x2, status_$t *status_ret);

/*
 * SMD_$VERT_LINE - Draw vertical line
 *
 * Original address: 0x00E84974
 */
void SMD_$VERT_LINE(int16_t *x, int16_t *y1, int16_t *y2, status_$t *status_ret);

/*
 * SMD_$SET_CLIP_WINDOW - Set clipping window
 *
 * Original address: 0x00E6FE30
 */
void SMD_$SET_CLIP_WINDOW(void *region, status_$t *status_ret);

/*
 * ============================================================================
 * Font Functions
 * ============================================================================
 */

/*
 * SMD_$LOAD_FONT - Load font
 *
 * Original address: 0x00E6DC1C
 */
void SMD_$LOAD_FONT(void *font, status_$t *status_ret);

/*
 * SMD_$UNLOAD_FONT - Unload font
 *
 * Original address: 0x00E6DD18
 */
void SMD_$UNLOAD_FONT(void *font, status_$t *status_ret);

/*
 * SMD_$COPY_FONT_TO_HDM - Copy font to hidden display memory
 *
 * Original address: 0x00E84934
 */
void SMD_$COPY_FONT_TO_HDM(void *font, smd_hdm_pos_t *pos, status_$t *status_ret);

/*
 * ============================================================================
 * Text Output Functions
 * ============================================================================
 */

/*
 * SMD_$WRITE_STRING - Write string to display
 *
 * Renders text at the specified position.
 *
 * Original address: 0x00E6FEBE
 */
void SMD_$WRITE_STRING(smd_cursor_pos_t *pos, void *font, void *buffer,
                       uint16_t *length, void *param5, status_$t *status_ret);

/*
 * SMD_$PUTC - Put character
 *
 * Original address: 0x00E1D89C
 */
void SMD_$PUTC(uint16_t c);

/*
 * ============================================================================
 * Event Functions
 * ============================================================================
 */

/*
 * SMD_$GET_IDM_EVENT - Get IDM event
 *
 * Original address: 0x00E6EE28
 */
void SMD_$GET_IDM_EVENT(void *event, status_$t *status_ret);

/*
 * SMD_$GET_UNIT_EVENT - Get unit event
 *
 * Original address: 0x00E6EEA8
 */
void SMD_$GET_UNIT_EVENT(uint16_t *unit, void *event, status_$t *status_ret);

/*
 * SMD_$DM_COND_EVENT_WAIT - Conditional event wait
 *
 * Original address: 0x00E6EFF0
 */
void SMD_$DM_COND_EVENT_WAIT(void *params, status_$t *status_ret);

/*
 * SMD_$EOF_WAIT - Wait for end-of-frame
 *
 * Original address: 0x00E6F3AE
 */
void SMD_$EOF_WAIT(status_$t *status_ret);

/*
 * SMD_$SIGNAL - Signal display event
 *
 * Original address: 0x00E6F1DA
 */
void SMD_$SIGNAL(void *params, status_$t *status_ret);

/*
 * ============================================================================
 * Miscellaneous Functions
 * ============================================================================
 */

/*
 * SMD_$LOCK_DISPLAY - Lock display for exclusive access
 *
 * Low-level display locking with interrupt disable.
 *
 * Original address: 0x00E15CCE
 */
int16_t SMD_$LOCK_DISPLAY(void *lock_data, void *param2);

/*
 * SMD_$BIT_SET - Set display bit
 *
 * Original address: 0x00E15D12
 */
void SMD_$BIT_SET(void *params);

/*
 * SMD_$LITES - Control display indicator lights
 *
 * Original address: 0x00E1D8B8
 */
void SMD_$LITES(uint16_t pattern);

/*
 * SMD_$BUSY_WAIT - Busy wait for display operation
 *
 * Original address: 0x00E1D89E
 */
void SMD_$BUSY_WAIT(void);

/*
 * SMD_$WIRE_MM - Wire memory for display
 *
 * Original address: 0x00E6F4A0
 */
void SMD_$WIRE_MM(void *params, status_$t *status_ret);

/*
 * SMD_$SEND_RESPONSE - Send response
 *
 * Original address: 0x00E6F584
 */
void SMD_$SEND_RESPONSE(void *params, status_$t *status_ret);

/*
 * SMD_$GET_EC - Get event count
 *
 * Original address: 0x00E6FD90
 */
void SMD_$GET_EC(void *ec, status_$t *status_ret);

/*
 * SMD_$SHUTDOWN - Shutdown display
 *
 * Original address: 0x00E700E6
 */
void SMD_$SHUTDOWN(void);

/*
 * SMD_$DISPLAY_LOGO - Display startup logo
 *
 * Original address: 0x00E701EE
 */
void SMD_$DISPLAY_LOGO(status_$t *status_ret);

/*
 * SMD_$MAP_DISPLAY_MEMORY - Map display memory
 *
 * Original address: 0x00E700AA
 */
void SMD_$MAP_DISPLAY_MEMORY(void *params, status_$t *status_ret);

/*
 * SMD_$UNMAP_DISPLAY_MEMORY - Unmap display memory
 *
 * Original address: 0x00E700C8
 */
void SMD_$UNMAP_DISPLAY_MEMORY(void *params, status_$t *status_ret);

#endif /* SMD_H */
