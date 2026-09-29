/*
 * PROC1_$GET_LIST - List the bound processes that have no address space
 * Original address: 0x00e15362 (150 bytes)
 *
 * Re-emitted from the disassembly.  A5 = 0x00E254E8 (the PROC1_ data block);
 * the only cell read is PROC1_$TYPE (0xC42,A5 + mypid*2).  PCBS is walked
 * from 0xE1EACC (`move.l #0xe1eacc,D5').
 *
 * Frame: (0x8,A6) count (pointer to a word), (0xC,A6) list (pointer to
 * 4-byte {pid, type} entries).  The compiler keeps four copies of the
 * running PCBS cursor (A0, A1, (-0xC,A6), (-0x10,A6)) - all advance by 4
 * together at 0x00E153DC..0x00E153E4; they are one index here.
 *
 * 0x00E15362  link.w A6,-0x10 / movem.l D2-D6/A2-A5,-(SP) / lea A5
 * 0x00E15370  D3 = count; D4 = list
 * 0x00E1537A  *count = 0
 * 0x00E1537C  D0 = 0x40; D1 = 0                     (dbf: 0x41 iterations,
 *                                                    slots 0..0x40)
 * 0x00E15398  tst.l PCBS[i] / beq next              NULL slot
 * 0x00E153A4  btst.b #3,(0x55,pcb) / beq next       not BOUND
 * 0x00E153B2  tst.w (0x46,pcb) / bne next           asid != 0
 * 0x00E153BA  (*count)++; D2 = *count << 2
 * 0x00E153C4  list[(D2-4)]      = pcb->mypid        (-0x4,A3,D2)
 * 0x00E153CC  D6 = pcb->mypid * 2
 * 0x00E153D6  list[(D2-2)]      = PROC1_$DATA.type[mypid] (-0x2,A3,D2)
 * 0x00E153DC  advance cursors; dbf D0,0x00E15396
 * 0x00E153EE  movem.l / unlk / rts
 *
 * The type is looked up with the PCB's own mypid, not the slot index, and
 * the entry index is the incremented count (1-based, hence the -4/-2).
 *
 * Parameters:
 *   count_ret - receives the number of entries written
 *   list_ret  - receives one {pid, type} entry per matching process
 */

#include "proc1/proc1_internal.h"

void PROC1_$GET_LIST(int16_t *count_ret, proc_list_entry_t *list_ret)
{
    proc1_t *pcb;
    int16_t d0;                 /* D0: dbf counter */
    uint16_t i;                 /* D1 / 4 */
    uint16_t count;             /* D2 before the shift */

    /* 0x00E1537A */
    *count_ret = 0;

    /* 0x00E1537C..0x00E153EA: moveq #0x40 / dbf => slots 0..0x40 */
    i = 0;
    for (d0 = 0x40; d0 >= 0; d0--, i++) {
        /* 0x00E15398 */
        pcb = PCBS[i];
        if (pcb == NULL) {
            continue;
        }

        /* 0x00E153A4: btst.b #0x3,(0x55,A3) */
        if ((pcb->pri_max & PROC1_FLAG_BOUND) == 0) {
            continue;
        }

        /* 0x00E153B2: tst.w (0x46,A4) */
        if (pcb->asid != 0) {
            continue;
        }

        /* 0x00E153BA / 0x00E153BC */
        (*count_ret)++;
        count = (uint16_t)*count_ret;

        /* 0x00E153C4: (-0x4,A3,D2*4) = pcb->mypid */
        list_ret[count - 1].pid = pcb->mypid;

        /* 0x00E153CC..0x00E153D6: (-0x2,A3,D2*4) = PROC1_$DATA.type[pcb->mypid] */
        list_ret[count - 1].type = PROC1_$DATA.type[pcb->mypid];
    }
}
