/*
 * WIN_$DINIT - Winchester Device Initialization
 *
 * Initializes a Winchester disk unit. Acquires the unit lock,
 * calls the common disk initialization, then releases the lock.
 *
 * @param vol_idx   Volume index
 * @param unit      Unit number
 * @param param_3   Device parameter 3
 * @param param_4   Device parameter 4
 * @param param_5   Device parameter 5
 * @param param_6   Device parameter 6
 * @param param_7   Device parameter 7
 * @return          Result from DISK_INIT
 */

#include "win.h"

uint32_t WIN_$DINIT(uint16_t vol_idx, uint16_t unit, void *param_3,
                    void *param_4, void *param_5, void *param_6, void *param_7)
{
    uint32_t result;
    uint8_t *win_data = WIN_DATA_BASE;
    int16_t lock_id;
    uint32_t unit_offset;

    /* Calculate offset for this unit's lock */
    unit_offset = (uint32_t)unit * WIN_UNIT_ENTRY_SIZE;
    lock_id = *(int16_t *)(win_data + unit_offset + 0x08);

    /* Acquire unit lock */
    ML__LOCK(lock_id);

    /* Call common disk initialization */
    result = DISK_INIT(unit, vol_idx, param_3, param_4, param_5, param_6, param_7);

    /* Release unit lock */
    ML__UNLOCK(lock_id);

    return result;
}
