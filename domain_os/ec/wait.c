/*
 * EC_$WAIT - Wait for eventcount(s) to reach value(s)
 *
 * Waits on up to 3 eventcounts.  Both arrays arrive by value on the
 * stack (see ec/ec.h); the list is terminated by the first NULL in
 * ecs.ec[1..2].
 *
 * Assembly (0x00e20610, 46 bytes):
 *   movem.l {A4 A3},-(SP)
 *   lea (0x10,SP),A0        ; &ecs.ec[1]
 *   moveq #1,D0
 *   tst.l (A0)+ ; beq       ; ecs.ec[1] == NULL -> 1
 *   moveq #2,D0
 *   tst.l (A0)+ ; beq       ; ecs.ec[2] == NULL -> 2
 *   moveq #3,D0
 *   lea (0xc,SP),A4         ; A4 = &ecs
 *   lea (0x18,SP),A3        ; A3 = &vals
 *   movea.l PROC1_$CURRENT_PCB,A1
 *   bsr.w PROC1_$EC_WAITN   ; D0 = 1-based index
 *   movem.l (SP)+,{A3 A4}
 *   subq.w #1,D0            ; 0-based
 *   rts
 *
 * Returns:
 *   0-based index of the satisfied eventcount
 *
 * Original address: 0x00e20610
 */

#include "ec/ec_internal.h"
#include "proc1/proc1.h"

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    int16_t num_ecs;
    int16_t result;

    /* Count number of eventcounts (1, 2, or 3) */
    num_ecs = 1;
    if (ecs.ec[1] != NULL) {
        num_ecs = 2;
        if (ecs.ec[2] != NULL) {
            num_ecs = 3;
        }
    }

    /* Call the N-wait function (A1 = current PCB, A4 = &ecs, A3 = &vals) */
    result = PROC1_$EC_WAITN(PROC1_$CURRENT_PCB, ecs.ec, vals.val, num_ecs);

    return result - 1;
}
