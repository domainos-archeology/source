/*
 * EC_$WAIT - Wait for up to three level-1 eventcounts
 *
 * Hand-written assembly in the image (PROC1_ASM segment; no link frame,
 * register arguments to the body), reproduced here as the C-callable shape
 * because the body it branches into, PROC1_$EC_WAITN (0x00E2065A), lives in
 * the tree as proc1/ec_waitn.c with a C calling convention:
 *
 *   0x00E20610  movem.l {A4 A3},-(SP)
 *   0x00E20614  lea (0x10,SP),A0        ; &ecs.ec[1]
 *   0x00E20618  moveq #1,D0
 *   0x00E2061A  tst.l (A0)+ / beq       ; ecs.ec[1] == NULL -> 1
 *   0x00E2061E  moveq #2,D0
 *   0x00E20620  tst.l (A0)+ / beq       ; ecs.ec[2] == NULL -> 2
 *   0x00E20624  moveq #3,D0
 *   0x00E20626  lea (0xc,SP),A4         ; A4 = &ecs
 *   0x00E2062A  lea (0x18,SP),A3        ; A3 = &vals
 *   0x00E2062E  movea.l (-0x1b68,PC),A1 ; A1 = PROC1_$CURRENT_PCB (0xE1EAC8)
 *   0x00E20632  bsr.w 0x00E2065A        ; PROC1_$EC_WAITN, D0 = 1-based index
 *   0x00E20636  movem.l (SP)+,{A3 A4}
 *   0x00E2063A  subq.w #1,D0w           ; 0-based
 *   0x00E2063C  rts
 *
 * Both arrays arrive BY VALUE on the stack (24 bytes, see ec/ec.h); the
 * list is terminated by the first NULL in ecs.ec[1..2] - ecs.ec[0] is never
 * tested.
 *
 * TODO: emit as ec/sau2/wait.s (byte-identical) once PROC1_$EC_WAITN is
 * itself carried as proc1/sau2 assembly; until then a .s here would `bsr`
 * into a C function with the wrong convention.  Bead source-k3o7.
 *
 * Returns:
 *   0-based index of the satisfied eventcount.
 *
 * Original address: 0x00e20610 (map: EC_$WAIT, 0xE20610..0xE2063D)
 */

#include "ec/ec_internal.h"
#include "proc1/proc1.h"

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    int16_t num_ecs;
    int16_t result;

    /* 0x00E20614-0x00E20624: 1, 2 or 3 entries. */
    num_ecs = 1;
    if (ecs.ec[1] != NULL) {
        num_ecs = 2;
        if (ecs.ec[2] != NULL) {
            num_ecs = 3;
        }
    }

    /* 0x00E20626-0x00E20632: A1 = current PCB, A4 = &ecs, A3 = &vals,
     * D0 = count; the body returns a 1-based index. */
    result = (int16_t)PROC1_$EC_WAITN(PROC1_$CURRENT_PCB, ecs.ec, vals.val, num_ecs);

    /* 0x00E2063A: subq.w #1,D0w */
    return (int16_t)(result - 1);
}
