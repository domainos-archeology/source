/*
 * PROC1_$SET_TYPE - Record a process's type
 * Original address: 0x00e152e4 (64 bytes)
 *
 * A5 = 0x00E254E8; PROC1_$TYPE[pid] is (0xC42,A5 + pid*2).
 *
 * 0x00E152E4  link.w A6,0x0 / movem.l D2/D3/A5,-(SP) / lea A5
 * 0x00E152F2  D2 = pid (0x8,A6); D3 = type (0xA,A6)
 * 0x00E152FA  tst.w D2 / beq crash; cmpi.w #0x40 / bls ok
 * 0x00E15304  CRASH_SYSTEM(&Illegal_process_id_err)  (`pea (-0x26,PC)' ->
 *             0x00E152E0, bytes 00 0a 00 01; no cleanup, and the code
 *             FALLS THROUGH into the store afterwards)
 * 0x00E1530E  PROC1_$TYPE[pid] = type
 * 0x00E1531A  movem.l / unlk / rts
 *
 * Parameters:
 *   pid  - the process (1..0x40; anything else crashes the system)
 *   type - the type word PROC1_$GET_TYPE / PROC1_$GET_LIST report
 */

#include "proc1/proc1_internal.h"
#include "misc/misc.h"

void PROC1_$SET_TYPE(uint16_t pid, uint16_t type)
{
    /* 0x00E152FA / 0x00E152FE */
    if (pid == 0 || pid > 0x40) {
        /* 0x00E15304: no return after the crash call in the image */
        CRASH_SYSTEM(&Illegal_process_id_err);
    }

    /* 0x00E1530E..0x00E15316 */
    PROC1_$TYPE[pid] = type;
}
