/*
 * PCHIST_$INTERRUPT - Record a PC sample from the clock interrupt
 *
 * Re-emitted from the image (0x00E1A1F6..0x00E1A208, 20 bytes).
 *
 *   00e1a1fa  pea (0xe,PC)              ; &0x00E1A20A            (arg 2)
 *   00e1a1fe  move.l (0x8,A6),-(SP)     ; pc_ptr                 (arg 1)
 *   00e1a202  bsr.w PCHIST_$COUNT       ; the 8 bytes are left for unlk
 *
 * The mode cell at 0x00E1A20A holds 00 01 (`gsk read 0x00e1a20a 4`): the
 * interrupt path asks PCHIST_$COUNT for mode 1 -- the per-process
 * trace-fault delivery -- not 0 as the old body had it.
 *
 * Callers: 0x00E1CFD0 and 0x00E1D02C (the clock interrupt handlers).
 *
 * Original address: 0x00e1a1f6
 */

#include "pchist/pchist_internal.h"

static const int16_t pchist_$interrupt_mode_00e1a20a = 1;

void PCHIST_$INTERRUPT(uint32_t *pc_ptr)
{
    PCHIST_$COUNT(pc_ptr, (int16_t *)&pchist_$interrupt_mode_00e1a20a);
}
