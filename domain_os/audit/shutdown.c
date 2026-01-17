/*
 * shutdown.c - AUDIT_$SHUTDOWN
 *
 * Shuts down the audit subsystem by stopping logging.
 *
 * Original address: 0x00E70D1E
 *
 * Original assembly:
 *   00e70d1e    link.w A6,-0x4
 *   00e70d22    pea (A5)
 *   00e70d24    lea (0xe854d8).l,A5
 *   00e70d2a    pea (-0x4,A6)           ; push &status
 *   00e70d2e    bsr.b 0x00e70d38        ; call audit_$stop_logging
 *   00e70d30    movea.l (-0x8,A6),A5
 *   00e70d34    unlk A6
 *   00e70d36    rts
 */

#include "audit/audit_internal.h"

void AUDIT_$SHUTDOWN(void)
{
    status_$t status;

    /* Stop audit logging, ignore errors */
    audit_$stop_logging(&status);
}
