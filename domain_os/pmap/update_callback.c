/*
 * PMAP_$UPDATE_CALLBACK - Timer callback that runs AST_$UPDATE
 *
 * 0x00E143B2 - 0x00E143CA (26 bytes).  Verified against the disassembly
 * 2026-09-27: saves A5, loads A5 = 0xE24D44 (unused by the body), calls
 * AST_$UPDATE (0x00E016D0) with no arguments, restores A5.  The timer
 * element is entered by PMAP_$INIT_TIMERS with no callback argument used.
 */

#include "pmap/pmap_internal.h"
#include "ast/ast.h"

void PMAP_$UPDATE_CALLBACK(void)
{
    AST_$UPDATE();                                      /* 0x00E143BE */
}
