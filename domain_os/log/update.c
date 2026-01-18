/*
 * LOG_$UPDATE - Check and return log update status
 *
 * Returns the wired page handle if the log has been modified
 * since the last update call, otherwise returns 0. Also clears
 * the early log extended magic.
 *
 * Original address: 00e1760c
 * Original size: 50 bytes
 *
 * Assembly:
 *   00e1760c    link.w A6,-0x4
 *   00e17610    pea (A5)
 *   00e17612    lea (0xe2b280).l,A5
 *   00e17618    tst.l (0x14,A5)         ; check LOG_$LOGFILE_PTR
 *   00e1761c    beq.b 0x00e1762e        ; return 0 if not init
 *   00e1761e    tst.b (0x18,A5)         ; check dirty_flag
 *   00e17622    bpl.b 0x00e1762e        ; return 0 if not dirty
 *   00e17624    move.l (0x10,A5),D0     ; return wired_handle
 *   00e17628    clr.b (0x18,A5)         ; clear dirty_flag
 *   00e1762e    clr.l D0                ; return 0
 *   00e17630    clr.l (0x00e0000c).l    ; clear early log magic
 */

#include "log/log_internal.h"

/* Reference to early log extended magic at 0x00e0000c */
extern early_log_extended_t EARLY_LOG_EXTENDED;

uint32_t LOG_$UPDATE(void)
{
    uint32_t result;

    if (LOG_$LOGFILE_PTR == NULL || LOG_$STATE.dirty_flag >= 0) {
        /* Not initialized or not dirty */
        result = 0;
    } else {
        /* Return wired handle and clear dirty flag */
        result = LOG_$STATE.wired_handle;
        LOG_$STATE.dirty_flag = 0;
    }

    /* Clear early log magic */
    EARLY_LOG_EXTENDED.magic = 0;

    return result;
}
