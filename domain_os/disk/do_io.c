/*
 * DISK_$DO_IO - Perform disk I/O operation
 *
 * Dispatches an I/O request to the appropriate device driver
 * by looking up the device's jump table and calling its DO_IO function.
 *
 * The dev_entry parameter points to the device data section within
 * a volume entry (at offset +0x7c). At +0x18 within this data is
 * a pointer to device info which contains the jump table.
 *
 * @param dev_entry  Device entry (volume entry + 0x7c, dev info ptr at +0x18)
 * @param req        I/O request buffer
 * @param param_3    Additional parameter
 * @param result     Result output
 */

#include "disk_internal.h"

void DISK_$DO_IO(void *dev_entry, void *req, void *param_3, void *result)
{
    void **dev_info;
    void *jump_table;
    void (*do_io_func)(void *, void *, void *, void *);

    /* Get device info pointer from dev_entry at offset +0x18 */
    dev_info = *(void ***)((uint8_t *)dev_entry + 0x18);

    /* Get jump table pointer from device info */
    jump_table = *dev_info;

    /* Get DO_IO function pointer from jump table at offset +0x10 */
    do_io_func = *(void (**)(void *, void *, void *, void *))
                 ((uint8_t *)jump_table + 0x10);

    /* Call device-specific DO_IO function */
    do_io_func(dev_entry, req, param_3, result);
}
