/*
 * smd/assoc.c - SMD_$ASSOC implementation
 *
 * Associates a display unit with a process address space.
 *
 * Original address: 0x00E6D882
 *
 * This function establishes the mapping between a process (identified by its
 * ASID) and a display unit. After association, SMD operations from that
 * process will use the associated display.
 *
 * Assembly:
 *   00e6d882    link.w A6,-0xc
 *   00e6d886    movem.l {  A5 A4 A3 A2 D2},-(SP)
 *   00e6d88a    lea (0xe82b8c).l,A5               ; A5 = globals
 *   00e6d890    movea.l (0x8,A6),A2               ; A2 = unit param
 *   00e6d894    movea.l (0x10,A6),A3              ; A3 = status_ret
 *   00e6d898    pea (A2)
 *   00e6d89a    bsr.w 0x00e6de1c                  ; SMD_$INQ_DISP_TYPE
 *   00e6d89e    addq.w #0x4,SP
 *   00e6d8a0    tst.w D0w
 *   00e6d8a2    seq D2b                           ; D2 = (type == 0)
 *   00e6d8a4    tst.b D2b
 *   00e6d8a6    bpl.b 0x00e6d8b0                  ; if valid, continue
 *   00e6d8a8    move.l #0x130001,(A3)             ; status = invalid_unit
 *   00e6d8ae    bra.b 0x00e6d920
 *   00e6d8b0    clr.l (A3)                        ; *status_ret = 0
 *   00e6d8b2    move.w (A2),(0x1d98,A5)           ; default_unit = *unit
 *   00e6d8b6    pea (A3)                          ; status_ret
 *   00e6d8b8    pea (0x70,PC)                     ; &word 1 at 0x00E6D92A
 *   00e6d8bc    pea (0x6e,PC)                     ; &word 0 at 0x00E6D92C
 *   00e6d8c0    jsr 0x00e1ab62.l                  ; TERM_$SET_REAL_LINE_DISCIPLINE
 *   00e6d8c6    lea (0xc,SP),SP
 *   00e6d8ca    pea (A2)
 *   00e6d8cc    jsr 0x00e699dc.l                  ; TPAD_$SET_UNIT
 *   00e6d8d2    addq.w #0x4,SP
 *   00e6d8d4    move.w (0x00e2060a).l,D2w         ; D2 = PROC1_$AS_ID
 *   00e6d8da    bne.b 0x00e6d8e2
 *   00e6d8dc    movea.l (0xc,A6),A0               ; A0 = asid param
 *   00e6d8e0    move.w (A0),D2w                   ; D2 = *asid
 *   00e6d8e2    move.w (A2),D0w
 *   00e6d8e4    movea.l #0xe2e3fc,A0
 *   00e6d8ea    mulu.w #0x10c,D0
 *   00e6d8ee    lea (0x0,A0,D0w*0x1),A3           ; A3 = biased unit record
 *   00e6d8f2    movea.l (-0xf4,A3),A4             ; A4 = rec->hw
 *   00e6d8f6    clr.w (-0xf0,A3)                  ; rec->owner_asid = 0
 *   00e6d8fa    st -(SP)                          ; full = true
 *   00e6d8fc    move.w (A2),-(SP)                 ; unit
 *   00e6d8fe    bsr.w 0x00e6d736                  ; smd_$reset_unit_display
 *   00e6d902    addq.w #0x4,SP
 *   00e6d904    move.w D2w,(-0xf0,A3)             ; rec->owner_asid = asid
 *   00e6d908    clr.w (-0xee,A3)                  ; rec->borrowed_asid = 0
 *   00e6d90c    clr.b (0x3c,A4)                   ; hw->tracking_enabled = 0
 *   00e6d910    move.w D2w,D0w
 *   00e6d912    add.w D0w,D0w
 *   00e6d914    move.w (A2),(0x48,A5,D0w*0x1)     ; asid_to_unit[asid] = *unit
 *   00e6d918    st -(SP)                          ; full = true
 *   00e6d91a    move.w (A2),-(SP)                 ; unit
 *   00e6d91c    bsr.w 0x00e6d7e2                  ; smd_$reset_display_globals
 *   00e6d920    movem.l (-0x20,A6),{  D2 A2 A3 A4 A5}
 *   00e6d926    unlk A6
 *   00e6d928    rts
 */

#include "smd/smd_internal.h"
#include "term/term.h"
#include "tpad/tpad.h"

/*
 * The `discipline` argument of TERM_$SET_REAL_LINE_DISCIPLINE is a constant
 * word in the code region, reached with "pea (0x70,PC)" at 0x00E6D8B8, i.e.
 * 0x00E6D8BA + 0x70 = 0x00E6D92A, which holds 0x0001 (read with gsk); it is
 * declared as SMD_ONE_LOCK_DATA in smd_internal.h.  The `line` argument is
 * "pea (0x6e,PC)" at 0x00E6D8BC -> 0x00E6D92C, the zero word already
 * declared there as SMD_ACQ_LOCK_DATA.
 */

/*
 * SMD_$ASSOC - Associate display with process
 *
 * Associates the specified display unit with the current process's
 * address space ID (ASID). This establishes which display the process
 * will use for subsequent SMD operations.
 *
 * Parameters:
 *   unit - Pointer to display unit number
 *   asid - Pointer to ASID (if NULL/0, uses current process's ASID)
 *   status_ret - Status return
 */
void SMD_$ASSOC(uint16_t *unit, uint16_t *asid, status_$t *status_ret)
{
    uint16_t disp_type;
    uint16_t use_asid;
    smd_display_unit_t *rec;
    smd_display_hw_t *hw;

    /* Validate display unit by checking its type (0x00e6d89a) */
    disp_type = SMD_$INQ_DISP_TYPE(unit);

    /* 0x00e6d8a0-0x00e6d8a6: seq/tst.b/bpl - the Domain boolean is 0xFF when
     * the type is zero, and the branch is taken when it is *not* negative. */
    if (disp_type == 0) {
        *status_ret = status_$display_invalid_unit_number;
        return;
    }

    *status_ret = status_$ok;                    /* 0x00e6d8b0 */

    SMD_GLOBALS.default_unit = (int16_t)*unit;   /* 0x00e6d8b2 */

    /* 0x00e6d8b6-0x00e6d8c0: both leading arguments are constant words in the
     * code region, passed by reference. */
    TERM_$SET_REAL_LINE_DISCIPLINE(&SMD_ACQ_LOCK_DATA,
                                   &SMD_ONE_LOCK_DATA,
                                   status_ret);

    /* Set trackpad unit (0x00e6d8cc) */
    TPAD_$SET_UNIT(unit);

    /* 0x00e6d8d4-0x00e6d8e0: use the caller's ASID only when the current
     * process has none. */
    use_asid = PROC1_$AS_ID;
    if (use_asid == 0) {
        use_asid = *asid;
    }

    /* 0x00e6d8e4-0x00e6d8f2: A3 = 0xE2E3FC + unit*0x10C, hw at (-0xF4,A3) */
    rec = smd_$unit_rec((int16_t)*unit);
    hw = rec->hw;

    /* 0x00e6d8f6: drop the current owner before the reset ... */
    rec->owner_asid = 0;
    smd_$reset_unit_display((int16_t)*unit, (boolean)0xFF);

    /* ... and install the new one afterwards (0x00e6d904-0x00e6d908) */
    rec->owner_asid = use_asid;
    rec->borrowed_asid = 0;

    /* 0x00e6d90c */
    hw->tracking_enabled = 0;

    /* 0x00e6d914 */
    SMD_GLOBALS.asid_to_unit[use_asid] = *unit;

    /* 0x00e6d91c */
    smd_$reset_display_globals((int16_t)*unit, (boolean)0xFF);
}
