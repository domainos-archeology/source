/*
 * flp/revalidate.c - FLP_$REVALIDATE (0x00E3DC54, 36 bytes)
 *
 * Jump-table entry +0x0C.  Clears the disk-change flag of the volume's
 * unit so I/O may proceed after a new disk was noticed.  Saves and
 * restores A5 around the module base (`pea (A5)` / `movea.l (-0x4,A6),A5`).
 */

#include "flp/flp_internal.h"

void FLP_$REVALIDATE(disk_$volume_t *vol)
{
    /* 0x00E3DC60-0x00E3DC6C: `move.w (0x1c,A0),D0w` is the volume's device
     * unit; `clr.b (0x124,A1)` its disk_change byte.  No bounds check. */
    FLP_DATA.disk_change[vol->dev_unit] = 0;
}
