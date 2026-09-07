/*
 * PROC2_$GET_ASID - Get the address space ID for a process
 *
 * A thin wrapper around PROC2_$FIND_ASID that supplies its middle
 * (by-reference boolean) argument as a constant FALSE.
 *
 * Parameters:
 *   proc_uid   - UID of process
 *   status_ret - Status return
 *
 * Original address: 0x00E40702 (34 bytes)
 *
 * Full instruction trace:
 *   00e40702  link.w A6,-0x4
 *   00e40706  pea (A5)
 *   00e40708  lea (0xe7be84).l,A5       ; PROC2 module data base
 *   00e4070e  move.l (0xc,A6),-(SP)     ; status_ret        (arg 3)
 *   00e40712  pea (-0x1dc2,PC)          ; &FALSE, cell 0x00E3E952   (arg 2)
 *   00e40716  move.l (0x8,A6),-(SP)     ; proc_uid          (arg 1)
 *   00e4071a  bsr.b 0x00e40724          ; PROC2_$FIND_ASID
 *   00e4071c  movea.l (-0x8,A6),A5
 *   00e40720  unlk A6
 *   00e40722  rts
 *
 * Note the arguments are pushed but the stack is never popped: the `unlk`
 * discards them, and PROC2_$FIND_ASID's result in D0 is simply left there.
 * PROC2_$GET_ASID is a Pascal procedure, so that result is not a return
 * value -- the ASID reaches the caller only through PROC2_$FIND_ASID's own
 * callers, and this entry point exists purely for the status.
 */

#include "proc2/proc2_internal.h"

/*
 * The by-reference boolean passed at 0x00E40712 (`pea (-0x1dc2,PC)`).
 * The cell is the word at 0x00E3E952 in the PROC2 code region, immediately
 * after the `unlk`/`rts` at 0x00E3E94E; its first byte is 0x00, and
 * PROC2_$FIND_ASID reads it with `tst.b (A0)` / `bpl` at 0x00E4075A.
 * FALSE therefore selects proc2_info_t.asid (+0x96) unconditionally,
 * never the alternate ASID at +0x98.
 */
static const int8_t proc2_$false_00e3e952 = 0;

void PROC2_$GET_ASID(uid_t *proc_uid, status_$t *status_ret)
{
    /* 0x00E4071A */
    (void)PROC2_$FIND_ASID(proc_uid, (int8_t *)&proc2_$false_00e3e952,
                           status_ret);
}
