/*
 * DISK_$MNT_DINIT - Mount device initialization
 *
 * Calls the device-specific initialization function for mounting.
 * Looks up the device's jump table and calls its DINIT function.
 *
 * @param vol_idx  Volume index (unused in call)
 * @param dev_ptr  Pointer to device info pointer
 * @param param_3  Device init parameter 1
 * @param param_4  Device init parameter 2
 * @param param_5  Device init parameter 3
 * @param param_6  Device init parameter 4
 * @param param_7  Device init parameter 5
 */

#include "disk_internal.h"

void DISK_$MNT_DINIT(uint16_t vol_idx, void **dev_ptr, void *param_3,
                     void *param_4, void *param_5, void *param_6, void *param_7)
{
    void *dev_info;
    void *jump_table;
    uint16_t unit;
    void (*dinit_func)(uint16_t, void *, void *, void *, void *, void *);

    /* Get device info from pointer */
    dev_info = *dev_ptr;

    /* Get jump table from device info */
    jump_table = *(void **)dev_info;

    /* Get DINIT function pointer from jump table at offset +0x08 */
    dinit_func = *(void (**)(uint16_t, void *, void *, void *, void *, void *))
                 ((uint8_t *)jump_table + 0x08);

    /* Get unit number from device info at offset +0x06 */
    unit = *(uint16_t *)((uint8_t *)dev_info + 0x06);

    /* Call device-specific DINIT function */
    dinit_func(unit, param_3, param_4, param_5, param_6, param_7);
}
