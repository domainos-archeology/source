/*
 * smd/blt.c - SMD_$BLT implementation
 *
 * Performs a bit block transfer operation.
 *
 * Original address: 0x00E6EC6E
 *
 * Assembly analysis:
 * This is a complex function that:
 * 1. Validates the current process has an associated display
 * 2. Validates the BLT parameters (mode bits)
 * 3. Converts user-facing BLT parameters to hardware format
 * 4. Acquires display lock (sync or async depending on mode)
 * 5. Starts the BLT operation
 * 6. Releases lock if sync mode
 *
 * Error codes:
 *   0x130004 - invalid use of driver procedure (no display)
 *   0x130028 - invalid BLT operation (bad mode bits)
 */

#include "smd/smd_internal.h"

/*
 * Hardware BLT parameter structure
 * This is the internal format passed to SMD_$START_BLT
 */
typedef struct smd_hw_blt_t {
    uint16_t    control;        /* 0x00: Control word */
    uint16_t    bit_pos;        /* 0x02: Bit position (plane select) */
    /* 0x04/0x06: copied verbatim from params[5]/params[6] as one longword
     * (0x00E6ED9C "move.l (0xa,A2),(-0xc,A6)").  SMD_$START_BLT copies both
     * straight into the controller registers at the same offsets
     * (0x00E15D3A / 0x00E15D34), so their meaning is a hardware detail. */
    uint16_t    field_04;
    uint16_t    field_06;
    uint16_t    y_extent;       /* 0x08: -1 - |params[11] - params[7]| */
    uint16_t    x_extent;       /* 0x0A: -1 - |params[12]>>4 - params[8]>>4| */
    uint16_t    y_start;        /* 0x0C: params[7] */
    uint16_t    x_start;        /* 0x0E: params[8] */
} smd_hw_blt_t;

_Static_assert(sizeof(smd_hw_blt_t) == 0x10, "smd_hw_blt_t size");

/* Lock data for async (0x00E6D92C = SMD_ACQ_LOCK_DATA, value 0) vs sync
 * (0x00E6DFF8 = SMD_SYNC_LOCK_DATA, value 1) BLT; declared in smd_internal.h */

/*
 * SMD_$BLT - Bit block transfer
 *
 * Performs a hardware-accelerated bit block transfer.
 *
 * Parameters:
 *   ctl        - User BLT control record, by reference
 *   param2     - Reserved (unused): &(the longword 0 at 0x00E6F978)
 *   param3     - Reserved (unused): &SMD_ACQ_LOCK_DATA (0x00E6D92C)
 *   status_ret - Output: status return
 *
 * BLT mode bits:
 *   bit 7: direction (must be 0)
 *   bit 6: invalid operation flag (must be 0)
 *   bit 5: use alternate ROP
 *   bit 4: async operation (use interrupts)
 *   bit 3: invalid operation flag (must be 0)
 *
 * Returns:
 *   status_$ok on success
 *   status_$display_invalid_use_of_driver_procedure if no display
 *   status_$display_invalid_blt_op if mode bits invalid
 */
void SMD_$BLT(smd_blt_ctl_t *ctl, const uint32_t *param2,
              const uint16_t *param3, status_$t *status_ret)
{
    /*
     * The body reaches every field the way the assembly does - word
     * displacements off A2 (0x00E6EC7C "movea.l (0x8,A6),A2") - so the
     * record is aliased here as the word array the code addresses.
     */
    uint16_t *params = (uint16_t *)ctl;
    int16_t unit;
    smd_display_hw_t *hw;
    smd_display_unit_t *rec;
    uint16_t mode;
    int16_t *lock_data;
    smd_hw_blt_t hw_params;
    int16_t dx, dy;

    (void)param2;
    (void)param3;

    /* 0x00e6ec8c */
    unit = (int16_t)SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID];

    if (unit == 0) {
        /* 0x00e6ec92 */
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /* 0x00e6ec9c: the mode word is latched into a local before anything else */
    mode = params[0];

    /* 0x00e6eca2-0x00e6ecb0: A3 = 0xE2E3FC + unit*0x10C, hw at (-0xF4,A3) */
    rec = smd_$unit_rec(unit);
    hw = rec->hw;

    /*
     * 0x00e6ecb4-0x00e6ecc2: both lock words are constants in the code
     * region reached with pea (d,PC):
     *   0x00E6ECBC pea (-0x1392,PC) -> 0x00E6ECBE - 0x1392 = 0x00E6D92C (0)
     *   0x00E6ECC2 pea (-0x0CCC,PC) -> 0x00E6ECC4 - 0x0CCC = 0x00E6DFF8 (1)
     */
    if ((mode & 0x10) != 0) {
        lock_data = (int16_t *)&SMD_ACQ_LOCK_DATA;
    } else {
        lock_data = &SMD_SYNC_LOCK_DATA;
    }

    /* 0x00e6ecc6 - the result is discarded here */
    SMD_$ACQ_DISPLAY(lock_data);

    /*
     * 0x00e6eccc-0x00e6ece0: the three validity tests are byte tests on the
     * LOW byte of the mode word (A6-0x15 is the second byte of the word at
     * A6-0x16), i.e. bits 7, 6 and 3 of the mode.
     */
    if ((int8_t)(mode & 0xFF) < 0 || (mode & 0x40) != 0 || (mode & 0x08) != 0) {
        *status_ret = status_$display_invalid_blt_op;
        SMD_$REL_DISPLAY();
        return;
    }

    /*
     * Build the hardware BLT record (0x00e6ecf0-0x00e6edd0).
     *
     * The control word is assembled a bit at a time.  Bit 15 comes from a
     * byte operation on the *high* byte of the word (0x00E6ECFC
     * "andi.b #0x7f,(-0x10,A6)" / 0x00E6ED06 "or.b D1b,(-0x10,A6)" with D1
     * = 0x80 when the mode word is negative), so it is bit 15 of the word,
     * not bit 7.  0x00E6ED0A then masks the word with 0x803F, leaving bit 15
     * and bits 0..5 alive.
     */
    hw_params.control =
        (uint16_t)(((mode & 0x8000u) ? 0x8000u : 0u) |
                   ((mode & 0x20u) ? 0x20u : 0u) |        /* 0x00e6ed10 */
                   ((mode & 0x10u) ? 0x10u : 0u) |        /* 0x00e6ed26 */
                   /* 0x00e6ed3c cmpi.b #0x2,(-0x14,A6): the first byte of the
                    * longword copied from params+2, i.e. the high byte of
                    * params[1].  Written with shifts so it holds on a
                    * little-endian host too. */
                   ((((params[1] >> 8) & 0xFFu) == 0x02u) ? 0x08u : 0u) |
                   /* 0x00e6ed52 cmpi.b #0x20,(-0x11,A6): the fourth byte of
                    * that longword, i.e. the low byte of params[2]. */
                   (((params[2] & 0xFFu) == 0x20u) ? 0x04u : 0u) |
                   ((mode & 0x02u) ? 0x02u : 0u) |        /* 0x00e6ed68 */
                   ((mode & 0x01u) ? 0x01u : 0u));        /* 0x00e6ed7e */

    /* 0x00e6ed92-0x00e6ed98 */
    hw_params.bit_pos = (uint16_t)(params[12] & 0x0F);

    /* 0x00e6ed9c move.l (0xa,A2),(-0xc,A6): params[5] lands at +0x04 and
     * params[6] at +0x06. */
    hw_params.field_04 = params[5];
    hw_params.field_06 = params[6];

    /* 0x00e6eda2-0x00e6edb2 */
    dy = (int16_t)(params[11] - params[7]);
    if (dy < 0) {
        dy = (int16_t)-dy;
    }
    hw_params.y_extent = (uint16_t)(-1 - dy);

    /* 0x00e6edb6-0x00e6edcc */
    dx = (int16_t)((uint16_t)(params[12] >> 4) - (uint16_t)(params[8] >> 4));
    if (dx < 0) {
        dx = (int16_t)-dx;
    }
    hw_params.x_extent = (uint16_t)(-1 - dx);

    /* 0x00e6edd0 move.l (0xe,A2),(-0x4,A6) */
    hw_params.y_start = params[7];
    hw_params.x_start = params[8];

    /* 0x00e6edd6-0x00e6ede0: the register base is the record's +0xFC field,
     * pushed by value. */
    SMD_$START_BLT((uint16_t *)&hw_params, hw, rec->ctrl_regs);

    /* 0x00e6edea-0x00e6edf8 */
    if ((mode & 0x10) == 0) {
        /* Sync mode - release the display now */
        SMD_$REL_DISPLAY();
    } else {
        /* Async mode - remember who owns the pending operation.  The store is
         * to (-0xec,A3), i.e. the record's +0x08 field, not the owner ASID. */
        rec->field_08 = PROC1_$AS_ID;
    }

    /* 0x00e6ee00 */
    *status_ret = status_$ok;
}
