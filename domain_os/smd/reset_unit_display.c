/*
 * smd/reset_unit_display.c - smd_$reset_unit_display implementation
 *
 * Resets one display unit back to its power-on state: full-screen clip
 * window, no fonts loaded, parameter block reprogrammed, and (conditionally)
 * the bottom of display memory cleared.
 *
 * Original address: 0x00E6D736 (renamed from FUN_00e6d736)
 *
 * Callers: SMD_$ASSOC (0x00E6D8FE) and SMD_$RETURN_DISPLAY (0x00E6F79E) both
 * push `st -(SP)` for `full`; smd_$init_display_state (0x00E6F560) forwards
 * its own `full` byte (`move.b D2b,-(SP)`) instead.
 *
 * Assembly (every instruction accounted for):
 *   00e6d736    link.w A6,-0x14
 *   00e6d73a    movem.l {  A3 A2 D4 D3 D2},-(SP)
 *   00e6d73e    move.w (0x8,A6),D0w         ; D0 = unit
 *   00e6d742    move.b (0xa,A6),D1b         ; D1 = `full` boolean
 *   00e6d746    move.w D0w,D2w
 *   00e6d748    movea.l #0xe2e3fc,A0
 *   00e6d74e    muls.w #0x10c,D2            ; signed unit * 0x10C
 *   00e6d752    lea (0x0,A0,D2*0x1),A0      ; A0 = biased unit record
 *   00e6d756    movea.l (-0xf4,A0),A1       ; A1 = rec->hw
 *   00e6d75a    lea (0x4e,A1),A2            ; A2 = &hw->min_x
 *   00e6d75e    move.l (A2)+,(0x56,A1)      ; clip_x1/clip_x2 = min_x/max_x
 *   00e6d762    move.l (A2)+,(0x5a,A1)      ; clip_y1/clip_y2 = min_y/max_y
 *   00e6d766    moveq #0x7,D0               ; 8 iterations
 *   00e6d768    moveq #0x8,D2
 *   00e6d76a    movea.l A0,A0
 *   00e6d76c    movea.l (A0),A2             ; A2 = rec->font_table (rec+0xF4)
 *   00e6d76e    clr.l (-0x8,A2,D2*0x1)      ; font_table[i].font_ptr = NULL
 *   00e6d772    addq.l #0x8,D2
 *   00e6d774    dbf D0w,0x00e6d76c
 *   00e6d778    movea.l (0x4,A0),A2         ; A2 = rec->hdm_list (rec+0xF8)
 *   00e6d77c    move.w #0x1,(A2)
 *   00e6d780    move.w (A1),D3w             ; D3 = hw->display_type
 *   00e6d782    moveq #0x44,D4              ; bits 2 and 6
 *   00e6d784    btst.l D3,D4
 *   00e6d786    bne.b 0x00e6d792
 *   00e6d788    move.l #0x3103ce,(0x2,A2)
 *   00e6d790    bra.b 0x00e6d79a
 *   00e6d792    move.l #0x357,(0x2,A2)
 *   00e6d79a    tst.w (-0xee,A0)            ; rec->borrowed_asid
 *   00e6d79e    bne.b 0x00e6d7a6
 *   00e6d7a0    tst.w (-0xf0,A0)            ; rec->owner_asid
 *   00e6d7a4    bne.b 0x00e6d7d8            ; owned and not borrowed -> done
 *   00e6d7a6    tst.b D1b
 *   00e6d7a8    bpl.b 0x00e6d7d8            ; `full` false -> done
 *   00e6d7aa    moveq #0x3,D1               ; 4 iterations
 *   00e6d7ac    clr.w D0w
 *   00e6d7ae    movea.l A0,A0
 *   00e6d7b0    movea.l (0x14,A0),A2        ; A2 = rec->display_base (rec+0x108)
 *   00e6d7b4    lea (0x0,A2,D0w*0x1),A3
 *   00e6d7b8    adda.l #0x1fff0,A3
 *   00e6d7be    clr.w (A3)                  ; display_base[0x1FFF0 + 2i] = 0
 *   00e6d7c0    movea.l (0x14,A0),A2
 *   00e6d7c4    lea (0x0,A2,D0w*0x1),A2
 *   00e6d7c8    adda.l #0x1fff8,A2
 *   00e6d7ce    move.w #-0x1,(A2)           ; display_base[0x1FFF8 + 2i] = -1
 *   00e6d7d2    addq.w #0x2,D0w
 *   00e6d7d4    dbf D1w,0x00e6d7b0
 *   00e6d7d8    movem.l (-0x28,A6),{  D2 D3 D4 A2 A3}
 *   00e6d7de    unlk A6
 *   00e6d7e0    rts
 */

#include "smd/smd_internal.h"

/*
 * Display types whose hidden-display-memory free block starts at zero.
 * 0x00E6D782-0x00E6D784: moveq #0x44,D4 / btst.l D3,D4, i.e. bit
 * (display_type mod 32) of 0x44 - bits 2 and 6, so types 2 and 6.
 *
 * The two longwords the original stores at list+0x02 cover the single free
 * block's {offset, size} pair (see smd_hdm_list_t):
 *   0x00000357 -> offset 0x0000, size 0x0357  (types 2 and 6)
 *   0x003103CE -> offset 0x0031, size 0x03CE  (every other type)
 * The second pair is exactly the [0x31, 0x3FF) range SMD_$FREE_HDM validates
 * for display type 1 (0x00E6DA98-0x00E6DAAA).
 */
#define SMD_HDM_SEED_TYPE_MASK 0x44u
#define SMD_HDM_SEED_A_OFFSET 0x0000u  /* types 2 and 6 */
#define SMD_HDM_SEED_A_SIZE 0x0357u
#define SMD_HDM_SEED_B_OFFSET 0x0031u  /* every other type */
#define SMD_HDM_SEED_B_SIZE 0x03CEu

/*
 * smd_$reset_unit_display - Reset one unit's display state to defaults.
 *
 * Parameters:
 *   unit - display unit number (1-based)
 *   full - Domain boolean; when true the display-memory clear is performed
 */
void smd_$reset_unit_display(int16_t unit, boolean full)
{
    smd_display_unit_t *rec;
    smd_display_hw_t *hw;
    int16_t i;

    /* 0x00e6d748-0x00e6d756 */
    rec = smd_$unit_rec(unit);
    hw = rec->hw;

    /*
     * 0x00e6d75a-0x00e6d762: reset the clip window to the full screen.  The
     * original copies min_x/max_x and min_y/max_y as two longwords through a
     * post-incrementing pointer, so the pairs must stay adjacent.
     */
    hw->clip_x1 = hw->min_x;
    hw->clip_x2 = hw->max_x;
    hw->clip_y1 = hw->min_y;
    hw->clip_y2 = hw->max_y;

    /*
     * 0x00e6d766-0x00e6d774: drop every loaded font.  D2 runs 8, 16, ... 64
     * and the store is (-8,A2,D2), i.e. entries 0..7; the font table pointer
     * is reloaded from the record on each iteration.
     */
    for (i = 0; i < SMD_MAX_FONTS_PER_UNIT; i++) {
        rec->font_table[i].font_ptr = NULL;
    }

    /* 0x00e6d778-0x00e6d79a: reseed the HDM free list with one block. */
    {
        smd_hdm_list_t *list = rec->hdm_list;

        list->count = 1;
        if (((SMD_HDM_SEED_TYPE_MASK >> (hw->display_type & 0x1F)) & 1u) != 0) {
            list->blocks[0].offset = SMD_HDM_SEED_A_OFFSET;
            list->blocks[0].size = SMD_HDM_SEED_A_SIZE;
        } else {
            list->blocks[0].offset = SMD_HDM_SEED_B_OFFSET;
            list->blocks[0].size = SMD_HDM_SEED_B_SIZE;
        }
    }

    /*
     * 0x00e6d79a-0x00e6d7a8: only clear display memory when the unit is
     * borrowed, or is not owned at all, and the caller asked for a full
     * reset.  `full` is a Domain boolean, tested with tst.b/bpl.
     */
    if ((rec->borrowed_asid != 0 || rec->owner_asid == 0) && full < 0) {
        /*
         * 0x00e6d7aa-0x00e6d7d4: clear four words at display_base+0x1FFF0 and
         * set four words at display_base+0x1FFF8 to 0xFFFF.  The display base
         * is reloaded from the record on every access, exactly as here.
         */
        int16_t off;

        for (i = 0, off = 0; i < 4; i++, off = (int16_t)(off + 2)) {
            *(SMD_HW_REG_PTR)(uintptr_t)(rec->display_base + 0x1FFF0u + (uint32_t)(uint16_t)off) = 0;
            *(SMD_HW_REG_PTR)(uintptr_t)(rec->display_base + 0x1FFF8u + (uint32_t)(uint16_t)off) = 0xFFFF;
        }
    }
}
