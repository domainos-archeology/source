/*
 * PMAP_$WAKE_PURIFIER - Kick the purifier daemons, optionally waiting for pages
 *
 * 0x00E13A18 - 0x00E13A9A (132 bytes, A5 = 0xE24D44).  Verified against
 * the disassembly 2026-09-27; the earlier emission was faithful.
 *
 * Argument: (0x8,A6) read as a BYTE (`move.b (0x8,A6),D2b`) - the Domain
 * boolean in the high half of the word slot.
 *
 * wait_value = PMAP_$PAGES_EC.value + 1; EC_$ADVANCE(PMAP_$L_PURIFIER_EC);
 * if MMAP_WSL[DIRTY_RMT].page_count != 0, EC_$ADVANCE(PMAP_$R_PURIFIER_EC);
 * if the boolean is set: ML_$UNLOCK(0x14), EC_$WAITN({&PMAP_$PAGES_EC},
 * &wait_value, 1) with the one-entry array built in the frame at
 * (-0x8,A6), ML_$LOCK(0x14).
 */

#include "pmap/pmap_internal.h"
#include "ec/ec.h"
#include "mmap/mmap.h"

void PMAP_$WAKE_PURIFIER(int8_t wait)
{
    int32_t wait_value;                                 /* (-0x4,A6) */
    ec_$eventcount_t *ecs;                              /* (-0x8,A6) */

    wait_value = PMAP_$PAGES_EC.value + 1;              /* 0x00E13A2A */
    EC_$ADVANCE(&PMAP_$L_PURIFIER_EC);                  /* 0x00E13A34 */
    if (MMAP_WSL[MMAP_WSL_POOL_DIRTY_RMT].page_count != 0) { /* 0x00E13A40 */
        EC_$ADVANCE(&PMAP_$R_PURIFIER_EC);              /* 0x00E13A48 */
    }
    if (wait < 0) {                                     /* 0x00E13A54 */
        ML_$UNLOCK(PMAP_LOCK_ID);                       /* 0x00E13A5E */
        ecs = &PMAP_$PAGES_EC;
        EC_$WAITN(&ecs, &wait_value, 1);                /* 0x00E13A7C */
        ML_$LOCK(PMAP_LOCK_ID);                         /* 0x00E13A8C */
    }
}
