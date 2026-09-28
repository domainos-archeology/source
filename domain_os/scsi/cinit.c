/*
 * SCSI_$CINIT - SCSI Controller Initialization (Stub)
 *
 * Original address: 0x00e34f04
 *
 * Re-emitted from the image (0x00E34F04..0x00E34F10, 14 bytes -- the SAU2
 * map gives the whole SCSI_ segment as 0x10 bytes; the "12540" in the batch
 * list is not this function's size).  No references.  The body is a link
 * frame around `move.l #0x100002,D0`: status_$io_controller_not_in_system.
 *
 * Assembly:
 *   00e34f04    link.w A6,-0x4
 *   00e34f08    move.l #0x100002,D0
 *   00e34f0e    unlk A6
 *   00e34f10    rts
 */

#include "scsi/scsi_internal.h"

status_$t SCSI_$CINIT(void)
{
    return status_$io_controller_not_in_system;
}
