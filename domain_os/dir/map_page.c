/*
 * dir_$map_page - Map directory data page with 2-slot LRU cache
 *
 * Maps a specific page of a directory's data into memory. Uses a
 * 2-slot cache keyed by page group (page_idx >> 5). Each group
 * contains 32 pages of 1024 bytes each (32KB total per group).
 * On cache miss, calls MST_$REMAP_PRIVI to map the group.
 *
 * Handle structure (relevant offsets):
 *   +0x1C: max_slots (int16_t) - number of valid cache slots
 *   +0x1E: cur_slot word (int16_t) - current slot selector
 *   +0x1F: cur_slot low byte - toggled to switch slots
 *   +0x22 + slot*8: cached group number (uint16_t)
 *   +0x24 + slot*8: cached base address (uint32_t)
 *
 * Returns pointer to (base_addr + (page_idx & 0x1F) * 1024).
 *
 * Original address: 0x00E4B340
 * Original size: 260 bytes
 */

#include "dir/dir_internal.h"

/* DIR_$CONST_ONE_W and DAT_00e4b448 - MST remap parameters */

void *dir_$map_page(void *handle, int16_t page_idx)
{
    uint8_t *h = (uint8_t *)handle;
    int16_t slot_off;
    uint32_t group;
    uint32_t offset;
    uint32_t base;

    /* Read current slot and compute slot byte offset */
    slot_off = *(int16_t *)(h + 0x1E);
    slot_off <<= 3;

    /* Check if current slot has the requested group */
    group = (uint16_t)page_idx >> 5;
    if (*(uint16_t *)(h + 0x22 + slot_off) == (uint16_t)group) {
        /* Cache hit - current slot */
        offset = (uint32_t)(uint16_t)((page_idx & 0x1F) << 10);
        return (void *)(*(uint32_t *)(h + 0x24 + slot_off) + offset);
    }

    /* Toggle to other slot */
    h[0x1F] ^= 1;
    slot_off = *(int16_t *)(h + 0x1E);
    slot_off <<= 3;

    /* Check if other slot has the requested group */
    if (*(uint16_t *)(h + 0x22 + slot_off) == (uint16_t)group) {
        /* Cache hit - other slot */
        offset = (uint32_t)(uint16_t)((page_idx & 0x1F) << 10);
        return (void *)(*(uint32_t *)(h + 0x24 + slot_off) + offset);
    }

    /* Full cache miss - need to remap */

    /* If toggled slot equals max_slots, toggle back to evict original slot */
    if (*(int16_t *)(h + 0x1E) == *(int16_t *)(h + 0x1C)) {
        h[0x1F] ^= 1;
    }

    /* Compute final slot offset */
    slot_off = *(int16_t *)(h + 0x1E);
    slot_off <<= 3;
    uint8_t *slot_ptr = h + slot_off;

    /* Store new group number */
    uint16_t grp16 = (uint16_t)page_idx >> 5;
    *(uint16_t *)(slot_ptr + 0x22) = grp16;

    /* Compute map address: group << 15 */
    uint32_t map_addr = (uint32_t)grp16 << 15;

    /* Call MST_$REMAP_PRIVI to map the group */
    status_$t status;
    uint32_t result;
    void *mapped_addr;
    mapped_addr = MST_$REMAP_PRIVI(&DIR_$CONST_ONE_W,
                                    (uint32_t *)(slot_ptr + 0x24),
                                    &DAT_00e4b448,
                                    &map_addr,
                                    &DAT_00e4b448,
                                    &result,
                                    &status);

    /* Store the returned base address */
    *(uint32_t *)(slot_ptr + 0x24) = (uint32_t)(uintptr_t)mapped_addr;

    /* Crash on remap error */
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /* Verify the remap returned expected size (0x8000 = 32KB) */
    if (result != 0x8000) {
        CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
    }

    /* Compute final address */
    offset = (uint32_t)(uint16_t)((page_idx & 0x1F) << 10);
    return (void *)(*(uint32_t *)(slot_ptr + 0x24) + offset);
}
