/*
 * DISK_$SPIN_DOWN - Spin down all disk devices
 *
 * Iterates through all registered disk devices and calls their
 * spin-down function (first entry in jump table). Waits for the
 * maximum spin-down time returned by any device.
 */

#include "disk/disk_internal.h"

/* Device registration table */
#define DISK_DEVICE_TABLE  ((uint8_t *)0x00e7ad5c)

/*
 * 0x00E3DB72: the constant delay-type word TIME_$WAIT is handed by reference
 * (`pea (0x12,PC)` at 0x00E3DB5E; PC = 0x00E3DB60).  Value 0 = relative.
 */
static const uint16_t disk_$spin_down_delay_type = 0;

/*
 * The routine reads no stack parameters (`link.w A6,-0x20` at 0x00E3DB04 is
 * followed by no positive-displacement A6 access) and never writes a caller
 * status - TIME_$WAIT's status goes into a local cell at A6-0x14 that is
 * discarded.
 */
void DISK_$SPIN_DOWN(void)
{
    int16_t max_time = 0;
    int16_t device_time;
    int16_t i;
    uint32_t *entry;
    void *jump_table;
    int16_t (*spin_down_func)(void *);

    /* A6-0x14: TIME_$WAIT's status cell; never read back */
    status_$t wait_status;

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
        /* A6-0x10: the clock_t built at 0x00E3DB4A - high = max_time * 4
         * (sign-extended then shifted), low word cleared. */
        clock_t wait_time;
        wait_time.high = (uint32_t)((int32_t)max_time << 2);
        wait_time.low = 0;
        TIME_$WAIT((uint16_t *)&disk_$spin_down_delay_type, &wait_time,
                   &wait_status);
    }
}
