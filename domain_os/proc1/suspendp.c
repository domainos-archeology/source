/*
 * PROC1_$SUSPENDP - Is a process suspended?
 * Original address: 0x00e14876 (84 bytes)
 *
 * Frame: (0x8,A6) pid (word), (0xA,A6) status.  Pascal boolean function.
 *
 * 0x00E14876  link.w A6,-0x8 / movem.l D2/A2,-(SP)
 * 0x00E1487E  D1 = pid; A0 = status
 * 0x00E14886  st D0b                                  result TRUE by default
 * 0x00E14888  beq / cmpi.w #0x40 / bls
 * 0x00E14890  *status = 0x000A0001; bra exit          (result stays TRUE)
 * 0x00E14898  A1 = PCBS[pid]
 * 0x00E148A6  btst.b #3,(0x55,A1) / bne
 * 0x00E148AE  *status = 0x000A0005; bra exit          (result stays TRUE)
 * 0x00E148B6  btst.b #1,(0x55,A1) / sne D0b           SUSPENDED?
 * 0x00E148BE  *status = 0
 * 0x00E148C0  movem.l / unlk / rts
 *
 * Both failure paths return TRUE (0xFF): PROC1_$UNBIND's wait loop
 * (0x00E14EF0) treats that as "stop waiting".
 *
 * Parameters:
 *   pid        - the process
 *   status_ret - status return
 *
 * Returns:
 *   Domain boolean: 0xFF if suspended (or on any error), 0 otherwise
 */

#include "proc1/proc1_internal.h"

int8_t PROC1_$SUSPENDP(uint16_t pid, status_$t *status_ret)
{
    proc1_t *pcb;               /* A1 */
    int8_t result;              /* D0.b */

    /* 0x00E14886: st D0b */
    result = -1;

    /* 0x00E14888 / 0x00E1488A */
    if (pid == 0 || pid > 0x40) {
        /* 0x00E14890 */
        *status_ret = status_$illegal_process_id;
        return result;
    }

    /* 0x00E14898..0x00E148A2 */
    pcb = PCBS[pid];

    /* 0x00E148A6: btst.b #0x3,(0x55,A1) */
    if ((pcb->pri_max & PROC1_FLAG_BOUND) == 0) {
        /* 0x00E148AE */
        *status_ret = status_$process_not_bound;
        return result;
    }

    /* 0x00E148B6 / 0x00E148BC: btst.b #0x1,(0x55,A1) / sne */
    result = (pcb->pri_max & PROC1_FLAG_SUSPENDED) ? -1 : 0;

    /* 0x00E148BE */
    *status_ret = status_$ok;
    return result;
}
