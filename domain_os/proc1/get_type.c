/*
 * PROC1_$GET_TYPE - Get process type
 * Original address: 0x00e15324 (62 bytes)
 *
 * A Pascal procedure with a var result, not a function:
 *
 * 0x00E15324  link.w A6,0x0 / movem.l {A5 D2},-(SP) / lea (0xe254e8).l,A5
 * 0x00E15332  move.w (0x8,A6),D2w            ; argument 1: pid (word)
 * 0x00E15336  beq.b 0x00E1533E               ; pid == 0 -> crash
 * 0x00E15338  cmpi.w #0x40,D2w / bls.b 0x00E15348
 * 0x00E1533E  pea (-0x60,PC)                 ; 0x00E152E0 Illegal_process_id_err
 * 0x00E15342  jsr CRASH_SYSTEM               ; (never pops; falls through)
 * 0x00E15348  move.w D2w,D0w
 * 0x00E1534A  movea.l (0xa,A6),A1            ; argument 2: type_ret
 * 0x00E1534E  add.w D0w,D0w
 * 0x00E15350  lea (0x0,A5,D0w*0x1),A0
 * 0x00E15354  move.w (0xc42,A0),(A1)         ; PROC1_$TYPE[pid] (A5 + 0xC42 + pid*2)
 * 0x00E15358  movem.l (-0x8,A6),{D2 A5} / unlk A6 / rts
 *
 * No caller in the image (Ghidra: "No references found").
 *
 * Parameters:
 *   pid      - process id, 1..64
 *   type_ret - receives PROC1_$TYPE[pid]
 */

#include "proc1/proc1_internal.h"
#include "misc/misc.h"

void PROC1_$GET_TYPE(uint16_t pid, uint16_t *type_ret)
{
    /* 0x00E15336..0x00E15342: the check is unsigned (bls after cmpi #0x40) */
    if (pid == 0 || pid > 0x40) {
        CRASH_SYSTEM(&Illegal_process_id_err);
        /* the image continues here if CRASH_SYSTEM ever returned */
    }

    /* 0x00E15348..0x00E15354 */
    *type_ret = PROC1_$TYPE[pid];
}
