/*
 * DISK_$SET_BUFF - Set buffer properties
 *
 * This is a wrapper that acquires the disk lock, calls DBUF_$SET_BUFF,
 * then releases the lock.
 *
 * @param buffer   Buffer to modify
 * @param flags    Flags to set
 * @param param_3  Additional parameter
 */

#include "disk_internal.h"

void DISK_$SET_BUFF(void *buffer, uint16_t flags, void *param_3)
{
    ML_$LOCK(DISK_LOCK_ID);
    DBUF_$SET_BUFF(buffer, flags, param_3);
    ML_$UNLOCK(DISK_LOCK_ID);
}
