/*
 * AS_$GET_ADDR - Get address range for a region
 *
 * Reverse engineered from Domain/OS at address 0x00E583F0
 *
 * Returns the base address and size of the specified address
 * space region. The region boundaries are calculated from MST
 * segment configuration values.
 *
 * Each segment is 32KB (0x8000 bytes), so the segment number
 * is shifted left by 15 bits (0x8000 = 2^15) to get the address.
 */

#include "as/as.h"
#include "mst/mst.h"

/*
 * Segment size shift value
 * Each segment is 32KB = 0x8000 = 2^15 bytes
 */
#define SEGMENT_SHIFT  15

void AS_$GET_ADDR(as_$addr_range_t *addr_range, int16_t *region)
{
    uint32_t base;
    uint32_t size;

    /*
     * Switch on region identifier
     * Assembly uses a jump table at 0xe5840e
     */
    switch (*region) {

    case AS_REGION_PRIVATE_A:
        /*
         * Private A: starts at 0, size from MST_$PRIVATE_A_SIZE
         * Assembly:
         *   clr.l (A0)                          ; base = 0
         *   clr.l D0
         *   move.w (0x00e2445c).l,D0w           ; MST_$PRIVATE_A_SIZE
         *   lsl.l #0x8,D0
         *   lsl.l #0x7,D0                       ; shift by 15 total
         *   move.l D0,(0x4,A0)                  ; size
         */
        addr_range->base = 0;
        addr_range->size = (uint32_t)MST_$PRIVATE_A_SIZE << SEGMENT_SHIFT;
        break;

    case AS_REGION_GLOBAL_A:
        /*
         * Global A: starts at segment MST_$SEG_GLOBAL_A,
         * size from MST_$GLOBAL_A_SIZE
         * Assembly:
         *   clr.l D0
         *   move.w (0x00e24460).l,D0w           ; MST_$SEG_GLOBAL_A
         *   lsl.l #0x8,D0
         *   lsl.l #0x7,D0                       ; shift by 15
         *   move.l D0,(A0)                      ; base
         *   clr.l D1
         *   move.w (0x00e24462).l,D1w           ; MST_$GLOBAL_A_SIZE
         *   [falls through to size calculation]
         */
        addr_range->base = (uint32_t)MST_$SEG_GLOBAL_A << SEGMENT_SHIFT;
        addr_range->size = (uint32_t)MST_$GLOBAL_A_SIZE << SEGMENT_SHIFT;
        break;

    case AS_REGION_PRIVATE_B:
        /*
         * Private B: starts at segment MST_$SEG_PRIVATE_B,
         * fixed size of 0x40000 (256KB = 8 segments)
         * Assembly:
         *   clr.l D0
         *   move.w (0x00e24458).l,D0w           ; MST_$SEG_PRIVATE_B
         *   lsl.l #0x8,D0
         *   lsl.l #0x7,D0                       ; shift by 15
         *   move.l D0,(A0)                      ; base
         *   move.l #0x40000,(0x4,A0)            ; size = 256KB
         */
        addr_range->base = (uint32_t)MST_$SEG_PRIVATE_B << SEGMENT_SHIFT;
        addr_range->size = 0x40000;  /* Fixed 256KB for Private B */
        break;

    case AS_REGION_GLOBAL_B:
        /*
         * Global B: starts at segment MST_$SEG_GLOBAL_B,
         * size from MST_$GLOBAL_B_SIZE
         * Assembly:
         *   clr.l D0
         *   move.w (0x00e24452).l,D0w           ; MST_$SEG_GLOBAL_B
         *   lsl.l #0x8,D0
         *   lsl.l #0x7,D0                       ; shift by 15
         *   move.l D0,(A0)                      ; base
         *   clr.l D1
         *   move.w (0x00e2444a).l,D1w           ; MST_$GLOBAL_B_SIZE
         *   [falls through to size calculation]
         */
        addr_range->base = (uint32_t)MST_$SEG_GLOBAL_B << SEGMENT_SHIFT;
        addr_range->size = (uint32_t)MST_$GLOBAL_B_SIZE << SEGMENT_SHIFT;
        break;

    default:
        /*
         * Invalid region: return sentinel values
         * Assembly:
         *   move.l #0x7fffffff,(A0)             ; base = max positive
         *   clr.l (0x4,A0)                      ; size = 0
         */
        addr_range->base = 0x7FFFFFFF;
        addr_range->size = 0;
        break;
    }
}
