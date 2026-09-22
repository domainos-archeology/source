/*
 * DISK_$SET_BUFF - DBUF_$SET_BUFF under the disk resource lock
 *
 * 0x00E3BBD4 - 0x00E3BC0C (58 bytes).  Re-emitted from the disassembly on
 * 2026-09-19: the third argument is the status cell DBUF_$SET_BUFF fills
 * ((0xe,A6) forwarded at 0x00E3BBE8), not an opaque parameter.
 *
 * Arguments:
 *   (0x8,A6) buffer  longword, the buffer VA
 *   (0xc,A6) flags   word
 *   (0xe,A6) status  -> status_$t
 *
 * ML_$LOCK(15), DBUF_$SET_BUFF(buffer, flags, status), ML_$UNLOCK(15);
 * all three open a discarded Pascal result slot.
 */

#include "disk/disk_internal.h"
#include "dbuf/dbuf.h"
#include "ml/ml.h"

void DISK_$SET_BUFF(void *buffer, uint16_t flags, status_$t *status)
{
    ML_$LOCK(DISK_LOCK_ID);
    DBUF_$SET_BUFF(buffer, flags, status);
    ML_$UNLOCK(DISK_LOCK_ID);
}
