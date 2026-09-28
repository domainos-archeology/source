/*
 * MMAP_$AVAIL - Put a page back on the working-set list it belongs to
 *
 * Original address: 0x00E0CC64 (82 bytes; `E0CC64 MMAP_$AVAIL` in the SAU2
 * map).  Callers: 0x00E02872, 0x00E12E3A, 0x00E135FC, 0x00E13D70,
 * 0x00E1436C.
 *
 * Frame (0x00E0CC64-0x00E0CC72): `link.w A6,#-4`, D2/A2/A5 saved, A5 = the
 * MMAP_ block (0xE23284).  One argument: (0x8,A6) vpn, longword (D2).
 *
 * 0x00E0CC76-0x00E0CC80  A2 = 0xEB4800 + vpn*0x10; the mmape_t is then
 *                        addressed with -0x2000-based displacements
 *                        (-0x1FFC = wsl_index, +4)
 * 0x00E0CC84-0x00E0CC96  if page->wsl_index > 0x45 (unsigned `bls' skips
 *                        the crash when <= WSL_INDEX_MAX):
 *                        CRASH_SYSTEM(&mmap_$bad_avail_00e0ccb8) - the
 *                        cell at 0x00E0CCB8 (00 06 00 04), shared with
 *                        MMAP_$UNWIRE
 * 0x00E0CC98-0x00E0CCA8  mmap_$add_to_wsl(page, vpn, (word)page->wsl_index,
 *                        true): `st -(SP)' pushes the boolean, `clr.w D0 /
 *                        move.b' zero-extends the index byte to a word
 *
 * No spin lock is taken here; the callers hold it.
 */

#include "mmap/mmap_internal.h"
#include "misc/misc.h"

void MMAP_$AVAIL(uint32_t vpn)
{
    mmape_t *page = MMAPE_FOR_VPN(vpn);                     /* 0x00E0CC76 */

    if (page->wsl_index > WSL_INDEX_MAX) {                   /* 0x00E0CC84 */
        CRASH_SYSTEM(&mmap_$bad_avail_00e0ccb8);             /* 0x00E0CC90 */
    }

    /* 0x00E0CC98-0x00E0CCA8 */
    mmap_$add_to_wsl(page, vpn, (uint16_t)page->wsl_index, true);
}
