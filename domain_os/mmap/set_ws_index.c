/*
 * MMAP_$SET_WS_INDEX - Give a process a working-set list slot
 *
 * Original address: 0x00E0D1C8 (168 bytes; `E0D1C8 MMAP_$SET_WS_INDEX` in
 * the SAU2 map).  Single caller: 0x00E14612.
 *
 * Frame (0x00E0D1C8-0x00E0D1D0): `link.w A6,#-4`, D2/D3/A2/A5 saved, A5 =
 * the MMAP_ block (0xE23284).  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  pid        word (D2)
 *   (0xA,A6)  wsl_index  word pointer (A2), in/out: 0 asks for a free slot
 *
 * 0x00E0D1DE-0x00E0D1EE  pid > 0x40 -> CRASH_SYSTEM(&mmap_$illegal_pid
 *                        _00e0d1c4) (cell shared with MMAP_$FREE_WSL)
 * 0x00E0D1F0-0x00E0D21E  *wsl_index == 0: scan MMAP_$WSL[8..69] (A0 = A5 +
 *                        0x120 so (0x2C,A0) is slot 8's flags word, `dbf'
 *                        on 0x3D = 62 slots, `tst.w / bmi' = bit 7 of the
 *                        flags byte, IN_USE); the first free slot is stored
 *                        through the pointer and raises MMAP_$WSL_HI_MARK
 *                        ((0xA22,A5)) when above it
 * 0x00E0D220-0x00E0D238  otherwise the given index must be >= 5 and <=
 *                        MMAP_$WSL_HI_MARK, else CRASH_SYSTEM(&mmap_
 *                        $illegal_wsl_index_00e0c9e0)
 * 0x00E0D23A-0x00E0D248  *wsl_index still 0 (no free slot) ->
 *                        CRASH_SYSTEM(&mmap_$ws_lists_exhausted_00e0d270)
 *                        and straight to the exit
 * 0x00E0D24A-0x00E0D262  `bset.b #0xf' = MMAP_$WSL[*wsl_index].flags |=
 *                        0x80; MMAP_PID_TO_WSL[pid] = *wsl_index
 */

#include "mmap/mmap_internal.h"
#include "misc/misc.h"

/*
 * 0x00E0D23E: pea (0x30,PC) -> 0x00E0D270, jsr CRASH_SYSTEM at 0x00E0D242.
 * Image bytes 00 06 00 0b = "ws lists exhsusted" (sic); only this routine
 * uses the cell.
 */
static const status_$t mmap_$ws_lists_exhausted_00e0d270 =
    status_$mmap_ws_lists_exhausted;

#define MMAP_SET_WS_INDEX_FIRST_SLOT 8
#define MMAP_SET_WS_INDEX_SLOTS      62   /* dbf on 0x3D: slots 8..69 */

void MMAP_$SET_WS_INDEX(uint16_t pid, uint16_t *wsl_index)
{
    uint16_t i;

    if (pid > MMAP_MAX_PID) {                                /* 0x00E0D1DE */
        CRASH_SYSTEM(&mmap_$illegal_pid_00e0d1c4);           /* 0x00E0D1E8 */
    }

    if (*wsl_index == 0) {                                   /* 0x00E0D1F0 */
        /* 0x00E0D200-0x00E0D21A: slots 8..69 */
        for (i = MMAP_SET_WS_INDEX_FIRST_SLOT;
             i < MMAP_SET_WS_INDEX_FIRST_SLOT + MMAP_SET_WS_INDEX_SLOTS; i++) {
            if (!(MMAP_$WSL[i].flags & WSL_FLAG_IN_USE)) {   /* 0x00E0D200 */
                *wsl_index = i;                              /* 0x00E0D206 */
                if (i > MMAP_$WSL_HI_MARK) {                 /* 0x00E0D208 */
                    MMAP_$WSL_HI_MARK = i;                   /* 0x00E0D20E */
                }
                break;
            }
        }
    } else {
        if (*wsl_index < WSL_INDEX_MIN_USER                  /* 0x00E0D222 */
            || *wsl_index > MMAP_$WSL_HI_MARK) {             /* 0x00E0D228 */
            CRASH_SYSTEM(&mmap_$illegal_wsl_index_00e0c9e0); /* 0x00E0D232 */
        }
    }

    if (*wsl_index == 0) {                                   /* 0x00E0D23A */
        CRASH_SYSTEM(&mmap_$ws_lists_exhausted_00e0d270);    /* 0x00E0D242 */
        return;                                              /* 0x00E0D248 */
    }

    MMAP_$WSL[*wsl_index].flags |= WSL_FLAG_IN_USE;          /* 0x00E0D254 */
    MMAP_PID_TO_WSL[pid] = *wsl_index;                       /* 0x00E0D262 */
}
