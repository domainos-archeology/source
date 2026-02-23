/*
 * dir_$release_wire - Release wired page and reset cache state
 *
 * Releases a wired directory page via WP_$UNWIRE and resets the
 * max_slots field to 2 (both cache slots available). Crashes if
 * max_slots is already 2 (double-release protection).
 *
 * Handle structure (relevant offsets):
 *   +0x14: wired page address (uint32_t) - passed to WP_$UNWIRE
 *   +0x1C: max_slots (int16_t) - must not be 2, set to 2
 *
 * Original address: 0x00E4B838
 * Original size: 54 bytes
 */

#include "dir/dir_internal.h"

/* WP_$UNWIRE - Unwire a previously wired page */
extern void WP_$UNWIRE(uint32_t page_addr);

void dir_$release_wire(void *handle)
{
    uint8_t *h = (uint8_t *)handle;

    /* Crash if max_slots is already 2 (double release) */
    if (*(int16_t *)(h + 0x1C) == 2) {
        CRASH_SYSTEM((const status_$t *)&Naming_bad_request_header_ver_err);
    }

    /* Reset max_slots to 2 (both cache slots available) */
    *(int16_t *)(h + 0x1C) = 2;

    /* Unwire the page at offset 0x14 */
    WP_$UNWIRE(*(uint32_t *)(h + 0x14));
}
