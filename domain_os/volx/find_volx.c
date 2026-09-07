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
 * Assembly notes (0x00E6B0BC):
 *   - 0x00E6B0C4  lea (0xe82604).l,A5    - A5 = entry 1 of the VOLX table
 *   - 0x00E6B0DA  moveq #0x5,D3          - counter, 5 downto -1: 6 iterations
 *   - 0x00E6B0DC  moveq #0x1,D4          - index starts at 1
 *   - 0x00E6B0DE  lea (0x20,A5),A0       - A0 is BIASED: it points one entry
 *                                          past entry 1, i.e. A5 + idx * 0x20
 *   - 0x00E6B104  lea (0x20,A0),A0       - advance one entry
 *   - fields are read at negative displacements off that biased pointer:
 *       0x00E6B0E6  cmp.w (-0x2,A1),D2w  lv_num  (entry offset 0x1E)
 *       0x00E6B0EC  cmp.w (-0x4,A1),D1w  ctlr    (entry offset 0x1C)
 *       0x00E6B0F2  cmp.w (-0x8,A1),D5w  dev     (entry offset 0x18)
 *       0x00E6B0F8  cmp.w (-0x6,A1),D0w  bus     (entry offset 0x1A)
 *     so the scan covers indices 1..6 = the whole 0xC0-byte VOLX_ segment.
 */
int16_t FIND_VOLX(int16_t dev, int16_t bus, int16_t ctlr, int16_t lv_num)
{
    int16_t count;
    int16_t vol_idx;
    volx_$entry_t *entry;

    count = 5;          /* Loop counter (5 downto -1 = 6 iterations) */
    vol_idx = 1;        /* Volume index (1-6) */
    entry = VOLX_$ENTRY(1);  /* Start at entry 1 */

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
