/*
 * PROC1_$BEGIN_ATOMIC_OP - Enter an atomic-operation region
 * Original address: 0x00e209e6 (8 bytes)
 *
 * 0x00E209E6  addq.w #0x1,(0x00e2060e).l     PROC1_$ATOMIC_OP_DEPTH++
 * 0x00E209EC  rts
 *
 * PROC1_$ATOMIC_OP_DEPTH is the word at 0xE2060E in the PROC1_ASM segment.
 * While it is non-zero PROC1_$DISPATCH_INT2 (0x00E20A2E) refuses to switch
 * and crashes through the stub that follows this routine at 0x00E209EE
 * (`pea Bad_atomic_operation_err / jsr CRASH_SYSTEM / bra.b', kept in
 * proc1/sau2/dispatch.s); PROC1_$END_ATOMIC_OP (0x00E209FA) is the matching
 * decrement.  Ghidra's 20-byte extent for this function is the 8-byte
 * routine plus that 12-byte stub.
 *
 * Only caller: PROC1_$GET_INFO_INT (0x00E20F12, proc1/sau2/misc.s).
 */

#include "proc1/proc1_internal.h"

void PROC1_$BEGIN_ATOMIC_OP(void)
{
    /* 0x00E209E6 */
    PROC1_$ATOMIC_OP_DEPTH++;
}
