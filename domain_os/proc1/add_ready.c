/*
 * PROC1_$ADD_READY - Add process to ready list
 *
 * Public interface to add a process to the ready list.
 * Simply calls the internal insert function.
 *
 * Parameters:
 *   pcb - Process to add
 *
 * Original address: 0x00e20820
 */

#include "proc1.h"

void PROC1_$ADD_READY(proc1_t *pcb)
{
    proc1_$insert_into_ready_list(pcb);
}
