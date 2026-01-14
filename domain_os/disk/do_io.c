/*
 * DISK_$DO_IO - Perform disk I/O operation
 *
 * Dispatches an I/O request to the appropriate device driver
 * by looking up the device's jump table and calling its DO_IO function.
 *
 * The request block contains a pointer to device info which contains
 * the jump table pointer.
 *
 * @param req      Request block (device info ptr at +0x18)
 * @param buffer   I/O buffer
 * @param param_3  Additional parameter
 * @param lba      Logical block address
 */

#include "disk.h"

void DISK_$DO_IO(void *req, void *buffer, void *param_3, uint32_t lba)
{
    void **dev_info;
    void *jump_table;
    void (*do_io_func)(void *, void *, void *, uint32_t);

    /* Get device info pointer from request block at offset +0x18 */
    dev_info = *(void ***)((uint8_t *)req + 0x18);

    /* Get jump table pointer from device info */
    jump_table = *dev_info;

    /* Get DO_IO function pointer from jump table at offset +0x10 */
    do_io_func = *(void (**)(void *, void *, void *, uint32_t))
                 ((uint8_t *)jump_table + 0x10);

    /* Call device-specific DO_IO function */
    do_io_func(req, buffer, param_3, lba);
}
