/*
 * DIR_$WIRE_PAGE - Wire (pin) a directory page in memory
 *
 * Wires a specific page address so it cannot be paged out.
 * Records the wired page info in the handle. Crashes if called
 * when a page is already wired (max_slots must be 2).
 *
 * After wiring, works out which of dir_$map_page's two cache slots covers
 * the page (by matching its 0x8000-aligned group base against the slots'
 * bases) and records that slot number in dir_$handle_t.max_slots, which is
 * how dir_$map_page knows not to evict it.
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
    dir_$handle_t *h = (dir_$handle_t *)handle;         /* A2 */
    uint32_t       addr = ARCH_PTR_TO_VA(page_data);    /* D2 */
    status_$t      local_status;                        /* A6-0x08 */

    /* 0x00E4B7C6: max_slots == 2 means no page is wired yet. */
    if (h->max_slots != 2) {
        CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
    }

    h->buf = addr;      /* 0x00E4B7DA `move.l D2,(0x18,A2)` */

    /* 0x00E4B7DE-0x00E4B7EC: the wire handle comes back in D0. */
    h->wired_page = MST_$WIRE(addr, &local_status);

    /* 0x00E4B7F0: the status is tested AFTER the store. */
    if (local_status != status_$ok) {
        CRASH_SYSTEM(&local_status);
    }

    /* 0x00E4B802-0x00E4B828: `andi.l #-0x8000,D0` rounds down to the
     * group base, which must match one of the two cache slots. */
    {
        uint32_t group_base = addr & 0xFFFF8000u;

        if (group_base == h->cache0_base) {
            h->max_slots = 0;       /* 0x00E4B810 `clr.w (0x1c,A2)` */
        } else if (group_base == h->cache1_base) {
            h->max_slots = 1;       /* 0x00E4B81C */
        } else {
            CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
        }
    }
}
