/*
 * flp/cinit.c - FLP_$CINIT (0x00E3E002, 266 bytes)
 *
 * Controller initialisation: probe the FDC, record its DCTE and register
 * base in the controller table, initialise FLP_$EC, drain whatever result
 * phase the FDC is sitting in, send SPECIFY, and register the driver with
 * DISK.
 *
 * Frame (link.w A6,-0x1c; A5 A3 A2 D3 D2 saved):
 *   A6-0x1C  4  jump_table_va   DISK_$REGISTER's fifth argument, by address
 *   A6-0x14  2  ctlr_word       DISK_$REGISTER's controller word, by address
 *   A6-0x10  2  sense_cmd       the word 8 SHAKE writes to the FDC
 *   A6-0x0E     probe result    io_$probe's third argument
 *   A2          dcte (argument 1)
 *   A3          the register base
 *   D2          controller number
 *   D3          drain loop counter
 */

#include "flp/flp_internal.h"

status_$t FLP_$CINIT(dcte_t *dcte)
{
    uint8_t probe_result[0x0A];         /* A6-0x0E */
    uint16_t sense_cmd;                 /* A6-0x10 */
    uint16_t ctlr_word;                 /* A6-0x14 */
    uint32_t jump_table_va;             /* A6-0x1C */
    status_$t status;                   /* A6-0x04 */
    volatile flp_regs_t *regs;          /* A3 */
    uint16_t ctlr;                      /* D2 */
    uint16_t tries;                     /* D3 */

    /*
     * 0x00E3E014-0x00E3E02C: io_$probe(&0, &dcte->disk_dinit, result).  The
     * first argument is the zero word at 0x00E3E10E (pea (0xf0,PC) at
     * 0x00E3E01C); the second is the address of the DCTE's +0x34 longword,
     * which this driver treats as the controller's register base.  A
     * Domain false (`tst.b D0b` / `bmi`) means no controller.
     */
    if (io_$probe(&flp_word_zero, &dcte->disk_dinit, probe_result) >= 0) {
        status = status_$io_controller_not_in_system;   /* 0x00E3E02E */
        return status;
    }

    /* 0x00E3E038-0x00E3E050: remember the base, then the DCTE and base in
     * this controller's 8-byte slot.  No bounds check on ctlr. */
    ctlr = dcte->cnum;
    FLP_DATA.hw_addr = dcte->disk_dinit;
    FLP_DATA.ctlr_table[ctlr].dcte_va = ARCH_PTR_TO_VA(dcte);
    FLP_DATA.ctlr_table[ctlr].hw_addr = FLP_DATA.hw_addr;

    /* 0x00E3E056-0x00E3E060 */
    EC_$INIT(&FLP_$EC);

    /* 0x00E3E062-0x00E3E06A */
    regs = FLP_REGS();
    status = status_$ok;
    tries = 0;

    /*
     * 0x00E3E06C-0x00E3E0C0: while the FDC reports a command in progress,
     * either read three result words into FLP_$SREGS (DIO set: SHAKE with
     * the count 3 at 0x00E3DDC2 and the read direction 0 at 0x00E3E10E) or
     * write it a SENSE INTERRUPT STATUS (DIO clear: the word 8 with the
     * one cell 0x00E3E110 pushed twice as both count and direction).  The
     * counter allows 201 passes (`cmpi.w #0xc8` / `bls` after the
     * increment) before giving up with status_$disk_controller_error.
     */
    while ((regs->status & FLP_STATUS_CMD_MASK) != 0) {
        if ((regs->status & FLP_STATUS_DIO) != 0) {
            /* 0x00E3E078-0x00E3E084 */
            SHAKE(FLP_$SREGS, &flp_word_three, &flp_word_zero);
        } else {
            /* 0x00E3E086-0x00E3E092 */
            sense_cmd = 8;
            SHAKE(&sense_cmd, &flp_word_one, &flp_word_one);
        }
        tries++;                                    /* 0x00E3E09E */
        if (tries > 0xC8) {                         /* 0x00E3E0A0-0x00E3E0A4 */
            status = status_$disk_controller_error; /* 0x00E3E0A6 */
            break;
        }
    }
    /* 0x00E3E0BC-0x00E3E0C0: `tst.l (-0x4,A6)` / `bne` - only the timeout
     * sets it. */
    if (status != status_$ok) {
        return status;
    }

    /* 0x00E3E0C2-0x00E3E0DA: SPECIFY (three words at +0x108, written: count
     * 0x00E3DDC2, direction 0x00E3E110). */
    status = SHAKE(FLP_DATA.specify_cmd, &flp_word_three, &flp_word_one);
    if (status != status_$ok) {
        return status;
    }

    /*
     * 0x00E3E0DC-0x00E3E0FA: DISK_$REGISTER(&1, &ctlr, &FLP_DATA.unit_count,
     * &dcte->disk_error_que, &jump_table_va).  The device type is the one
     * word at 0x00E3E110 (pea (0x18,PC) at 0x00E3E0F6); the flags word is
     * the DCTE's +0x3C cell; the jump table is passed as a longword holding
     * the address of FLP_DATA (`lea (A5),A0` / `move.l A0,(-0x1c,A6)`).
     * The Domain boolean it returns is not examined.
     */
    ctlr_word = ctlr;
    jump_table_va = ARCH_PTR_TO_VA(&FLP_DATA);
    (void)DISK_$REGISTER((uint16_t *)&flp_word_one, &ctlr_word,
                         &FLP_DATA.unit_count, (uint16_t *)&dcte->disk_error_que,
                         (void **)&jump_table_va);

    /* 0x00E3E100: the status left by SHAKE, i.e. status_$ok. */
    return status;
}
