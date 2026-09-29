/*
 * PROC2_$SIGPAUSE - Install a temporary mask and wait for a signal
 *
 * Re-emitted from the image (0x00E3FA10..0x00E3FB32, 292 bytes).
 *
 * Frame (link.w A6,-0x1C; A5 = 0xE7BE84):
 *   (0x8,A6)  new_mask ptr -> D2 = *ptr    (0xC,A6) result -> A2 (two longwords)
 *   A6-0x8    the one-element EC pointer array {&FIM_$WIRED_DATA.quit_ec[AS_ID]}
 *   A6-0x10   the one-element value array {FIM_$WIRED_DATA.quit_value[AS_ID] + 1}
 *   A4 = the caller's entry (biased, found BEFORE the lock)
 *
 *   00e3fa56  move.l (-0x6c,A4),(-0x5c,A4)   ; entry+0x88 = entry+0x78 (saved mask)
 *   00e3fa5c  move.l D2,(-0x6c,A4)           ; entry+0x78 = *new_mask
 *   00e3fa60  bset.b #0x6,(-0xba,A4)         ; HIGH byte bit 6 -> flags |= 0x4000
 *   00e3fa74  result[0] = entry+0x78; result[1] = flags & 0x0400 ? 1 : 0
 *   00e3fa8e  A6-0x8 = &FIM_$WIRED_DATA.quit_ec[AS_ID] (0xE22002 + AS_ID*12)
 *   loop at 00e3fab8:
 *     A6-0x10 = FIM_$WIRED_DATA.quit_value[AS_ID] + 1          (0xE222BA + AS_ID*4)
 *     if (entry+0x80 & ~entry+0x78) != 0: lock, DELIVER_PENDING(+0x1C), unlock, exit
 *     EC_$WAITN(&A6-0x8, &A6-0x10, 1)
 *     FIM_$WIRED_DATA.quit_value[AS_ID] = FIM_$WIRED_DATA.quit_ec[AS_ID].value ; back to the loop
 *
 * PROC1_$AS_ID is re-read through A2 (= 0xE2060A) on every iteration.
 * Only reference: the SVC table entry at 0x00E7B5F2.
 *
 * Original address: 0x00e3fa10
 */

#include "proc2/proc2_internal.h"

void PROC2_$SIGPAUSE(uint32_t *new_mask, uint32_t *result)
{
    uint32_t mask_val;               /* D2 */
    proc2_info_t *entry;             /* A4 */
    ec_$eventcount_t *ec_list[1];    /* A6-0x8 */
    int32_t wait_val[1];             /* A6-0x10 */

    /* 0x00E3FA1E-0x00E3FA46 */
    mask_val = *new_mask;
    entry = P2_INFO_ENTRY((int16_t)PROC2_$DATA.pid_to_index[PROC1_$CURRENT]);

    /* 0x00E3FA36/0x00E3FA4A-0x00E3FA54 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E3FA56-0x00E3FA60 */
    entry->pad_88 = entry->sig_blocked_2;
    entry->sig_blocked_2 = mask_val;
    entry->flags |= 0x4000;

    /* 0x00E3FA66-0x00E3FA72 */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E3FA74-0x00E3FA8A */
    result[0] = entry->sig_blocked_2;
    result[1] = ((entry->flags & 0x0400) != 0) ? 1u : 0u;

    /* 0x00E3FA8E-0x00E3FAA6 */
    ec_list[0] = &FIM_$WIRED_DATA.quit_ec[PROC1_$AS_ID];

    for (;;) {
        /* 0x00E3FAB8-0x00E3FAC2 */
        wait_val[0] = (int32_t)(FIM_$WIRED_DATA.quit_value[PROC1_$AS_ID] + 1);

        /* 0x00E3FAC6-0x00E3FAD0: (+0x80 & ~+0x78) != 0 -> deliver and leave */
        if ((entry->sig_mask_2 & ~entry->sig_blocked_2) != 0) {
            ML_$LOCK(PROC2_LOCK_ID);                                 /* 0x00E3FAD2 */
            PROC2_$DELIVER_PENDING_INTERNAL((int16_t)entry->self_index);   /* 0x00E3FAE6 */
            ML_$UNLOCK(PROC2_LOCK_ID);                               /* 0x00E3FAF2 */
            return;                                                  /* 0x00E3FAF8 */
        }

        /* 0x00E3FAFA-0x00E3FB0E */
        EC_$WAITN(ec_list, wait_val, 1);

        /* 0x00E3FB12-0x00E3FB22: FIM_$WIRED_DATA.quit_value[AS_ID] = FIM_$WIRED_DATA.quit_ec[AS_ID].value */
        FIM_$WIRED_DATA.quit_value[PROC1_$AS_ID] = (uint32_t)FIM_$WIRED_DATA.quit_ec[PROC1_$AS_ID].value;
    }
}
