/*
 * DISK_$UNASSIGN_ALL - DISK_$UNASSIGN for volumes 1..10
 *
 * 0x00E6BE1C - 0x00E6BE48 (46 bytes).  Verified against the disassembly on
 * 2026-09-19; the earlier emission was faithful.  `moveq #0x9` / dbf with
 * the index word at (-0x6,A6) counting from 1 and the status cell at
 * (-0x4,A6) never examined.
 */

#include "disk/disk_internal.h"

void DISK_$UNASSIGN_ALL(void)
{
    uint16_t vol_idx;               /* (-0x6,A6) */
    status_$t status;               /* (-0x4,A6) */
    int16_t i;

    for (i = 0; i < 10; i++) {
        vol_idx = (uint16_t)(i + 1);
        DISK_$UNASSIGN(&vol_idx, &status);
    }
}
