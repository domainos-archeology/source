/*
 * misc/set_lites_loc.c - SET_LITES_LOC
 *
 * Original address: 0x00E0C4DC
 * Size: 54 bytes (0x00E0C4DC .. 0x00E0C511)
 *
 * Stores a new display-memory address for the status lights.  When the
 * lights were disabled (LITES_LOC == 0) and the new address is non-zero,
 * the MEM_LITES process is started after the store.
 *
 * Frame (link.w A6,-0x4; A5 saved and set to 0xE2327C = &LITES_LOC):
 *   (0x8,A6)   loc_p     pointer to the new address
 *   (-0x4,A6)  new_loc   its value
 *
 *   0x00E0C4F0  tst.l (A5) / bne.b 0x00e0c506        already enabled -> plain store
 *   0x00E0C4F4  movea.l (-0x4,A6),A1 / cmpa.w #0,A1  new address zero -> plain store
 *   0x00E0C4FE  move.l A1,(A5) / bsr START_MEM_LITES
 *   0x00E0C506  move.l (-0x4,A6),(A5)
 *
 * Verified against the disassembly 2026-09-27; the body was equivalent
 * and is restated in the image's branch order.
 */

#include "misc/misc_internal.h"

void SET_LITES_LOC(int32_t *loc_p)
{
    int32_t new_loc;                /* (-0x4,A6) */

    /* 0x00E0C4E8 .. 0x00E0C4EC */
    new_loc = *loc_p;

    /* 0x00E0C4F0 .. 0x00E0C4FC */
    if (LITES_LOC == 0 && new_loc != 0) {
        LITES_LOC = new_loc;                            /* 0x00E0C4FE */
        START_MEM_LITES();                              /* 0x00E0C500 */
        return;
    }

    LITES_LOC = new_loc;                                /* 0x00E0C506 */
}
