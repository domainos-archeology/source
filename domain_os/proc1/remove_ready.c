/*
 * PROC1_$REMOVE_READY - Remove process from ready list
 *
 * Public interface to remove a process from the ready list.
 * Simply calls the internal remove function.
 *
 * Parameters:
 *   pcb - Process to remove
 *
 * Original address: 0x00e206d2
 */

#include "proc1.h"

void PROC1_$REMOVE_READY(proc1_t *pcb)
{
    proc1_$remove_from_ready_list(pcb);
}
