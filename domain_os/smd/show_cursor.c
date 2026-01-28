/*
 * smd/show_cursor.c - SHOW_CURSOR implementation
 *
 * Internal cursor show/update function. Manages cursor visibility,
 * position updates, tracking rectangle checks, and display locking.
 *
 * Original address: 0x00E6E1CC
 * Size: 652 bytes
 */

#include "smd/smd_internal.h"
/*
 * Helper to read/write uint32_t from struct fields without strict-aliasing
 * violations. The original m68k code accesses pairs of uint16_t fields as
 * a single 32-bit value (e.g., field_32 + field_34 = cursor position).
 */
static inline uint32_t read_u32(const void *p) {
    uint32_t v;
    __builtin_memcpy(&v, p, sizeof(v));
    return v;
}
static inline void write_u32(void *p, uint32_t v) {
    __builtin_memcpy(p, &v, sizeof(v));
}

/* Exclusion lock for tracking rectangle access.
 * Original address: 0x00E2E520 */
extern ml_$exclusion_t ml_$exclusion_t_00e2e520;

/* Fixed lock data for ACQ_DISPLAY / LOCK_DISPLAY calls.
 * Original address: 0x00E6DFF8 (contains 0x0001 as big-endian int16_t) */
static const int16_t show_cursor_acq_lock_data = 1;

/* Cursor draw/undraw flag for smd_$draw_cursor_internal.
 * Original address: 0x00E6E458 (contains 0xFF = undraw/erase) */
static const int8_t show_cursor_undraw_flag = (int8_t)0xFF;

/* Cursor draw flag for smd_$draw_cursor_internal.
 * Original address: 0x00E6E45A (contains 0x00 = draw) */
static const int8_t show_cursor_draw_flag = 0;

/*
 * SHOW_CURSOR - Update cursor display state
 *
 * This function manages cursor visibility, position, and display. It:
 *   1. Validates the current display unit
 *   2. Resolves default cursor position/type from display unit hw state
 *   3. Computes cursor bounding box using SMD_CURSOR_PTABLE, clips to display
 *   4. Checks tracking rectangles (under exclusion lock) for cursor overlap
 *   5. Acquires display lock (blocking or try-lock based on lock_data2)
 *   6. Undraws old cursor via smd_$draw_cursor_internal
 *   7. Draws new cursor at new position via same function
 *   8. Updates global cached state (position, visibility, blink timer)
 *   9. Releases display
 *
 * Parameters:
 *   pos        - Pointer to cursor position (as uint32_t: x in high 16, y in low 16)
 *   lock_data1 - Pointer to cursor number (int16_t); -1 means use default
 *   lock_data2 - Pointer to blocking flag (int8_t); negative = use ACQ_DISPLAY (blocking),
 *                non-negative = use LOCK_DISPLAY (try-lock, may fail)
 *
 * Original address: 0x00E6E1CC
 *
 * Assembly:
 *   00e6e1cc    link.w A6,-0x40
 *   00e6e1d0    movem.l {  A5 A4 A3 A2 D7 D6 D5 D4 D3 D2},-(SP)
 *   00e6e1d4    lea (0xe82b8c).l,A5           ; A5 = SMD_GLOBALS
 *   00e6e1da    movea.l (0x8,A6),A0           ; A0 = pos
 *   00e6e1de    move.l (A0),(-0x8,A6)         ; local_pos = *pos
 *   00e6e1e2    movea.l (0xc,A6),A1           ; A1 = lock_data1
 *   00e6e1e6    movea.l (0x10,A6),A2          ; A2 = lock_data2
 *   00e6e1ea    move.w (A1),D6w              ; cursor_num = *lock_data1
 *   00e6e1ee    move.b (A2),D5b              ; blocking_flag = *lock_data2
 *   00e6e1f0    move.w (0x1d98,A5),-(SP)     ; push default_unit
 *   00e6e1f4    bsr.w smd_$validate_unit
 *   00e6e1f8    addq.w #0x4,SP
 *   00e6e1fa    tst.b D0b
 *   00e6e1fc    bpl.w exit                    ; if not valid, return
 *   ; ... (see full assembly in validate_unit.c for reference)
 *   00e6e44e    movem.l (-0x68,A6),registers
 *   00e6e454    unlk A6
 *   00e6e456    rts
 */
int8_t SHOW_CURSOR(uint32_t *pos, int16_t *lock_data1, int8_t *lock_data2)
{
    uint32_t local_pos;
    int16_t cursor_num;
    int8_t blocking_flag;
    smd_display_hw_t *current_hw;
    smd_display_hw_t *prev_hw;
    uint8_t *current_unit_ptr; /* A1 in assembly: base + default_unit * 0x10C */
    uint8_t *prev_unit_ptr;   /* A3 in assembly: base + previous_unit * 0x10C */
    uint16_t default_unit;
    uint16_t prev_unit;
    int16_t *cursor_pattern;
    int16_t cursor_height;
    int16_t top_y;
    int16_t bottom_x;
    int16_t bottom_y;
    int16_t left_x;
    uint8_t cursor_visible;
    int8_t draw_result;
    int16_t i;

    local_pos = *pos;
    cursor_num = *lock_data1;
    blocking_flag = *lock_data2;

    if (smd_$validate_unit(SMD_GLOBALS.default_unit) < 0) {
        default_unit = SMD_GLOBALS.default_unit;
        prev_unit = SMD_GLOBALS.previous_unit;

        /* If previous_unit is unset (-1), initialize to current unit */
        if (SMD_GLOBALS.previous_unit == (uint16_t)0xFFFF) {
            SMD_GLOBALS.previous_unit = default_unit;
            prev_unit = default_unit;
        }

        /*
         * Get pointers into the display unit array.
         * Assembly: movea.l #0xe2e3fc,A0; muls.w #0x10c,D0; lea (0x0,A0,D0*0x1),A1
         * These point to: SMD_DISPLAY_UNITS_BASE + unit * 0x10C
         *
         * The hw pointer is at offset -0xF4 from this position, which corresponds
         * to the hw field in the display_unit_t structure of the preceding slot.
         */
        current_unit_ptr = (uint8_t *)SMD_DISPLAY_UNITS +
                           (int32_t)default_unit * SMD_DISPLAY_UNIT_SIZE;
        current_hw = *(smd_display_hw_t **)(current_unit_ptr - 0xF4);

        prev_unit_ptr = (uint8_t *)SMD_DISPLAY_UNITS +
                        (int32_t)prev_unit * SMD_DISPLAY_UNIT_SIZE;
        prev_hw = *(smd_display_hw_t **)(prev_unit_ptr - 0xF4);

        /*
         * If position matches the default cursor sentinel (at offset 0x1D94
         * in SMD_GLOBALS), use the cursor position from the previous unit's
         * hw state instead.
         *
         * Assembly:
         *   00e6e246    move.l (-0x8,A6),D0
         *   00e6e24a    cmp.l (0x1d94,A5),D0
         *   00e6e24e    bne.b 0x00e6e256
         *   00e6e250    move.l (0x32,A2),(-0x8,A6)
         */
        if (local_pos == SMD_GLOBALS.cursor_pos_sentinel) {
            /* Use cursor position from previous unit's hw state.
             * hw->field_32 and hw->field_34 form a packed cursor position. */
            local_pos = read_u32(&prev_hw->field_32);
        }

        /* If cursor number is -1, use default from previous unit's hw */
        if (cursor_num == -1) {
            cursor_num = prev_hw->cursor_number;
        }

        /* Assume cursor should be visible */
        cursor_visible = 0xFF;

        /*
         * Look up cursor pattern from the cursor pattern table.
         * SMD_CURSOR_PTABLE[cursor_num] points to cursor bitmap metadata:
         *   [0] = cursor height
         *   [1] = x hotspot offset (subtracted from bottom_x for left_x)
         *   [2] = y hotspot offset (subtracted from pos.y for top_y)
         *   [3] = x extent (added to pos.x for bottom_x)
         */
        cursor_pattern = SMD_CURSOR_PTABLE[cursor_num];
        cursor_height = cursor_pattern[0];

        /*
         * Compute cursor bounding box from position and cursor pattern.
         * Position is packed as uint32_t: x in high 16 bits, y in low 16 bits
         * (big-endian m68k byte order within the smd_cursor_pos_t struct).
         *
         * Assembly:
         *   00e6e27a    move.w (-0x6,A6),D4w   ; D4 = pos.y (low 16)
         *   00e6e282    sub.w (0x4,A0),D4w     ; D4 = pos.y - hot_y
         *   00e6e28a    move.w (-0x8,A6),D3w   ; D3 = pos.x (high 16)
         *   00e6e292    add.w (0x6,A0),D3w     ; D3 = pos.x + x_extent
         */
        top_y = (int16_t)(local_pos & 0xFFFF) - cursor_pattern[2];
        if (top_y < 0) {
            top_y = 0;
        }

        bottom_x = (int16_t)(local_pos >> 16) + cursor_pattern[3];
        if (bottom_x > current_hw->width) {
            bottom_x = current_hw->width;
        }

        /* bottom_y = top_y + cursor_height */
        bottom_y = top_y + cursor_height;
        if (bottom_y > current_hw->height) {
            bottom_y = current_hw->height + 1;
            top_y = bottom_y - cursor_height;
        }

        /* left_x = bottom_x - cursor_pattern[1] (x hotspot offset) */
        left_x = bottom_x - cursor_pattern[1];
        if (left_x < 0) {
            left_x = -1;
            bottom_x = cursor_pattern[1] - 1;
        }

        /*
         * Check tracking rectangles under exclusion lock.
         * If the cursor bounding box overlaps any tracking rectangle,
         * mark cursor as not visible (cursor_visible = 0).
         *
         * Tracking rectangles are stored in SMD_GLOBALS starting at
         * offset 0xE8 (tracking_rects array), with the count at the
         * field currently named tracking_rect_count.
         *
         * Assembly:
         *   00e6e2cc    pea (0xe2e520).l             ; exclusion lock
         *   00e6e2d2    jsr ML_$EXCLUSION_START
         *   00e6e2da    move.w (0xe6,A5),D1w         ; tracking rect count
         *   00e6e2de    subq.w #0x1,D1w
         *   00e6e2e0    bmi.b skip_check
         *   00e6e2e2    movea.l A5,A1
         *   00e6e2e6    addq.l #0x8,A1               ; A1 = globals + 8
         */
        ML_$EXCLUSION_START(&ml_$exclusion_t_00e2e520);

        i = SMD_GLOBALS.tracking_rect_count - 1;
        if (i >= 0) {
            smd_track_rect_t *rect = &SMD_GLOBALS.tracking_rects[0];
            int16_t count = i;
            do {
                /*
                 * Check if cursor bounding box overlaps tracking rectangle.
                 * Original uses fields at offsets 0xe0..0xe6 from (A5+8),
                 * which map to tracking_rects[i].x1, y1, x2, y2.
                 *
                 * Assembly:
                 *   00e6e2ee    cmp.w (0xe6,A0),D1w   ; left_x < rect.y2
                 *   00e6e2f4    cmp.w (0xe4,A0),D3w   ; bottom_x >= rect.x2
                 *   00e6e2fa    cmp.w (0xe0,A0),D2w   ; bottom_y > rect.x1
                 *   00e6e300    cmp.w (0xe2,A0),D4w   ; top_y <= rect.y1
                 */
                if (left_x < rect->y2 &&
                    bottom_x >= rect->x2 &&
                    bottom_y > rect->x1 &&
                    top_y <= rect->y1) {
                    cursor_visible = 0;
                    break;
                }
                rect++;
                count--;
            } while (count != (int16_t)-1);
        }

        ML_$EXCLUSION_STOP(&ml_$exclusion_t_00e2e520);

        /* Store cursor state in previous unit's hw */
        prev_hw->cursor_number = cursor_num;
        write_u32(&prev_hw->field_32, local_pos);

        /*
         * Check if cursor state actually changed. If nothing changed,
         * skip the display update.
         *
         * Assembly:
         *   00e6e32a    move.b (-0x1c,A6),D1b        ; cursor_visible
         *   00e6e32e    cmp.b (0x38,A4),D1b          ; vs current_hw->cursor_visible
         *   00e6e334    move.w (0x1d9c,A5),D0w       ; previous_unit
         *   00e6e338    cmp.w (0x1d98,A5),D0w        ; vs default_unit
         *   00e6e33e    move.l (-0x8,A6),D0           ; local_pos
         *   00e6e342    cmp.l (0xd0,A5),D0           ; vs default_cursor_pos
         *   00e6e348    cmp.w (0xd4,A5),D6w          ; cursor_num vs cursor_button_state
         */
        if (cursor_visible != current_hw->cursor_visible ||
            SMD_GLOBALS.previous_unit != SMD_GLOBALS.default_unit ||
            local_pos != read_u32(&SMD_GLOBALS.default_cursor_pos) ||
            cursor_num != SMD_GLOBALS.cursor_button_state) {

            /*
             * Store previous unit in ASID-to-unit table.
             * Assembly:
             *   00e6e350    move.w (0x00e2060a).l,D0w     ; PROC1_$AS_ID
             *   00e6e356    add.w D0w,D0w                 ; * 2
             *   00e6e358    move.w (0x1d9c,A5),(0x48,A5,D0w*0x1)
             */
            SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID] = SMD_GLOBALS.previous_unit;

            /*
             * Acquire display lock - blocking or try-lock.
             * Assembly:
             *   00e6e35e    tst.b D5b                     ; blocking_flag
             *   00e6e360    bpl.b try_lock
             *   00e6e362    pea (-0x36c,PC)               ; &lock_data (0xe6dff8)
             *   00e6e366    bsr.w SMD_$ACQ_DISPLAY
             *   00e6e36e    pea (-0x378,PC)               ; &lock_data (0xe6dff8)
             *   00e6e372    pea (A2)                      ; prev_hw
             *   00e6e374    jsr SMD_$LOCK_DISPLAY
             */
            if (blocking_flag < 0) {
                SMD_$ACQ_DISPLAY((int16_t *)&show_cursor_acq_lock_data);
            } else {
                int8_t lock_result = (int8_t)SMD_$LOCK_DISPLAY(
                    prev_hw, (int16_t *)&show_cursor_acq_lock_data);
                if (lock_result >= 0) {
                    return 0; /* Could not acquire lock */
                }
            }

            draw_result = (int8_t)0xFF;

            /*
             * If previous cursor was visible, undraw it.
             * Assembly:
             *   00e6e384    tst.b (0x38,A2)              ; prev_hw->cursor_visible
             *   00e6e388    bpl.b skip_undraw
             *   00e6e38a    movea.l (-0x34,A6),A0
             *   00e6e38e    clr.b (A0)                   ; SMD_BLINK_STATE.smd_time_com = 0
             *   00e6e390    tst.b (0x2,A0)               ; SMD_BLINK_STATE.blink_flag
             *   00e6e394    bpl.b skip_undraw_call
             */
            if ((int8_t)prev_hw->cursor_visible < 0) {
                SMD_BLINK_STATE.smd_time_com = 0;
                draw_result = (int8_t)0xFF;
                if ((int8_t)SMD_BLINK_STATE.blink_flag < 0) {
                    /*
                     * Undraw old cursor at cached position.
                     * Assembly:
                     *   00e6e396    move.l (0x8,A3),-(SP)      ; prev_aux ec_1
                     *   00e6e39a    move.l (0x14,A3),-(SP)     ; prev_aux ec_2
                     *   00e6e39e    pea (0xb8,PC)              ; cursor flag (0xe6e458)
                     *   00e6e3a2    pea (A2)                   ; prev_hw (display_comm)
                     *   00e6e3a4    pea (0x4e,A2)              ; prev_hw + 0x4E (hw_offset)
                     *   00e6e3a8    pea (0xd0,A5)              ; &cached_cursor_pos
                     *   00e6e3ac    pea (0xd4,A5)              ; &cached_cursor_num
                     */
                    draw_result = smd_$draw_cursor_internal(
                        (int16_t *)&SMD_GLOBALS.cursor_button_state,
                        (uint32_t *)&SMD_GLOBALS.default_cursor_pos, /* cursor_pos */
                        (void *)((uint8_t *)prev_hw + 0x4E),
                        (void *)prev_hw,
                        (int8_t *)&show_cursor_undraw_flag,
                        (uint32_t *)(prev_unit_ptr + 0x14),
                        (uint32_t *)(prev_unit_ptr + 0x08));
                }
                if (draw_result < 0) {
                    prev_hw->cursor_visible = 0;
                }
            }

            /*
             * Update cursor pending flag.
             * Assembly:
             *   00e6e3c2    move.b (-0x1c,A6),D1b        ; cursor_visible
             *   00e6e3c6    move.l (-0x8,A6),D7          ; local_pos
             *   00e6e3ca    not.b D1b                    ; ~cursor_visible
             *   00e6e3cc    cmp.l (0xd0,A5),D7           ; local_pos == cached_pos?
             *   00e6e3d0    seq D4b                      ; D4 = (equal) ? 0xFF : 0
             *   00e6e3d2    and.b D4b,D1b                ; D1 = ~cursor_visible & same_pos
             *   00e6e3d4    or.b D1b,(0x1744,A5)         ; cursor_pending |= D1
             */
            {
                uint8_t same_pos = (local_pos == read_u32(&SMD_GLOBALS.default_cursor_pos)) ? 0xFF : 0;
                SMD_GLOBALS.cursor_pending_flag |= (~cursor_visible) & same_pos;
            }

            /*
             * If both draw_result and cursor_visible are negative (true),
             * draw the new cursor.
             * Assembly:
             *   00e6e3d8    move.b (-0x1c,A6),D4b       ; cursor_visible
             *   00e6e3dc    and.b D0b,D4b               ; D4 = cursor_visible & draw_result
             *   00e6e3de    bpl.b skip_draw              ; if not negative, skip
             */
            if ((int8_t)(draw_result & cursor_visible) < 0) {
                int16_t draw_cursor_num;
                uint32_t draw_pos;
                int16_t draw_cursor_num_local;
                int8_t draw_ok;

                /*
                 * Draw new cursor at new position.
                 * Assembly:
                 *   00e6e3e0    move.w (0x36,A4),D2w        ; cursor_num from current_hw
                 *   00e6e3e4    move.l (0x32,A4),(-0x8,A6)  ; cursor_pos from current_hw
                 *   00e6e3ea    move.w D2w,(-0x18,A6)       ; local cursor_num
                 *   00e6e3ee    movea.l (-0x24,A6),A0       ; current_aux
                 *   00e6e3f2    move.l (0x8,A0),-(SP)       ; current_aux ec_1
                 *   00e6e3f6    move.l (0x14,A0),-(SP)      ; current_aux ec_2
                 *   00e6e3fa    pea (0x5e,PC)               ; cursor flag (0xe6e45a)
                 *   00e6e3fe    pea (A4)                    ; current_hw
                 *   00e6e400    pea (0x4e,A4)               ; current_hw + 0x4E
                 *   00e6e404    pea (-0x8,A6)               ; &draw_pos
                 *   00e6e408    pea (-0x18,A6)              ; &draw_cursor_num_local
                 */
                draw_cursor_num = current_hw->cursor_number;
                draw_pos = read_u32(&current_hw->field_32);
                draw_cursor_num_local = draw_cursor_num;

                draw_ok = smd_$draw_cursor_internal(
                    &draw_cursor_num_local,
                    &draw_pos,
                    (void *)((uint8_t *)current_hw + 0x4E),
                    (void *)current_hw,
                    (int8_t *)&show_cursor_draw_flag,
                    (uint32_t *)(current_unit_ptr + 0x14),
                    (uint32_t *)(current_unit_ptr + 0x08));

                if (draw_ok < 0) {
                    /*
                     * Cursor drawn successfully.
                     * Assembly:
                     *   00e6e41a    clr.b (0x1744,A5)          ; cursor_pending = 0
                     *   00e6e41e    move.l (-0x8,A6),(0xd0,A5) ; cached_pos = draw_pos
                     *   00e6e424    move.w D2w,(0xd4,A5)       ; cached_cursor_num = draw_cursor_num
                     *   00e6e428    st (0x38,A4)               ; current_hw->cursor_visible = 0xFF
                     *   00e6e42c    seq D4b                    ; D4 = (cursor_num==0) ? 0xFF : 0
                     *   00e6e42e    movea.l (-0x34,A6),A0      ; SMD_BLINK_STATE ptr
                     *   00e6e432    move.b D4b,(A0)            ; smd_time_com = (cursor_num==0)?0xFF:0
                     *   00e6e434    st (0x2,A0)                ; blink_flag = 0xFF
                     *   00e6e438    move.w #0x7,(0x4,A0)       ; blink_counter = 7
                     */
                    SMD_GLOBALS.cursor_pending_flag = 0;
                    write_u32(&SMD_GLOBALS.default_cursor_pos, draw_pos);
                    SMD_GLOBALS.cursor_button_state = draw_cursor_num;
                    current_hw->cursor_visible = (uint8_t)0xFF;
                    SMD_BLINK_STATE.smd_time_com = (draw_cursor_num == 0) ? (uint8_t)0xFF : 0;
                    SMD_BLINK_STATE.blink_flag = (uint8_t)0xFF;
                    SMD_BLINK_STATE.blink_counter = 7;
                } else {
                    /*
                     * Draw failed - mark cursor as pending.
                     * Assembly:
                     *   00e6e440    st (0x1744,A5)             ; cursor_pending = 0xFF
                     */
                    SMD_GLOBALS.cursor_pending_flag = 0xFF;
                }
            }

            /* Update previous_unit to current unit */
            SMD_GLOBALS.previous_unit = SMD_GLOBALS.default_unit;

            /* Release display lock */
            SMD_$REL_DISPLAY();
        }
    }

    return 0;
}
