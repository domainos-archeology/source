/*
 * LOG_$UPDATE - Hand back the wired log page when the log is dirty
 *
 * Re-emitted from the image (0x00E1760C..0x00E1763C, 50 bytes; the batch
 * address 0x00E1762E is the "return 0" label) and verified; the previous
 * body was faithful.  A5 = 0xE2B280 = LOG_$STATE.
 *
 *   00e17618  tst.l (0x14,A5) / beq 0            ; LOG_$LOGFILE_PTR
 *   00e1761e  tst.b (0x18,A5) / bpl 0            ; dirty_flag (Domain boolean)
 *   00e17624  move.l (0x10,A5),D0 ; clr.b (0x18,A5)
 *   00e1762e  clr.l D0
 *   00e17630  clr.l (0x00e0000c).l               ; on every path
 *
 * Original address: 0x00e1760c
 */

#include "log/log_internal.h"

uint32_t LOG_$UPDATE(void)
{
    uint32_t result;             /* D0 */

    if (LOG_$LOGFILE_PTR == NULL || LOG_$STATE.dirty_flag >= 0) {
        result = 0;                                  /* 0x00E1762E */
    } else {
        result = LOG_$STATE.wired_handle;            /* 0x00E17624 */
        LOG_$STATE.dirty_flag = 0;                   /* 0x00E17628 */
    }

    LOG_$LAST_ENTRY.magic = 0;                    /* 0x00E17630 */
    return result;
}
