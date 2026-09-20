/*
 * PROC1_$INIT - Initialise the level-1 process manager at boot
 * Original address: 0x00e2f958 (206 bytes)
 *
 * Re-emitted from the disassembly.  This lives in the boot-time PROC1_
 * segment (SAU2 map: I E2F958 PROC1_ size D0); it reaches the PROC1_ data
 * block with `movea.l #0xe254e8,A0' rather than through A5.
 *
 * 0x00E2F958  link.w A6,-0x4
 * 0x00E2F95C  A0 = 0xE254E8
 * 0x00E2F962  STACK_LOW_WATER  (0xC40,A0) = 0x00D00000
 * 0x00E2F96A  STACK_HIGH_WATER (0xC3C,A0) = 0x00D50000
 * 0x00E2F972  STACK_FREE_LIST  (0xC38,A0) = 0
 * 0x00E2F976  OS_STACK_BASE[1] (0x734,A0) = 0x00EB2000
 * 0x00E2F97E  PROC1_$SET_TYPE(2, 3)               (`move.l #0x20003': pid 2,
 *                                                  type 3; addq #4)
 * 0x00E2F98C  A0 = (0x00E1EAD0) = PCBS[1]         (0xE1EACC + 4)
 * 0x00E2F992  pcb->state = 0x10
 * 0x00E2F998  longword (0x56,A0) = 0x00010010     inh_count 1, sw_bsr 0x10
 * 0x00E2F9A0  pcb->resource_locks_held = 0
 * 0x00E2F9A4  ori #0x700,SR                       raised, never lowered
 *                                                  here (PROC1_$DISPATCH
 *                                                  forces IPL 0)
 * 0x00E2F9A8  D0 = (0x54,A0) & 0xB; cmpi.w #8 / bne 0x00E2F9BE
 * 0x00E2F9B4  PROC1_$REORDER_READY(pcb)           (`pea (A0)')
 * 0x00E2F9BC  bra 0x00E2F9CC
 * 0x00E2F9BE  word (0x54,A0) = 0x0008             pri_min 0, pri_max BOUND
 * 0x00E2F9C4  PROC1_$ADD_READY(pcb)               (`pea (A0)')
 * 0x00E2F9CC  addq #4 (shared by both arms)
 * 0x00E2F9CE  PROC1_$CURRENT_PCB (0xE1EAC8) = PCBS[1]
 * 0x00E2F9D8  PROC1_$CURRENT (0xE20608) = PROC1_$CURRENT_PCB->mypid
 * 0x00E2F9E6  PROC1_$INIT_TS_TIMER(2)             (result slot; addq #4)
 * 0x00E2F9F4  PROC1_$INIT_TS_TIMER(1)             (result slot; addq #4)
 * 0x00E2FA02  PROC1_$DISPATCH()
 * 0x00E2FA08  PMAP_$INIT_WS_SCAN(2, 5)            (`move.l #0x20005'; addq #4)
 * 0x00E2FA16  PMAP_$INIT_WS_SCAN(1, 7)            (`move.l #0x10007'; no
 *                                                  cleanup: unlk)
 * 0x00E2FA22  unlk A6 / rts
 *
 * The PCB set up and made current is PCBS[1] (the boot process); the
 * SET_TYPE, TS_TIMER and WS_SCAN calls also touch pid 2.  The word at
 * 0x54 is tested against BOUND|SUSPENDED|WAITING (0xB) == BOUND: a PCB
 * that is already on the ready list is reordered, otherwise its flags are
 * replaced wholesale with BOUND and it is added.
 */

#include "proc1/proc1_internal.h"
#include "pmap/pmap.h"

/* 0x00E2F962 / 0x00E2F96A / 0x00E2F976: the boot-time region constants */
#define PROC1_INIT_STACK_LOW    0x00D00000u
#define PROC1_INIT_STACK_HIGH   0x00D50000u
#define PROC1_INIT_OS_STACK_1   0x00EB2000u

void PROC1_$INIT(void)
{
    proc1_t *pcb;
    uint16_t flags;

    /* 0x00E2F962..0x00E2F976 */
    STACK_LOW_WATER = ARCH_VA_TO_PTR(PROC1_INIT_STACK_LOW);
    STACK_HIGH_WATER = ARCH_VA_TO_PTR(PROC1_INIT_STACK_HIGH);
    STACK_FREE_LIST = NULL;
    OS_STACK_BASE[1] = ARCH_VA_TO_PTR(PROC1_INIT_OS_STACK_1);

    /* 0x00E2F97E: PROC1_$SET_TYPE(2, 3) */
    PROC1_$SET_TYPE(2, 3);

    /* 0x00E2F98C: the longword at 0xE1EAD0 is PCBS[1] */
    pcb = PCBS[1];

    /* 0x00E2F992 */
    pcb->state = 0x10;

    /* 0x00E2F998: one longword over inh_count and sw_bsr */
    pcb->inh_count = 0x0001;
    pcb->sw_bsr = 0x0010;

    /* 0x00E2F9A0 */
    pcb->resource_locks_held = 0;

    /* 0x00E2F9A4: ori #0x700,SR - nothing saved, nothing restored here */
    SET_IPL7();

    /* 0x00E2F9A8..0x00E2F9B2: (pri_min:pri_max & 0xB) == 0x8 */
    flags = (uint16_t)(((uint16_t)pcb->pri_min << 8) | pcb->pri_max);
    if ((flags & 0x000B) == 0x0008) {
        /* 0x00E2F9B4 */
        PROC1_$REORDER_READY(pcb);
    } else {
        /* 0x00E2F9BE: word 0x0008 over pri_min:pri_max */
        pcb->pri_min = 0;
        pcb->pri_max = PROC1_FLAG_BOUND;

        /* 0x00E2F9C4 */
        PROC1_$ADD_READY(pcb);
    }

    /* 0x00E2F9CE / 0x00E2F9D8 */
    PROC1_$CURRENT_PCB = PCBS[1];
    PROC1_$CURRENT = PROC1_$CURRENT_PCB->mypid;

    /* 0x00E2F9E6 / 0x00E2F9F4 */
    PROC1_$INIT_TS_TIMER(2);
    PROC1_$INIT_TS_TIMER(1);

    /* 0x00E2FA02 */
    PROC1_$DISPATCH();

    /* 0x00E2FA08 / 0x00E2FA16 */
    PMAP_$INIT_WS_SCAN(2, 5);
    PMAP_$INIT_WS_SCAN(1, 7);
}
