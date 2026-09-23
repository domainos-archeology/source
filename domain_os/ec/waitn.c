/*
 * EC_$WAITN - Wait for N level-1 eventcounts
 *
 * Hand-written assembly in the image (PROC1_ASM segment; no link frame,
 * register arguments to the body), reproduced here as the C-callable shape
 * because the body it branches into, PROC1_$EC_WAITN (0x00E2065A), lives in
 * the tree as proc1/ec_waitn.c with a C calling convention:
 *
 *   0x00E2063E  move.l A3,-(SP)
 *   0x00E20640  move.l A4,-(SP)
 *   0x00E20642  movea.l (-0x1b7c,PC),A1 ; A1 = PROC1_$CURRENT_PCB (0xE1EAC8)
 *   0x00E20646  movea.l (0xc,SP),A4     ; A4 = ecs      (argument 1)
 *   0x00E2064A  movea.l (0x10,SP),A3    ; A3 = wait_val (argument 2)
 *   0x00E2064E  move.w (0x14,SP),D0w    ; D0 = num_ecs  (argument 3, word)
 *   0x00E20652  bsr.b 0x00E2065A        ; PROC1_$EC_WAITN, D0 = 1-based index
 *   0x00E20654  movea.l (SP)+,A4
 *   0x00E20656  movea.l (SP)+,A3
 *   0x00E20658  rts
 *
 * Unlike EC_$WAIT there is no `subq.w #1,D0w`: the index comes back
 * 1-based, which is why EC2_$WAIT (0x00E4262A) tests `cmpi.w #0x2,D0w` for
 * its second list entry.
 *
 * TODO: emit as ec/sau2/waitn.s (byte-identical) once PROC1_$EC_WAITN is
 * itself carried as proc1/sau2 assembly.  Bead source-k3o7.
 *
 * Parameters:
 *   ecs      - Array of eventcount pointers
 *   wait_val - Array of wait values
 *   num_ecs  - Number of eventcounts to wait on (word)
 *
 * Returns:
 *   1-based index of the satisfied eventcount.
 *
 * Original address: 0x00e2063e (map: EC_$WAITN, 0xE2063E..0xE206D1)
 */

#include "ec/ec_internal.h"
#include "proc1/proc1.h"

uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *wait_val, int16_t num_ecs)
{
    /* 0x00E20642-0x00E20652 */
    return PROC1_$EC_WAITN(PROC1_$CURRENT_PCB, ecs, wait_val, num_ecs);
}
