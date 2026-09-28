/*
 * flp/int.c - FLP_$INT (0x00E19F6C, 172 bytes; map segment `I E19F6C FLP_
 * size = AC`)
 *
 * The floppy interrupt handler.  Collects the FDC's result phase into
 * FLP_$SREGS - issuing SENSE INTERRUPT STATUS first if the FDC is waiting
 * for a command - marks the unit's disk as changed when ST0 says so, and
 * advances FLP_$EC so EXCS wakes.
 *
 * Frame (link.w A6,-0x14; A2 D2 saved):
 *   A6-0x04  2  word    high byte cleared once, low byte (A6-0x03) is
 *                       where each data byte lands
 *   D0          count   result bytes stored so far
 *   D1          done    Domain boolean
 *   A1          destination in FLP_$SREGS
 *   A2          FLP_DATA (`movea.l #0xe7aef4,A2`)
 */

#include "flp/flp_internal.h"

int8_t FLP_$INT(dcte_t *dcte)
{
    uint16_t word;                      /* A6-0x04 */
    uint16_t count;                     /* D0 */
    int8_t done;                        /* D1 */
    volatile flp_regs_t *regs;          /* A0 */
    uint16_t *dst;                      /* A1 */

    /* 0x00E19F74-0x00E19F8E: the register base from the DCTE's controller
     * slot. */
    FLP_DATA.hw_addr = FLP_DATA.ctlr_table[dcte->cnum].hw_addr;

    /* 0x00E19F94-0x00E19FA0 */
    word = 0;
    count = 0;
    done = 0;
    regs = FLP_REGS();
    dst = FLP_$SREGS;

    /*
     * 0x00E19FA4-0x00E19FDA.  Each pass waits for RQM, then: with DIO set
     * a result byte is read (and kept while fewer than three have been);
     * with DIO clear and nothing read yet, SENSE INTERRUPT STATUS (8) is
     * written; with DIO clear after results, the phase is over.
     */
    do {
        while ((regs->status & FLP_STATUS_RQM) == 0) {
            ARCH_SPIN_TICK();
        }
        if ((regs->status & FLP_STATUS_DIO) != 0) {
            /* 0x00E19FB2-0x00E19FC8 */
            word = (uint16_t)((word & 0xFF00) | FLP_FDC_READ_DATA(regs));
            if (count < 3) {
                *dst = word;
                count++;
                dst++;
            }
        } else if (count == 0) {
            /* 0x00E19FCE */
            FLP_FDC_WRITE_DATA(regs, 8);
        } else {
            /* 0x00E19FD6 */
            done = -1;
        }
    } while (done >= 0);

    /* 0x00E19FDC-0x00E19FF8: ST0's low three bits equal to 6 flag a disk
     * change on the unit in its low two bits. */
    if ((FLP_$SREGS[0] & 7) == 6) {
        FLP_DATA.disk_change[FLP_$SREGS[0] & 3] = -1;
    }

    /* 0x00E19FFC-0x00E1A006 */
    EC_$ADVANCE_WITHOUT_DISPATCH(&FLP_$EC);

    /* 0x00E1A00C: `st D0b` */
    return -1;
}
