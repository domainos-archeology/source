/*
 * smd/util_init.c - SMD_$UTIL_INIT implementation
 *
 * Initializes a utility context structure for drawing operations.
 * This sets up pointers to display hardware and validates that the
 * calling process has an associated display.
 *
 * Original address: 0x00E6DED4
 */

#include "smd/smd_internal.h"

/*
 * SMD_$UTIL_INIT - Initialize utility context
 *
 * Sets up context for drawing operations by looking up the display unit
 * associated with the current process's address space ID.
 *
 * Parameters:
 *   ctx - Pointer to utility context structure to fill
 *
 * On success:
 *   ctx->hw_regs points to hardware BLT registers
 *   ctx->status is status_$ok (0)
 *
 * On failure (no display associated):
 *   ctx->status is status_$display_invalid_use_of_driver_procedure
 *
 * Original address: 0x00E6DED4
 *
 * Assembly:
 *   00e6ded4    link.w A6,-0x4
 *   00e6ded8    pea (A5)
 *   00e6deda    lea (0xe82b8c).l,A5        ; Load SMD_GLOBALS base
 *   00e6dee0    move.w (0x00e2060a).l,D0w  ; Get PROC1_$AS_ID
 *   00e6dee6    movea.l (0x8,A6),A0        ; A0 = ctx parameter
 *   00e6deea    add.w D0w,D0w              ; D0 = ASID * 2
 *   00e6deec    move.w (0x48,A5,D0w*0x1),D0w ; D0 = unit number from asid_to_unit
 *   00e6def0    bne.b 0x00e6defc           ; Branch if unit != 0
 *   00e6def2    move.l #0x130004,(0x10,A0) ; ctx->status = invalid_use
 *   00e6defa    bra.b 0x00e6df22
 *   00e6defc    move.w D0w,D1w             ; D1 = unit number
 *   00e6defe    movea.l #0xe2e3fc,A1       ; A1 = &SMD_$WIRED_DATA
 *   00e6df04    muls.w #0x10c,D1           ; D1 = unit * 0x10C
 *   00e6df08    move.l (0x14,A1,D1*0x1),(0x4,A0) ; ctx->display_base (rec+0x108)
 *   00e6df0e    move.l (0x8,A1,D1*0x1),(0x8,A0)  ; ctx->ctrl_regs   (rec+0x0FC)
 *   00e6df14    lea (0x0,A1,D1*0x1),A1     ; A1 = biased unit record
 *   00e6df18    move.l (-0xf4,A1),(0xc,A0) ; ctx->hw          (rec+0x000)
 *   00e6df1e    clr.l (0x10,A0)            ; ctx->status = 0
 *   00e6df22    movea.l (-0x8,A6),A5
 *   00e6df26    unlk A6
 *   00e6df28    rts
 */
void SMD_$UTIL_INIT(smd_util_ctx_t *ctx)
{
    uint16_t asid;
    int16_t unit_num;
    smd_display_unit_t *rec;

    /* 0x00e6dee0-0x00e6deec */
    asid = PROC1_$AS_ID;
    unit_num = (int16_t)SMD_GLOBALS.asid_to_unit[asid];

    if (unit_num == 0) {
        /* 0x00e6def2 - and nothing else in the record is written */
        ctx->status = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /*
     * 0x00e6defe-0x00e6df18: all three pointers come out of the same biased
     * record (A1 + unit*0x10C), at displacements +0x14, +0x08 and -0xF4,
     * i.e. record offsets 0x108, 0xFC and 0x00.
     */
    rec = smd_$unit_rec(unit_num);
    ctx->display_base = rec->display_base;
    ctx->ctrl_regs = (smd_hw_blt_regs_t *)rec->ctrl_regs;
    ctx->hw = rec->hw;

    /* 0x00e6df1e */
    ctx->status = status_$ok;
}
