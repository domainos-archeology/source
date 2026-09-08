/*
 * DISK_$GET_STATS - Get disk statistics
 *
 * Retrieves statistics for a specific device. First copies global
 * disk statistics from 0xe7aedc, then looks up the device-specific
 * statistics function in the jump table and calls it.
 *
 * The frame is (0x8,A6) word, (0xA,A6) word, (0xC,A6) word, (0xE,A6) long,
 * (0x12,A6) long: FIVE arguments.  The first two words are matched against
 * the device table entry's +0x04 and +0x06 (0x00E3DBE0 / 0x00E3DBE6) and the
 * third is handed straight to the driver's own statistics routine together
 * with the controller number and the buffer ("pea (A2) / move.w (0xC,A6) /
 * move.w D2w / jsr (A3)" at 0x00E3DBF4-0x00E3DBFE).
 *
 * @param ctype       Controller type to look up (dcte_t.ctype)
 * @param cnum        Controller number (dcte_t.cnum)
 * @param unit        Unit passed on to the driver routine
 * @param has_stats   Output: Non-zero if stats are available
 * @param stats       Output: Statistics buffer (DISK_STATS_SIZE bytes)
 */

#include "disk/disk_internal.h"

/* Global statistics at 0xe7aedc */
#define DISK_GLOBAL_STATS  ((uint32_t *)0x00e7aedc)

/* Device registration table */
#define DISK_DEVICE_TABLE  ((uint8_t *)0x00e7ad5c)

void DISK_$GET_STATS(int16_t ctype, int16_t cnum, int16_t unit,
                     uint8_t *has_stats, void *stats)
{
    uint32_t *stats_buf = (uint32_t *)stats;
    int16_t i;
    uint32_t *entry;
    void *jump_table;
    void (*get_stats_func)(uint16_t, uint16_t, void *);

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
            if (entry_info[0] == (uint16_t)ctype &&
                entry_info[1] == (uint16_t)cnum) {

                /* Found matching device - get stats function */
                jump_table = (void *)(uintptr_t)*entry;
                get_stats_func = *(void (**)(uint16_t, uint16_t, void *))
                                 ((uint8_t *)jump_table + 0x18);

                if (get_stats_func != NULL) {
                    /* Call device-specific stats function */
                    get_stats_func((uint16_t)cnum, (uint16_t)unit,
                                   stats_buf);

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
