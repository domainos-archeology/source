/*
 * DIR_$WIRE_PAGE - Wire (pin) a directory page in memory
 *
 * Wires a specific page address so it cannot be paged out.
 * Records the wired page info in the handle. Crashes if called
 * when a page is already wired (max_slots must be 2).
 *
 * After wiring, determines which cache slot the page belongs to
 * based on the 32KB-aligned base address, and sets max_slots to
 * the slot index (0 or 1).
 *
 * Handle fields used:
 *   +0x14: Wired page handle (4 bytes) - set by MST_$WIRE return
 *   +0x18: Wired page address (4 bytes)
 *   +0x1C: max_slots / current slot index (2 bytes)
 *   +0x24: Cache slot 0 base address (4 bytes)
 *   +0x2C: Cache slot 1 base address (4 bytes)
 *
 * Parameters:
 *   handle    - Pointer to handle structure
 *   page_data - Page virtual address to wire
 *
 * Original address: 0x00E4B7B6
 * Original size: 130 bytes
 */

#include "dir/dir_internal.h"

void DIR_$WIRE_PAGE(void *handle, void *page_data)
{
    uint8_t *h = (uint8_t *)handle;
    uint32_t addr = (uint32_t)(uintptr_t)page_data;
    uint32_t wire_handle;
    status_$t local_status;

    /* Must not already have a page wired (max_slots == 2 means none wired) */
    if (*(int16_t *)(h + 0x1C) != 2) {
        CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
    }

    /* Record page address */
    *(uint32_t *)(h + 0x18) = addr;

    /* Wire the page */
    wire_handle = MST_$WIRE(addr, &local_status);
    *(uint32_t *)(h + 0x14) = wire_handle;

    if (local_status != status_$ok) {
        CRASH_SYSTEM(&local_status);
    }

    /* Determine which cache slot this page belongs to */
    {
        uint32_t group_base = addr & 0xFFFF8000;

        if (group_base == *(uint32_t *)(h + 0x24)) {
            *(int16_t *)(h + 0x1C) = 0;
        } else if (group_base == *(uint32_t *)(h + 0x2C)) {
            *(int16_t *)(h + 0x1C) = 1;
        } else {
            CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
        }
    }
}
