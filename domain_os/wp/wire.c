/*
 * wp/wire.c - WP_$WIRE implementation
 *
 * Wire a physical page.  Acquires the WP lock, wires the page, then
 * releases the lock.  Mirrors WP_$UNWIRE (0x00e07176) exactly.
 *
 * Original address: 0x00e071b0
 * Size: 58 bytes
 *
 * Assembly (0x00e071b0):
 *   link.w   A6,0x0
 *   pea      (A5)
 *   lea      (0xe1dc80).l,A5          ; WP module data base (unused here)
 *   subq.l   #0x2,SP                  ; ML_$LOCK result slot
 *   move.w   #0x14,-(SP)              ; resource id 0x14 = WP_LOCK_ID
 *   jsr      ML_$LOCK
 *   addq.w   #0x4,SP
 *   move.l   (0x8,A6),-(SP)           ; ppn, by value
 *   jsr      MMAP_$WIRE
 *   addq.w   #0x4,SP
 *   subq.l   #0x2,SP                  ; ML_$UNLOCK result slot
 *   move.w   #0x14,-(SP)
 *   jsr      ML_$UNLOCK
 *   movea.l  (-0x4,A6),A5
 *   unlk     A6
 *   rts
 *
 * Note that the trailing `addq.w #0x4,SP` for the ML_$UNLOCK call is folded
 * into the `unlk`: the stack is discarded wholesale on return.
 *
 * 0x00E071B0 - 0x00E071E8 (58 bytes).  Verified against the disassembly
 * 2026-09-27; faithful: MMAP_$WIRE((0x8,A6)) under ML lock 0x14, A5
 * saved/loaded with 0xE1DC80 and unused.
 */

#include "wp/wp_internal.h"
#include "mmap/mmap.h"

/*
 * WP_$WIRE - Wire a physical page
 *
 * Parameters:
 *   ppn - Physical page number to wire
 */
void WP_$WIRE(uint32_t ppn)
{
    ML_$LOCK(WP_LOCK_ID);
    MMAP_$WIRE(ppn);
    ML_$UNLOCK(WP_LOCK_ID);
}
