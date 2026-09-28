/*
 * SYSBUS_$DEFINE_INT - Crash: dynamic interrupt definition is not supported
 *
 * Re-emitted from the image (0x00E0ABBC..0x00E0ABCC, 18 bytes).  No
 * references.
 *
 *   00e0abc0  pea (0xe,PC)            ; &0x00E0ABD0
 *   00e0abc4  jsr CRASH_SYSTEM        ; the 4 bytes are left for unlk
 *
 * The cell at 0x00E0ABD0 holds 00 3e 00 02 = status 0x003E0002, "unknown
 * interrupt ID"; the old body had 0x00080032, which is not what the image
 * contains.
 *
 * From: 0x00e0abbc
 */

#include "sysbus/sysbus_internal.h"

static const status_$t sysbus_$define_int_status_00e0abd0 =
    status_$sysbus_unknown_interrupt_id;

void SYSBUS_$DEFINE_INT(void)
{
    CRASH_SYSTEM(&sysbus_$define_int_status_00e0abd0);   /* 0x00E0ABC4 */
}
