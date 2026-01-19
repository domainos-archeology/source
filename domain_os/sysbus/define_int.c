/*
 * SYSBUS_$DEFINE_INT - Define an interrupt (stub/error handler)
 *
 * This function crashes the system with "Unknown Interrupt ID" error.
 * It appears to be a placeholder for an unimplemented feature that
 * was intended to allow dynamic interrupt definition.
 *
 * From: 0x00e0abbc
 *
 * Original assembly:
 *   00e0abbc    link.w A6,0x0
 *   00e0abc0    pea (0xe,PC)               ; &Sysbus_Unknown_Interrupt_ID_Err
 *   00e0abc4    jsr 0x00e1e700.l           ; CRASH_SYSTEM
 *   00e0abca    unlk A6
 *   00e0abcc    rts
 *
 * The error string "Sysbus_Unknown_Interrupt_ID_Err" is located at
 * PC + 0xe = 0x00e0abce after the function.
 */

#include "sysbus/sysbus_internal.h"

/* Error status for unknown interrupt ID */
const status_$t Sysbus_Unknown_Interrupt_ID_Err = 0x00080032;

void SYSBUS_$DEFINE_INT(void)
{
    CRASH_SYSTEM(&Sysbus_Unknown_Interrupt_ID_Err);
    /* Not reached - CRASH_SYSTEM doesn't return */
}
