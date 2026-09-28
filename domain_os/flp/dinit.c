/*
 * flp/dinit.c - FLP_$DINIT (0x00E3E112, 266 bytes)
 *
 * Unit initialisation, reached through FLP_$JUMP_TABLE[2] (+0x08) from
 * DISK_$MNT_DINIT.  Wires the format-table buffer the first time through,
 * recalibrates the unit, and reports the fixed geometry of the 1.2 MB drive
 * when the caller has no block count yet.
 *
 * Frame (link.w A6,-0x5c; A5 A4 A3 A2 D3 D2 saved):
 *   A6-0x50  4  status      MMU_$VTOP's status, then EXCS's result
 *   A6-0x48 72  excs_vol    the "volume" EXCS is given: an uninitialised
 *                           0x48-byte local (0x00E3E1A6 pea (-0x48,A6)) of
 *                           which only as_options (+0x28 = A6-0x20) is set:
 *                           `clr.w (-0x20,A6)` at 0x00E3E19A, so EXCS's
 *                           `btst.b #0x1,(0x29,A2)` (0x00E3E456) sees 0
 *   D2          ppn         MMU_$VTOP's result
 *   D3          unit        argument 1
 *   A2          num_blocks  argument 3
 *   A3          pvlabel_info argument 6
 */

#include "flp/flp_internal.h"

status_$t FLP_$DINIT(uint16_t unit, uint16_t ctlr, int32_t *num_blocks,
                     uint16_t *sec_per_track, uint16_t *num_heads,
                     flp_pvlabel_info_t *pvlabel_info, uint16_t *flags)
{
    status_$t status;                   /* A6-0x50 */
    disk_$volume_t excs_vol;            /* A6-0x48, uninitialised */
    uint32_t ppn;                       /* D2 */

    /* 0x00E3E130-0x00E3E13C */
    if (unit > 3) {
        return status_$invalid_unit_number;
    }

    /* 0x00E3E140-0x00E3E14E: the register base from this controller's slot
     * (ctlr * 8, no bounds check). */
    FLP_DATA.hw_addr = FLP_DATA.ctlr_table[ctlr].hw_addr;

    /*
     * 0x00E3E154-0x00E3E18C: once only, translate and wire the page holding
     * io_buffer and remember its physical address: ppn << 10 plus the
     * buffer's offset within the page (`andi.l #0x3ff`).
     */
    if (FLP_DATA.initialized >= 0) {
        ppn = MMU_$VTOP(ARCH_PTR_TO_VA(FLP_DATA.io_buffer), &status);
        WP_$WIRE(ppn);
        FLP_DATA.fmt_buf_pa = (ppn << 10)
                            + (ARCH_PTR_TO_VA(FLP_DATA.io_buffer) & 0x3FF);
        FLP_DATA.initialized = -1;                  /* st (0x138,A5) */
    }

    /* 0x00E3E190-0x00E3E1A2: `clr.w (-0x20,A6)` is excs_vol.as_options
     * (A6-0x48 + 0x28), the one field of the stand-in volume EXCS reads. */
    FLP_REGS()->control = 3;
    excs_vol.as_options = 0;
    FLP_DATA.cmd_retry = 0;
    FLP_DATA.recal_cmd[1] = unit;

    /* 0x00E3E1A6-0x00E3E1BA: RECALIBRATE, two words (0x00E3E21C), the
     * local record standing in for the volume. */
    status = EXCS(FLP_DATA.recal_cmd, &flp_word_two, &excs_vol);

    /* 0x00E3E1BE-0x00E3E1D2: the unit is at cylinder 0 with no pending
     * disk change, whatever the recalibrate said. */
    FLP_DATA.unit_cyl[unit] = 0;
    FLP_DATA.disk_change[unit] = 0;

    /* 0x00E3E1D6-0x00E3E206: with a good recalibrate and no block count
     * supplied (`tst.l (A2)` / `bgt` skips), report the geometry. */
    if (status == status_$ok && *num_blocks <= 0) {
        *flags = 0;                                 /* 0x00E3E1E4 */
        *sec_per_track = 8;                         /* 0x00E3E1EA */
        *num_heads = 2;                             /* 0x00E3E1F2 */
        *num_blocks = 0x4D0;                        /* 0x00E3E1F6 */
        *pvlabel_info = flp_dinit_pvlabel;          /* 0x00E3E1FC-0x00E3E206 */
    }

    /* 0x00E3E208: on every path, including failures. */
    pvlabel_info->w_06 = 1;

    /* 0x00E3E20E */
    return status;
}
