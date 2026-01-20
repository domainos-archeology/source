/*
 * FIND_VOLX - Find volume index by physical location
 *
 * Searches the VOLX table for a mounted volume matching the given
 * physical device location (dev, bus, controller, lv_num).
 *
 * Original address: 0x00E6B0BC
 */

#include "volx/volx_internal.h"

/*
 * FIND_VOLX
 *
 * Parameters:
 *   dev     - Device unit number
 *   bus     - Bus/controller number
 *   ctlr    - Controller type
 *   lv_num  - Logical volume number
 *
 * Returns:
 *   Volume index (1-6) if found, 0 if not found
 *
 * Algorithm:
 *   Iterates through entries 1-6 of the VOLX table, comparing
 *   the device location fields. Returns the first match.
 *
 * Assembly notes:
 *   - Uses A5 as base register pointing to VOLX table (0xE82604)
 *   - Iterates with counter in D3 (5 downto -1, so 6 iterations)
 *   - Index in D4 starts at 1 and increments
 *   - Entry offset is 0x20 bytes per entry
 *   - Compares at offsets -2, -4, -6, -8 from entry pointer + 0x20
 *     (i.e., offsets 0x1E, 0x1C, 0x1A, 0x18 within entry)
 */
int16_t FIND_VOLX(int16_t dev, int16_t bus, int16_t ctlr, int16_t lv_num)
{
    int16_t count;
    int16_t vol_idx;
    volx_entry_t *entry;

    count = 5;          /* Loop counter (5 downto -1 = 6 iterations) */
    vol_idx = 1;        /* Volume index (1-6) */
    entry = &VOLX_$TABLE_BASE[1];  /* Start at entry 1 */

    while (count >= 0) {
        if (entry->lv_num == lv_num &&
            entry->ctlr == ctlr &&
            entry->dev == dev &&
            entry->bus == bus) {
            return vol_idx;
        }
        vol_idx++;
        entry++;
        count--;
    }

    return 0;   /* Not found */
}
