/*
 * DISK_$REVALID - Revalidate a volume after a media change
 *
 * Dispatches to the driver's revalidate entry, if it has one.
 *
 * Original address: 0x00E3DB74
 * Size: 40 bytes
 *
 * Assembly (0x00E3DB74):
 *   link.w   A6,-0x8
 *   movem.l  {A2 D2},-(SP)
 *   movea.l  (0x8,A6),A2        ; A2 = vol, the volume descriptor
 *   movea.l  (0x18,A2),A1       ; A1 = vol->dev_info
 *   movea.l  (A1),A0            ; A0 = *dev_info, the driver entry vector
 *   move.l   (0xc,A0),D2        ; D2 = vector->revalidate
 *   beq.b    0x00e3db92         ; no entry: nothing to do
 *   pea      (A2)               ; the driver gets the same descriptor
 *   movea.l  D2,A1
 *   jsr      (A1)
 *   movem.l  (-0x10,A6),{D2 A2}
 *   unlk     A6
 *   rts
 *
 * Note that the argument is the descriptor address, not a volume index, and
 * that the driver call's argument is never popped -- the unlk discards it.
 */

#include "disk/disk_internal.h"
#include "arch/arch.h"

/* The driver entry DISK_$REVALID jumps to. */
typedef void (*disk_$revalidate_fn_t)(disk_$volume_t *vol);

void DISK_$REVALID(disk_$volume_t *vol)
{
    disk_$dev_ops_t *ops;
    disk_$revalidate_fn_t revalidate;

    ops = (disk_$dev_ops_t *)ARCH_VA_TO_PTR(*(uint32_t *)vol->dev_info);
    revalidate = (disk_$revalidate_fn_t)ARCH_VA_TO_PTR(ops->revalidate);

    if (revalidate != NULL) {
        revalidate(vol);
    }
}
