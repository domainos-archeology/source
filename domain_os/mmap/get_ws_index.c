/*
 * MMAP_$GET_WS_INDEX - Report the working-set list index a process uses
 *
 * Original address: 0x00E0CA3A (64 bytes; `E0CA3A MMAP_$GET_WS_INDEX` in
 * the SAU2 map).  Single caller: 0x00E653CE.
 *
 * Frame (0x00E0CA3A-0x00E0CA42): `link.w A6,#0`, A2/A5 saved, A5 = the
 * MMAP_ block (0xE23284).  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  pid        word (D0)
 *   (0xA,A6)  wsl_index  word pointer (out)
 *   (0xE,A6)  status     longword pointer (out)
 *
 * 0x00E0CA4C-0x00E0CA50  *status = 0
 * 0x00E0CA52-0x00E0CA5E  pid > 0x40 (unsigned `bls' skips): *status =
 *                        0x6000A (status_$mmap_illegal_pid), done -
 *                        *wsl_index is left untouched
 * 0x00E0CA60-0x00E0CA6C  *wsl_index = MMAP_PID_TO_WSL[pid]
 *                        ((0xA22,A5) + pid*2 = 0xE23CA6 + pid*2)
 */

#include "mmap/mmap_internal.h"

void MMAP_$GET_WS_INDEX(uint16_t pid, uint16_t *wsl_index, status_$t *status)
{
    *status = status_$ok;                                    /* 0x00E0CA50 */

    if (pid > MMAP_MAX_PID) {                                /* 0x00E0CA52 */
        *status = status_$mmap_illegal_pid;                  /* 0x00E0CA58 */
        return;
    }

    *wsl_index = MMAP_PID_TO_WSL[pid];                       /* 0x00E0CA6C */
}
