/*
 * DISK_$ERROR_QUE - Handle queue error for disk I/O
 *
 * Dispatches an error handling request to the device-specific
 * error queue handler via the device's jump table.
 *
 * @param req      Request block
 * @param param_2  Parameter 2
 * @param param_3  Parameter 3
 */

#include "disk_internal.h"

void DISK_$ERROR_QUE(void *req, uint16_t param_2, void *param_3)
{
    void **dev_info;
    void *jump_table;
    void (*error_func)(void *, uint16_t, void *);

    /* Get device info pointer from request block at offset +0x18 */
    dev_info = *(void ***)((uint8_t *)req + 0x18);

    /* Get jump table pointer from device info */
    jump_table = *dev_info;

    /* Get error queue function pointer from jump table at offset +0x14 */
    error_func = *(void (**)(void *, uint16_t, void *))
                 ((uint8_t *)jump_table + 0x14);

    /* Call device-specific error handler */
    error_func(req, param_2, param_3);
}
