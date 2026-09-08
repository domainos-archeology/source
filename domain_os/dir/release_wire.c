/*
 * dir_$release_wire - Release wired page and reset cache state
 *
 * Releases the page DIR_$WIRE_PAGE pinned: puts dir_$handle_t.max_slots
 * back to 2 (neither cache slot is pinned) and hands the wire handle at
 * +0x14 to WP_$UNWIRE.  Crashes if max_slots is already 2, i.e. on a
 * double release.
 *
 * Original address: 0x00E4B838
 * Original size: 54 bytes
 */

#include "dir/dir_internal.h"

void dir_$release_wire(void *handle)
{
    dir_$handle_t *h = (dir_$handle_t *)handle;     /* A2 */

    /* 0x00E4B842: max_slots == 2 means nothing is wired. */
    if (h->max_slots == 2) {
        CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
    }

    h->max_slots = 2;                   /* 0x00E4B856 */

    WP_$UNWIRE(h->wired_page);          /* 0x00E4B85C */
}
