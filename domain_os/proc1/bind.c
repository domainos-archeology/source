/*
 * PROC1_$BIND - Bind a new process to a free PCB
 * Original address: 0x00e14d1c (256 bytes)
 *
 * Re-emitted from the disassembly.  A5 = 0x00E254E8 (the PROC1_ data
 * block).  PCBS is the pointer table at 0xE1EACC (`movea.l #0xe1eacc,A0'),
 * indexed by pid; the search starts at slot 3 (`lea (0xc,A0),A0').
 *
 * Frame: (0x8,A6) entry, (0xC,A6) initial_sp, (0x10,A6) stack_base,
 * (0x14,A6) ws_param (word), (0x16,A6) status.
 * Registers: D2 = the pid returned, D3 = scaled pid, D4 = stack_base,
 * D5 = search pid, A2 = status then the PCB, A3 = scratch.
 *
 * 0x00E14D1C  link.w A6,-0x8 / movem.l D2-D5/A2/A3/A5,-(SP) / lea A5
 * 0x00E14D2A  D4 = stack_base; A2 = status
 * 0x00E14D2E  ML_$LOCK(0xB)                              (addq #4)
 * 0x00E14D40  A0 = &PCBS[3]; D5 = 3; bra 0x00E14D6E
 * 0x00E14D4E  D5++; A0 += 4; cmpi.w #0x40,D5 / bls 0x00E14D6E
 * 0x00E14D58  *status = 0x000A0008; ML_$UNLOCK(0xB); bra 0x00E14E10
 *             (result slot + word left for the unlk to discard)
 * 0x00E14D6E  A1 = *A0; btst.b #3,(0x55,A1) / bne 0x00E14D4E   bound: next
 * 0x00E14D78  D2 = D5; *status = 0; D3 = D5 << 2
 * 0x00E14D80  OS_STACK_BASE[D5] = D4                     (0x730,A5 + D5*4)
 * 0x00E14D88  PMAP_$INIT_WS_SCAN(D5, ws_param)           (addq #4)
 * 0x00E14D96  A2 = PCBS[D5]
 * 0x00E14DA2  A2->resource_locks_held = 0
 * 0x00E14DA6  longword (0x56,A2) = 0x00010010            inh_count=1, sw_bsr=0x10
 * 0x00E14DAE  copy 8 bytes from 0x00E14E1C into (0x4C,A2)..(0x53,A2)
 *             (`lea (0x6c,PC),A0' / moveq #7 / dbf: 8 iterations)
 * 0x00E14DBE  A2->asid = 0
 * 0x00E14DC2  D3 = D5 << 4; clear the four longwords at (0x828,A5 + D3)
 * 0x00E14DD8  word (0x54,A2) = 0x000A                    pri_min=0, pri_max=0x0A
 * 0x00E14DDE  A2->field_60 = 0; A2->field_64 = 0
 * 0x00E14DE6  ML_$UNLOCK(0xB)                            (addq #4)
 * 0x00E14DF4  INIT_STACK(A2, &entry, &initial_sp)        (lea (0xc,SP),SP)
 * 0x00E14E08  PROC1_$INIT_TS_TIMER(D5)                   (result slot, no cleanup)
 * 0x00E14E10  D0 = D2 / movem.l / unlk / rts
 *
 * Quirks reproduced:
 *   - on the "no PCB" path D2 is never written, so the function returns
 *     whatever the caller left in D2 (the register is callee-saved, so it
 *     is the caller's own D2);
 *   - pri_max is written as 0x0A: PROC1_FLAG_BOUND | PROC1_FLAG_SUSPENDED,
 *     the process is born suspended and PROC1_$RESUME starts it;
 *   - the 8-byte template copy covers cpu_total (0x4C), cpu_usage (0x50)
 *     and state (0x52): state becomes 0x0010.
 *
 * Parameters:
 *   entry      - initial PC of the new process
 *   initial_sp - initial SP of the new process
 *   stack_base - recorded in OS_STACK_BASE[pid]
 *   ws_param   - handed to PMAP_$INIT_WS_SCAN
 *   status_p   - status return
 *
 * Returns:
 *   the pid bound (D2)
 */

#include "proc1/proc1_internal.h"
#include "ml/ml.h"
#include "pmap/pmap.h"

/*
 * The 8-byte PCB template at 0x00E14E1C (`lea (0x6c,PC),A0' at 0x00E14DAE):
 * image bytes 00 00 00 00 00 00 00 10.  It is copied byte by byte into
 * PCB+0x4C..0x53, i.e. over cpu_total, cpu_usage and state.
 */
static const uint8_t proc1_$bind_pcb_template[8] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
};
_Static_assert(sizeof(proc1_$bind_pcb_template) == 8, "0x00E14E1C..0x00E14E24");

/* 0x00E14DA6: `move.l #0x10010,(0x56,A2)' */
#define PROC1_BIND_INH_COUNT   0x0001u
#define PROC1_BIND_SW_BSR      0x0010u

/* 0x00E14DD8: `move.w #0xa,(0x54,A2)' */
#define PROC1_BIND_PRI_MIN     0x00u
#define PROC1_BIND_PRI_MAX     (PROC1_FLAG_BOUND | PROC1_FLAG_SUSPENDED)

uint16_t PROC1_$BIND(void *entry, void *initial_sp, void *stack_base,
                     uint16_t ws_param, status_$t *status_p)
{
    /*
     * D2: not written until a slot is found, so the "no PCB" exit returns
     * whatever the caller held in D2; a defined zero stands in for that.
     */
    uint16_t pid_result = 0;
    uint16_t pid;               /* D5 */
    proc1_t *pcb;               /* A1, then A2 */
    proc1_$stats_t *stats;      /* A3 = A5 + pid*16, then A0 */

    /* 0x00E14D2E: ML_$LOCK(PROC1_CREATE_LOCK_ID) */
    ML_$LOCK(PROC1_CREATE_LOCK_ID);

    /* 0x00E14D40..0x00E14D56: scan slots 3..0x40 for an unbound PCB */
    pid = 3;
    for (;;) {
        /* 0x00E14D6E: btst.b #0x3,(0x55,A1) */
        pcb = PCBS[pid];
        if ((pcb->pri_max & PROC1_FLAG_BOUND) == 0) {
            break;
        }
        /* 0x00E14D4E..0x00E14D56: D5++ / cmpi.w #0x40,D5w / bls */
        pid++;
        if (pid > 0x40) {
            /* 0x00E14D58 */
            *status_p = status_$no_pcb_is_available;
            ML_$UNLOCK(PROC1_CREATE_LOCK_ID);
            /* 0x00E14D6A -> 0x00E14E10: D2 was never written */
            return pid_result;
        }
    }

    /* 0x00E14D78 */
    pid_result = pid;
    *status_p = status_$ok;

    /* 0x00E14D7C..0x00E14D84: OS_STACK_BASE[pid] = stack_base, a VA
     * (`move.l D4,(0x730,A1)' with A1 = A5 + pid*4) */
    PROC1_$DATA.os_stack_base[pid] = ARCH_PTR_TO_VA(stack_base);

    /* 0x00E14D88: PMAP_$INIT_WS_SCAN(pid, ws_param) */
    PMAP_$INIT_WS_SCAN(pid, (int16_t)ws_param);

    /* 0x00E14D96..0x00E14DA0: A2 = PCBS[pid] (re-read after the call) */
    pcb = PCBS[pid];

    /* 0x00E14DA2 */
    pcb->resource_locks_held = 0;

    /* 0x00E14DA6: one longword store over inh_count and sw_bsr */
    pcb->inh_count = PROC1_BIND_INH_COUNT;
    pcb->sw_bsr = PROC1_BIND_SW_BSR;

    /*
     * 0x00E14DAE..0x00E14DBA: 8-byte copy into PCB+0x4C..0x53.  The bytes
     * are assembled big-endian into the three fields they cover so the
     * result matches the image on any host byte order.
     */
    pcb->cpu_total = ((uint32_t)proc1_$bind_pcb_template[0] << 24) |
                     ((uint32_t)proc1_$bind_pcb_template[1] << 16) |
                     ((uint32_t)proc1_$bind_pcb_template[2] << 8) |
                     (uint32_t)proc1_$bind_pcb_template[3];
    pcb->cpu_usage = (uint16_t)(((uint16_t)proc1_$bind_pcb_template[4] << 8) |
                                proc1_$bind_pcb_template[5]);
    pcb->state     = (uint16_t)(((uint16_t)proc1_$bind_pcb_template[6] << 8) |
                                proc1_$bind_pcb_template[7]);

    /* 0x00E14DBE */
    pcb->asid = 0;

    /* 0x00E14DC2..0x00E14DD6: four longwords at A5 + 0x828 + pid*16 */
    stats = &PROC1_$DATA.stats[pid];
    stats->stat[0] = 0;
    stats->stat[1] = 0;
    stats->stat[2] = 0;
    stats->stat[3] = 0;

    /* 0x00E14DD8: word 0x000A over pri_min:pri_max */
    pcb->pri_min = PROC1_BIND_PRI_MIN;
    pcb->pri_max = PROC1_BIND_PRI_MAX;

    /* 0x00E14DDE / 0x00E14DE2 */
    pcb->field_60 = 0;
    pcb->field_64 = 0;

    /* 0x00E14DE6: ML_$UNLOCK(PROC1_CREATE_LOCK_ID) */
    ML_$UNLOCK(PROC1_CREATE_LOCK_ID);

    /* 0x00E14DF4..0x00E14DFE: INIT_STACK(pcb, &entry, &initial_sp) */
    INIT_STACK(pcb, &entry, &initial_sp);

    /* 0x00E14E08: PROC1_$INIT_TS_TIMER(pid) - a Pascal function whose
     * result slot is simply discarded */
    PROC1_$INIT_TS_TIMER(pid);

    /* 0x00E14E10 */
    return pid_result;
}
