/*
 * MMAP_$FREE_WSL - Release a process's working-set list slot
 *
 * Original address: 0x00E0D158 (106 bytes; `E0D158 MMAP_$FREE_WSL` in the
 * SAU2 map).  Single caller: 0x00E146FE.
 *
 * Frame (0x00E0D158-0x00E0D166): `link.w A6,#-8`, D2/D3/A5 saved, A5 = the
 * MMAP_ block (0xE23284).  One argument: (0x8,A6) pid, word (D2).
 *
 * 0x00E0D16A-0x00E0D17A  pid > 0x40 (unsigned `bls' skips) ->
 *                        CRASH_SYSTEM(&mmap_$illegal_pid_00e0d1c4), the
 *                        cell at 0x00E0D1C4 (00 06 00 0a) shared with
 *                        MMAP_$SET_WS_INDEX
 * 0x00E0D17C-0x00E0D188  wsl_index = MMAP_PID_TO_WSL[pid]
 *                        ((0xA22,A5) + pid*2 = 0xE23CA6 + pid*2); the slot
 *                        is then cleared
 * 0x00E0D18C-0x00E0D19C  scan MMAP_PID_TO_WSL[1..64] (A0 = A5+2, `dbf' on
 *                        0x3F = 64 words) for another process still using
 *                        the same index; a hit returns without purging.
 *                        Entry 0 (MMAP_$WSL_HI_MARK) is NOT part of the
 *                        scan.
 * 0x00E0D1A0-0x00E0D1A4  MMAP_$PURGE(wsl_index) - the compiler allocates a
 *                        2-byte result slot (`subq.l #2,SP') it never reads
 * 0x00E0D1A8-0x00E0D1B2  `bclr.b #0xf,(0x2c,A5,D0w)' with D0 = index*0x24:
 *                        bit 15 of a byte operand is bit 7 of
 *                        MMAP_$WSL[wsl_index].flags, i.e. clear
 *                        WSL_FLAG_IN_USE
 */

#include "mmap/mmap_internal.h"
#include "misc/misc.h"

void MMAP_$FREE_WSL(uint16_t pid)
{
    uint16_t wsl_index;   /* D3 */
    uint16_t i;

    if (pid > MMAP_MAX_PID) {                                 /* 0x00E0D16A */
        CRASH_SYSTEM(&mmap_$illegal_pid_00e0d1c4);            /* 0x00E0D174 */
    }

    wsl_index = MMAP_PID_TO_WSL[pid];                         /* 0x00E0D184 */
    MMAP_PID_TO_WSL[pid] = 0;                                 /* 0x00E0D188 */

    /* 0x00E0D18C-0x00E0D19C: entries 1..64, 64 iterations */
    for (i = 1; i <= MMAP_MAX_PID; i++) {
        if (MMAP_PID_TO_WSL[i] == wsl_index) {                /* 0x00E0D194 */
            return;                                            /* 0x00E0D198 */
        }
    }

    MMAP_$PURGE(wsl_index);                                   /* 0x00E0D1A4 */

    MMAP_$WSL[wsl_index].flags &= (uint8_t)~WSL_FLAG_IN_USE;  /* 0x00E0D1B2 */
}
