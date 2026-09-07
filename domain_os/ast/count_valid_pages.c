/*
 * ast_$count_valid_pages - allocate and zero the pages for a copy-on-write
 *                          run, or refuse if the object is read-only
 *
 * A nested Pascal procedure of AST_$TOUCH (0x00E030C0), called from two
 * places in it (0x00E0322C and 0x00E03318) with the static link implicit
 * in the dynamic link (`movea.l (A6),A2` at 0x00E0306C).  Through that
 * link it reads three of AST_$TOUCH's own arguments:
 *
 *   (0x14,A2) = AST_$TOUCH's `ppn_array`
 *   (0x18,A2) = AST_$TOUCH's `status`
 *   (0x1D,A2) = the LOW byte of AST_$TOUCH's `flags` word at (0x1C,A2),
 *               tested with `btst.b #0x1` = flags & 0x0002
 *
 * All three are explicit parameters in this flattening.
 *
 * Its own two stack arguments are:
 *   (0x08,A6) = a pointer INTO the segment map (`pea (A2)` at both call
 *               sites, where A2 walks the segmap) -- NOT an ASTE
 *   (0x0C,A6) = the page count
 *
 * Returns the number of pages allocated, or the original count on the
 * read-only path.
 *
 * Original address: 0x00E0305C (100 bytes)
 *
 * Full instruction trace:
 *   00e0305c  link.w A6,-0x10
 *   00e03060  movem.l {A3 A2 D3 D2},-(SP)
 *   00e03064  movea.l (0x8,A6),A3      ; segmap_entry
 *   00e03068  move.w (0xc,A6),D2w      ; count
 *   00e0306c  movea.l (A6),A2          ; AST_$TOUCH's frame
 *   00e0306e  btst.b #0x1,(0x1d,A2)    ; touch_flags & 0x0002
 *   00e03074  beq.b 0x00e0308c
 *   00e03076  subq.l #0x2,SP           ; Pascal result slot (discarded)
 *   00e03078  move.w D2w,-(SP)         ; count           (arg 2)
 *   00e0307a  pea (A3)                 ; segmap_entry    (arg 1)
 *   00e0307c  bsr.w 0x00e0283c         ; ast_$clear_transition_bits
 *   00e03080  movea.l (0x18,A2),A0     ; AST_$TOUCH's status
 *   00e03084  move.l #0x50008,(A0)
 *   00e0308a  bra.b 0x00e030b4         ; the 8 pushed bytes are never popped
 *   00e0308c  movea.l (0x14,A2),A3     ; A3 = ppn_array (segmap_entry is dead)
 *   00e03090  move.l (0x14,A2),-(SP)   ; ppn_array       (arg 3)
 *   00e03094  move.w #0x1,-(SP)        ; min_count = 1   (arg 2)
 *   00e03098  move.w D2w,-(SP)         ; count           (arg 1)
 *   00e0309a  bsr.w 0x00e00d46         ; ast_$allocate_pages -> D0
 *   00e0309e  addq.w #0x8,SP
 *   00e030a0  move.w D0w,D2w
 *   00e030a2  beq.b 0x00e030b4
 *   00e030a4  move.w D2w,D3w / subq.w #0x1,D3w
 *   00e030a8  move.l (A3)+,-(SP)       ; ppn_array[0], [1], ... ASCENDING
 *   00e030aa  bsr.w 0x00e00eb0         ; ZERO_PAGE
 *   00e030ae  addq.w #0x4,SP
 *   00e030b0  dbf D3w,0x00e030a8
 *   00e030b4  move.w D2w,D0w
 *   00e030b6  movem.l (-0x20,A6),{D2 D3 A2 A3} / unlk A6 / rts
 */

#include "ast/ast_internal.h"

/*
 * 0x00E03084: move.l #0x50008,(A0) -- "OS / AST manager: object is
 * read-only".
 */
#define AST_$OBJECT_READ_ONLY 0x00050008

int16_t ast_$count_valid_pages(uint32_t *segmap_entry, int16_t count,
                               uint16_t touch_flags,
                               uint32_t *ppn_array,
                               status_$t *status)
{
    int16_t i;
    int16_t allocated;

    /* 0x00E0306E: bit 1 of AST_$TOUCH's flags word */
    if ((touch_flags & 0x0002) != 0) {
        /* 0x00E0307C */
        ast_$clear_transition_bits(segmap_entry, count);

        /* 0x00E03084 */
        *status = AST_$OBJECT_READ_ONLY;

        /* 0x00E030B4: D2 still holds the original count */
        return count;
    }

    /* 0x00E0309A */
    allocated = ast_$allocate_pages(count, 1, ppn_array);

    /* 0x00E030A2 */
    if (allocated != 0) {
        /* 0x00E030A8: A3 post-increments, so the order is ascending */
        for (i = 0; i < allocated; i++) {
            ZERO_PAGE(ppn_array[i]);
        }
    }

    return allocated;
}
