/*
 * proc1_$remove_from_ready_list - Remove process from ready list
 *
 * Unlinks the PCB from the doubly-linked circular ready list
 * and decrements the ready count.
 *
 * Parameters:
 *   pcb - Process to remove (passed in A1 on m68k)
 *
 * Original address: 0x00e206d6
 */

#include "proc1.h"

void proc1_$remove_from_ready_list(proc1_t *pcb)
{
    proc1_t *next = pcb->nextp;
    proc1_t *prev = pcb->prevp;

    /* Unlink from list */
    prev->nextp = next;
    next->prevp = prev;

    PROC1_$READY_COUNT--;
}
