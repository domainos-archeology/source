/*
 * PROC2_$SIGRETURN - Return from a signal handler
 *
 * Re-emitted from the image (0x00E3F582..0x00E3F63C, 188 bytes).
 *
 * Frame (link.w A6,-0x10; A5 = 0xE7BE84):
 *   (0x8,A6)  context_ptr  -> A2 (pointer to the sigcontext pointer)
 *   (0xC,A6)  regs_ptr     (0x10,A6) fp_state_ptr   -- passed straight on
 *   (0x14,A6) result       -> A4 (two longwords)
 *   D2 = (*context_ptr)->sc_onstack, D3 = (*context_ptr)->sc_mask
 *
 *   00e3f5ce  tst.l D2 ; sne ; lsr.b #7 ; lsl.b #2   -> 0 or 4
 *   00e3f5d4  andi.b #-0x5,(-0xba,A3) ; or.b          -> HIGH byte bit 2:
 *             flags bit 0x0400 := (sc_onstack != 0)
 *   00e3f5e0  entry+0x78 = sc_mask
 *   00e3f5e4  if (+0x80 & ~+0x78) != 0: DELIVER_PENDING(+0x1C)
 *   00e3f60a  result[0] = +0x78; result[1] = flags & 0x0400 ? 1 : 0
 *   00e3f624  FIM_$FAULT_RETURN(context_ptr, regs_ptr, fp_state_ptr)
 *
 * The old C cleared/set 0x0004; the byte operations are on the HIGH byte,
 * so the bit is 0x0400 -- the same bit result[1] reports.
 *
 * Only reference: the SVC table entry at 0x00E7BA42.
 *
 * Original address: 0x00e3f582
 */

#include "proc2/proc2_internal.h"

NORETURN void PROC2_$SIGRETURN(void *context_ptr, void *regs_ptr,
                               void *fp_state_ptr, uint32_t *result)
{
    sigcontext_t *sigctx;            /* A0 = *(A2) */
    int32_t onstack;                 /* D2 */
    uint32_t new_mask;               /* D3 */
    proc2_info_t *entry;             /* A3 */

    /* 0x00E3F590-0x00E3F59C */
    sigctx = *(sigcontext_t **)context_ptr;
    onstack = sigctx->sc_onstack;
    new_mask = sigctx->sc_mask;

    /* 0x00E3F5A0-0x00E3F5BE (before the lock) */
    entry = P2_INFO_ENTRY((int16_t)PROC2_$DATA.pid_to_index[PROC1_$CURRENT]);

    /* 0x00E3F5AE/0x00E3F5C2-0x00E3F5CC */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E3F5CE-0x00E3F5DC */
    entry->flags &= (uint16_t)~0x0400;
    if (onstack != 0) {
        entry->flags |= 0x0400;
    }

    /* 0x00E3F5E0 */
    entry->sig_blocked_2 = new_mask;

    /* 0x00E3F5E4-0x00E3F5FA */
    if ((entry->sig_mask_2 & ~entry->sig_blocked_2) != 0) {
        PROC2_$DELIVER_PENDING_INTERNAL((int16_t)entry->self_index);
    }

    /* 0x00E3F5FC-0x00E3F608 */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E3F60A-0x00E3F620 */
    result[0] = entry->sig_blocked_2;
    result[1] = ((entry->flags & 0x0400) != 0) ? 1u : 0u;

    /* 0x00E3F624-0x00E3F62E */
    FIM_$FAULT_RETURN((sigcontext_t **)context_ptr, (uint32_t **)regs_ptr, fp_state_ptr);
}
