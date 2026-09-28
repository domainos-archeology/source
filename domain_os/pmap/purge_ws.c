/*
 * PMAP_$PURGE_WS - Purge a process's working set, or free its WSL slot
 *
 * 0x00E146B4 - 0x00E1471A (104 bytes, A5 = 0xE24D44).  Verified against
 * the disassembly 2026-09-27; the earlier emission was faithful.
 *
 * Arguments: (0x8,A6) index word (the pid, D2); (0xa,A6) is read as a
 * BYTE (`move.b (0xa,A6),D3b`) - the high byte of the second word, tested
 * with `tst.b / bpl`.  The callers push that word as 0 or 0xFF00, so an
 * int16_t `flags` tested `< 0` is the same bit the image tests.
 *
 * Under ML lock 0x14: negative -> MMAP_$PURGE of the WSL index owned by
 * the process (the word at 0xE23CA6 + index * 2, i.e. MMAP_PID_TO_WSL);
 * otherwise MMAP_$FREE_WSL(index).  Both callee result slots are discarded.
 */

#include "pmap/pmap_internal.h"
#include "mmap/mmap.h"

void PMAP_$PURGE_WS(int16_t index, int16_t flags)
{
    uint16_t slot;              /* (-0x2,A6) */

    ML_$LOCK(PMAP_LOCK_ID);                             /* 0x00E146CC */
    if (flags < 0) {                                    /* 0x00E146D8 */
        slot = MMAP_PID_TO_WSL[index];                  /* 0x00E146E6 */
        MMAP_$PURGE(slot);                              /* 0x00E146F2 */
    } else {
        MMAP_$FREE_WSL((uint16_t)index);                /* 0x00E146FE */
    }
    ML_$UNLOCK(PMAP_LOCK_ID);                           /* 0x00E1470C */
}
