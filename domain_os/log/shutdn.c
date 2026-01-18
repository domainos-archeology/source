/*
 * LOG_$SHUTDN - Shutdown the logging subsystem
 *
 * Adds a shutdown entry to the log, unwires and unmaps the memory,
 * and unlocks the log file.
 *
 * Original address: 00e1758c
 * Original size: 120 bytes
 *
 * Assembly:
 *   00e1758c    link.w A6,-0x8
 *   00e17590    pea (A5)
 *   00e17592    lea (0xe2b280).l,A5
 *   00e17598    tst.l (0x14,A5)         ; check LOG_$LOGFILE_PTR
 *   00e1759c    beq.b 0x00e175fc        ; skip if not initialized
 *   00e175a8    bsr.w LOG_$ADD          ; add shutdown entry
 *   00e175bc    jsr WP_$UNWIRE
 *   00e175dc    jsr MST_$UNMAP_PRIVI
 *   00e175f0    jsr FILE_$UNLOCK
 *   00e175f6    clr.l (0x00e0000c).l    ; clear early log magic
 */

#include "log/log_internal.h"
#include "file/file.h"
#include "wp/wp.h"
#include "mst/mst.h"

/* Shutdown message data at 0x00e17608 */
static const char shutdown_msg[] = "";  /* Empty message for shutdown entry */

/* Reference to early log extended magic at 0x00e0000c */
extern early_log_extended_t EARLY_LOG_EXTENDED;

void LOG_$SHUTDN(void)
{
    status_$t status;
    int32_t saved_ptr;

    /* Only proceed if log was initialized */
    if (LOG_$LOGFILE_PTR != NULL) {
        /* Add shutdown log entry */
        LOG_$ADD(LOG_TYPE_SHUTDOWN, (void *)shutdown_msg, 0);

        /* Save and clear the log pointer */
        saved_ptr = (int32_t)LOG_$LOGFILE_PTR;
        LOG_$LOGFILE_PTR = NULL;

        /* Unwire the log buffer page */
        WP_$UNWIRE(LOG_$STATE.wired_handle);

        /* Unmap the log file */
        MST_$UNMAP_PRIVI(1, &UID_$NIL, saved_ptr, LOG_BUFFER_SIZE, 0, &status);

        /* Unlock the log file */
        {
            uint16_t lock_mode = 0;
            FILE_$UNLOCK(&LOG_$LOGFILE_UID, &lock_mode, &status);
        }

        /* Clear early log magic */
        EARLY_LOG_EXTENDED.magic = 0;
    }
}
