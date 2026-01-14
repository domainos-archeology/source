/*
 * DISK_$SPIN_DOWN - Spin down all disk devices
 *
 * Iterates through all registered disk devices and calls their
 * spin-down function (first entry in jump table). Waits for the
 * maximum spin-down time returned by any device.
 */

#include "disk.h"

/* Device registration table */
#define DISK_DEVICE_TABLE  ((uint8_t *)0x00e7ad5c)

/* Time wait function */
extern void TIME__WAIT(void *timeout_type, void *timeout, status_$t *status);

/* Timeout type constant */
static uint16_t timeout_type = 0;  /* Embedded in code at 0xe3db72 */

void DISK_$SPIN_DOWN(int16_t vol_idx, status_$t *status)
{
    int16_t max_time = 0;
    int16_t device_time;
    int16_t i;
    uint32_t *entry;
    void *jump_table;
    int16_t (*spin_down_func)(void *);

    (void)vol_idx;
    *status = status_$ok;

    /* Iterate through all device registration entries */
    entry = (uint32_t *)DISK_DEVICE_TABLE;
    for (i = 0x1f; i >= 0; i--) {
        /* Check if entry is valid */
        if (*entry != 0) {
            jump_table = (void *)(uintptr_t)*entry;

            /* Get spin-down function from jump table entry 0 */
            spin_down_func = *(int16_t (**)(void *))jump_table;

            if (spin_down_func != NULL) {
                /* Call spin-down function, passing device info */
                device_time = spin_down_func((void *)((uint8_t *)entry + 6));

                /* Track maximum spin-down time */
                if (device_time > max_time) {
                    max_time = device_time;
                }
            }
        }
        entry += 3;  /* 12 bytes per entry */
    }

    /* If any device returned a spin-down time, wait */
    if (max_time > 0) {
        int32_t wait_time = (int32_t)max_time << 2;
        uint16_t wait_mode = 0;
        TIME__WAIT(&timeout_type, &wait_time, status);
    }
}
