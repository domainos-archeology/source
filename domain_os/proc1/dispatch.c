/*
 * PROC1_$DISPATCH - Dispatch, then force IPL 0
 * Original address: 0x00e20a18 (8 bytes)
 *
 * 0x00E20A18  bsr.b PROC1_$DISPATCH_INT      (0x00E20A20)
 * 0x00E20A1A  andi #-0x701,SR                forced IPL 0, not a restore
 * 0x00E20A1E  rts
 *
 * Callers raise to IPL 7 with a bare `ori #0x700,SR' and rely on this
 * routine to drop the mask again (PROC1_$RESUME 0x00E147D0, PROC1_$SUSPEND
 * 0x00E1485C, PROC1_$GET_INFO 0x00E14F3E, PROC1_$SET_PRIORITY 0x00E152C2,
 * PROC1_$INIT 0x00E2FA02).
 *
 * On m68k the routine is emitted with the rest of the dispatcher in
 * proc1/sau2/dispatch.s (the `bsr.b' target, PROC1_$DISPATCH_INT, is
 * register-convention assembly that falls through into DISPATCH_INT2 and
 * DISPATCH_INT3); this C body is the same two operations for other
 * targets.
 */

#include "proc1/proc1_internal.h"

#if !defined(ARCH_M68K)

void PROC1_$DISPATCH(void)
{
    /* 0x00E20A18 */
    PROC1_$DISPATCH_INT();

    /* 0x00E20A1A: andi #-0x701,SR */
    SET_IPL0();
}

#endif /* !ARCH_M68K */
