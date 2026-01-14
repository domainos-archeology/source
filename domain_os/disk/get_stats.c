/*
 * DISK_$GET_STATS - Get disk statistics
 *
 * Retrieves statistics for a specific device. First copies global
 * disk statistics from 0xe7aedc, then looks up the device-specific
 * statistics function in the jump table and calls it.
 *
 * @param dev_type    Device type to look up
 * @param controller  Controller number
 * @param has_stats   Output: Non-zero if stats are available
 * @param stats       Output: Statistics buffer
 */

#include "disk.h"

/* Global statistics at 0xe7aedc */
#define DISK_GLOBAL_STATS  ((uint32_t *)0x00e7aedc)

/* Device registration table */
#define DISK_DEVICE_TABLE  ((uint8_t *)0x00e7ad5c)

void DISK_$GET_STATS(int16_t vol_idx, void *stats, status_$t *status)
{
    uint8_t *has_stats = (uint8_t *)status;  /* Actually a flag byte */
    uint32_t *stats_buf = (uint32_t *)stats;
    int16_t i;
    uint32_t *entry;
    void *jump_table;
    void (*get_stats_func)(uint16_t, void *);

    /* Clear has_stats flag */
    *has_stats = 0;

    /* Copy global statistics (5 longs + 1 word = 22 bytes) */
    for (i = 0; i < 5; i++) {
        stats_buf[i] = DISK_GLOBAL_STATS[i];
    }
    *(uint16_t *)&stats_buf[5] = *(uint16_t *)&DISK_GLOBAL_STATS[5];

    /* Search device table for matching device type and controller */
    entry = (uint32_t *)DISK_DEVICE_TABLE;
    for (i = 0x1f; i >= 0; i--) {
        if (*entry != 0) {
            uint16_t *entry_info = (uint16_t *)((uint8_t *)entry + 4);
            if (entry_info[0] == (uint16_t)vol_idx /* dev_type */ &&
                entry_info[1] == 0 /* controller */) {

                /* Found matching device - get stats function */
                jump_table = (void *)(uintptr_t)*entry;
                get_stats_func = *(void (**)(uint16_t, void *))
                                 ((uint8_t *)jump_table + 0x18);

                if (get_stats_func != NULL) {
                    /* Call device-specific stats function */
                    get_stats_func(0 /* controller */, stats_buf);

                    /* Set has_stats if any stats are non-zero */
                    if (stats_buf[0] != 0 || stats_buf[1] != 0) {
                        *has_stats = 0xff;
                    }
                }
                return;
            }
        }
        entry += 3;  /* 12 bytes per entry */
    }
}
