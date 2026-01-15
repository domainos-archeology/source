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
 *   ; ... continues with TERM setup and asid mapping
 */

#include "smd/smd_internal.h"
#include "term/term.h"
#include "tpad/tpad.h"

/* Static lock data for synchronization - addresses from original */
static uint16_t smd_assoc_lock_data_1 = 0;
static uint16_t smd_assoc_lock_data_2 = 0;

/* Internal helper to update display flags */
static void smd_update_display_flags(uint16_t unit, uint8_t flag);

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
    smd_display_unit_t *disp_unit;
    smd_display_hw_t *hw;

    /* Validate display unit by checking its type */
    disp_type = SMD_$INQ_DISP_TYPE(unit);

    if (disp_type == 0) {
        /* Invalid unit number */
        *status_ret = status_$display_invalid_unit_number;
        return;
    }

    *status_ret = status_$ok;

    /* Store as default display unit */
    SMD_GLOBALS.default_unit = *unit;

    /* Set up terminal line discipline for this display */
    /* Original calls TERM_$SET_REAL_LINE_DISCIPLINE at 0x00e1ab62 */
    TERM_$SET_REAL_LINE_DISCIPLINE(&smd_assoc_lock_data_1,
                                    &smd_assoc_lock_data_2,
                                    status_ret);

    /* Set trackpad unit */
    TPAD_$SET_UNIT(unit);

    /* Determine ASID to use */
    use_asid = PROC1_$AS_ID;
    if (use_asid == 0) {
        /* Use provided ASID if process ASID is 0 */
        use_asid = *asid;
    }

    /* Get display unit structure */
    disp_unit = smd_get_unit(*unit);
    hw = disp_unit->hw;

    /* Clear current ASID association for this unit */
    disp_unit->asid = 0;

    /* Update display flags - clear current state */
    smd_update_display_flags(*unit, 0xFF);

    /* Set new ASID association */
    disp_unit->asid = use_asid;
    disp_unit->field_14 = 0;

    /* Clear tracking enabled flag */
    hw->tracking_enabled = 0;

    /* Map ASID to display unit */
    SMD_GLOBALS.asid_to_unit[use_asid] = *unit;

    /* Update display flags with new state */
    smd_update_display_flags(*unit, 0xFF);
}

/*
 * smd_update_display_flags - Update display state flags
 *
 * Internal helper called during association to update display state.
 *
 * Original addresses: 0x00E6D736, 0x00E6D7E2
 */
static void smd_update_display_flags(uint16_t unit, uint8_t flag)
{
    /*
     * TODO: Full implementation requires understanding FUN_00e6d736
     * and FUN_00e6d7e2. These appear to update hardware state and
     * possibly refresh the display or cursor state.
     */
    (void)unit;
    (void)flag;
}
