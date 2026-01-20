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
 *   00e6defe    movea.l #0xe2e3fc,A1       ; A1 = SMD_DISPLAY_UNITS base
 *   00e6df04    muls.w #0x10c,D1           ; D1 = unit * 0x10C
 *   00e6df08    move.l (0x14,A1,D1*0x1),(0x4,A0) ; ctx->field_04 from unit[x].field_14
 *   00e6df0e    move.l (0x8,A1,D1*0x1),(0x8,A0)  ; ctx->field_08 from unit[x].field_08
 *   00e6df14    lea (0x0,A1,D1*0x1),A1     ; A1 = &unit[x]
 *   00e6df18    move.l (-0xf4,A1),(0xc,A0) ; ctx->hw_regs from unit[x-1].hw
 *   00e6df1e    clr.l (0x10,A0)            ; ctx->status = 0
 *   00e6df22    movea.l (-0x8,A6),A5
 *   00e6df26    unlk A6
 *   00e6df28    rts
 */
void SMD_$UTIL_INIT(smd_util_ctx_t *ctx)
{
    uint16_t asid;
    uint16_t unit_num;
    smd_display_unit_t *unit_base;

    /* Get current process's address space ID */
    asid = PROC1_$AS_ID;

    /* Look up display unit for this ASID */
    unit_num = SMD_GLOBALS.asid_to_unit[asid];

    if (unit_num == 0) {
        /* No display associated with this process */
        ctx->status = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /*
     * Get pointers from the display unit structure.
     *
     * The original code uses 1-based unit numbers and accesses data
     * at various offsets. The hardware pointer is accessed at -0xF4
     * from (base + unit*0x10C), which effectively gives us the hw
     * pointer from the previous slot - but since units are 1-based,
     * this correctly maps to the hw field of the unit structure.
     */
    unit_base = &SMD_DISPLAY_UNITS[unit_num];

    /* Copy field at offset 0x14 */
    ctx->field_04 = unit_base->field_14;

    /* Copy field at offset 0x08 (part of event_count_1) */
    ctx->field_08 = *((uint32_t *)((uint8_t *)unit_base + 0x08));

    /*
     * Get hardware register pointer.
     * The -0xF4 offset from current position maps to the hw field
     * of the actual unit (due to 1-based indexing).
     */
    ctx->hw_regs = (smd_hw_blt_regs_t *)SMD_DISPLAY_UNITS[unit_num - 1].hw;

    ctx->status = status_$ok;
}
