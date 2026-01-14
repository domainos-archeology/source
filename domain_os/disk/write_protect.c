/*
 * DISK_$WRITE_PROTECT - Set or check write protection
 *
 * Controls write protection for a volume:
 *   mode 0: Enable write protection
 *   mode 1: Check if write protected (returns error if so)
 *
 * @param mode    0 = enable protection, 1 = check protection
 * @param vol_idx Volume index
 * @param status  Output: Status code
 */

#include "disk.h"

/* Write protect flag byte offset within volume entry */
#define DISK_WP_OFFSET  0xa5

/* Volume entry table base */
#define DISK_VOLUME_BASE  ((uint8_t *)0x00e7a1cc)

void DISK_$WRITE_PROTECT(int16_t mode, int16_t vol_idx, status_$t *status)
{
    uint8_t *wp_flag;
    int32_t offset;

    *status = status_$ok;

    /* Calculate offset to write protect flag
     * Each volume entry is 0x48 (72) bytes
     * Offset = base + (vol_idx * 0x48) + 0xa5
     */
    offset = (int16_t)(vol_idx * DISK_VOLUME_SIZE);
    wp_flag = DISK_VOLUME_BASE + offset + DISK_WP_OFFSET;

    if (mode == 0) {
        /* Enable write protection */
        *wp_flag |= 0x01;
    }
    else if (mode == 1) {
        /* Check if write protected */
        if ((*wp_flag & 0x01) != 0) {
            *status = status_$disk_write_protected;
        }
    }
    /* mode > 1: do nothing */
}
