/*
 * PROC2_$QUIT - Send SIGQUIT to process
 *
 * Wrapper around PROC2_$SIGNAL that sends SIGQUIT (signal 3).
 *
 * Parameters:
 *   proc_uid - UID of process to signal
 *   status_ret - Status return
 *
 * Original address: 0x00e3f130
 */

#include "proc2.h"

/* Constants from original code at PC-relative addresses */
static int16_t quit_signal = SIGQUIT;  /* 3 */
static uint32_t quit_param = 0;

void PROC2_$QUIT(uid_t *proc_uid, status_$t *status_ret)
{
    PROC2_$SIGNAL(proc_uid, &quit_signal, &quit_param, status_ret);
}
