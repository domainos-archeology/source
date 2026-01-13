/*
 * PROC2_$GET_ASID - Get address space ID for process
 *
 * Wrapper around PROC2_$FIND_ASID with a constant parameter.
 *
 * Parameters:
 *   proc_uid - UID of process
 *   status_ret - Status return
 *
 * Original address: 0x00e40702
 */

#include "proc2.h"

/* Constant parameter passed to FIND_ASID (at address 0xE3E952) */
static int8_t asid_param = 0;  /* TODO: Determine actual value */

void PROC2_$GET_ASID(uid_$t *proc_uid, status_$t *status_ret)
{
    PROC2_$FIND_ASID(proc_uid, &asid_param, status_ret);
}
