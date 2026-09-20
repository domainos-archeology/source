/*
 * PROC2_$GET_CPU_USAGE - Get CPU usage for the current process
 *
 * Asks PROC1 for the CPU time and two PCB statistics, copies the 20-byte
 * frame area holding them to *usage, then overwrites the last longword
 * with the constant 0x411C.
 *
 * Parameters:
 *   usage - (0x8,A6) A2: 20-byte output record
 *
 * Original address: 0x00e41d2a (74 bytes)
 * A5 = 0xE7BE84 (PROC2 module data), not otherwise used.
 *   0x00E41D3C..0x00E41D4E  PROC1_$GET_CPU_USAGE(&(-0x18,A6), &(-0xC,A6), &(-0x10,A6))
 *                           (pushes right-to-left: arg1 = -0x18, arg2 = -0xC, arg3 = -0x10)
 *   0x00E41D58..0x00E41D5E  moveq #4 / dbf: five longwords (-0x18..-0x5,A6) -> usage
 *   0x00E41D62              move.l #0x411c,(0x10,A2)
 *
 * Frame layout (-0x18,A6) .. (-0x5,A6):
 *   -0x18  6-byte CPU time from PROC1 (the low word of the -0x14 longword is
 *          the tail of it; its high word is never written by PROC1)
 *   -0x10  stat2 (third argument)
 *   -0x0C  stat1 (second argument)
 *   -0x08  never written -- copied as-is, then replaced by 0x411C
 */

#include "proc2/proc2_internal.h"

void PROC2_$GET_CPU_USAGE(uint32_t *usage)
{
    uint32_t frame[5];        /* (-0x18,A6) .. (-0x5,A6) */
    int16_t i;

    /* arg1 = frame[0..] (6-byte time), arg2 = frame[3], arg3 = frame[2] */
    PROC1_$GET_CPU_USAGE(&frame[0], &frame[3], &frame[2]);

    /* moveq #0x4 / dbf: 5 iterations */
    for (i = 4; i >= 0; i--) {
        usage[4 - i] = frame[4 - i];
    }

    /* 0x00E41D62 */
    usage[4] = 0x411C;
}
