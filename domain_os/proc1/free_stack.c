/*
 * PROC1_$FREE_STACK - Free a process stack
 * Original address: 0x00e1511a (46 bytes)
 *
 * Returns a stack to the free list when it lies above STACK_HIGH_WATER
 * (i.e. it came from the downward-growing large-stack region).  Stacks at or
 * below the high-water mark are simply dropped.  The free-list link is the
 * longword just below the stack top.
 *
 * 0x00E1511A  link.w A6,-0x4 / pea (A5) / lea (0xe254e8).l,A5
 * 0x00E15126  move.l (0x8,A6),D0             ; argument 1: stack
 * 0x00E1512A  cmp.l (0xc3c,A5),D0            ; STACK_HIGH_WATER
 * 0x00E1512E  bls.b 0x00E15140               ; stack <= high water: nothing
 * 0x00E15130  subq.l #0x4,D0
 * 0x00E15132  move.l D0,(-0x4,A6)            ; local copy, never read
 * 0x00E15136  movea.l D0,A0
 * 0x00E15138  move.l (0xc38,A5),(A0)         ; *link = STACK_FREE_LIST
 * 0x00E1513C  move.l A0,(0xc38,A5)           ; STACK_FREE_LIST = link
 * 0x00E15140  movea.l (-0x8,A6),A5 / unlk A6 / rts
 *
 * Parameters:
 *   stack - top of the stack to free (the value PROC1_$ALLOC_STACK returned)
 */

#include "proc1/proc1_internal.h"

void PROC1_$FREE_STACK(void *stack)
{
    uint32_t va = ARCH_PTR_TO_VA(stack);
    uint32_t *link;

    /* 0x00E1512A: unsigned compare, bls = branch if stack <= high water */
    if (va > PROC1_$DATA.stack_high_water) {
        /* 0x00E15130..0x00E1513C */
        link = (uint32_t *)ARCH_VA_TO_PTR(va - 4);
        *link = PROC1_$DATA.stack_free_list;
        PROC1_$DATA.stack_free_list = va - 4;
    }
}
