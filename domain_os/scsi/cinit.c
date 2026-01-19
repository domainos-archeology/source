/*
 * SCSI_$CINIT - SCSI Controller Initialization (Stub)
 *
 * Original address: 0x00e34f04
 *
 * In this Domain/OS build, SCSI support is not enabled.
 * This function simply returns status_$io_controller_not_in_system
 * to indicate no SCSI controller is present.
 *
 * Assembly:
 *   00e34f04    link.w A6,-0x4
 *   00e34f08    move.l #0x100002,D0
 *   00e34f0e    unlk A6
 *   00e34f10    rts
 */

#include "scsi/scsi.h"

status_$t SCSI_$CINIT(void)
{
    return status_$io_controller_not_in_system;
}
