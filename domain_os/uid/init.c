/*
 * UID_$INIT - Initialize the UID generator
 *
 * Sets the node ID portion of the UID generator state from NODE_$ME.
 * The low word of the generator state encodes:
 *   - Bits 0-19: Node ID (from NODE_$ME)
 *   - Bits 20-31: Counter (12 bits, preserved during init)
 *
 * Must be called after NODE_$ME is set during system initialization.
 *
 * Original address: 0x00e30950
 *
 * Assembly:
 *   00e30950    link.w A6,0x0
 *   00e30954    movea.l #0xe2c008,A0
 *   00e3095a    andi.l #-0x100000,(0x4,A0)    ; Clear bits 0-19
 *   00e30962    move.l (0x00e245a4).l,D0      ; Load NODE_$ME
 *   00e30968    or.l D0,(0x4,A0)              ; OR into low word
 *   00e3096c    unlk A6
 *   00e3096e    rts
 */

#include "uid.h"

void UID_$INIT(void)
{
    /*
     * Clear the node ID bits (0-19) and set them from NODE_$ME.
     * The counter bits (20-31) are preserved.
     *
     * Mask 0xFFF00000 = ~0x000FFFFF preserves upper 12 bits (counter)
     */
    UID_$GENERATOR_STATE.low = (UID_$GENERATOR_STATE.low & 0xFFF00000) | NODE_$ME;
}
