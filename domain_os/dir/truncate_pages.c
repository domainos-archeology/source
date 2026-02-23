/*
 * dir_$truncate_pages - Truncate/resize directory pages
 *
 * Truncates a directory to the specified number of pages by calling
 * AST_$INVALIDATE to invalidate the excess pages. Clears the dirty
 * flag at handle+0x0E and zeros the status.
 *
 * The number of pages to invalidate is computed as:
 *   (handle[4] >> 10) - new_page_count
 * where handle[4] is the directory size in bytes (at offset 0x10).
 *
 * Parameters:
 *   handle         - Directory handle (pointer to handle structure)
 *   new_page_count - Number of pages to keep
 *   status_ret     - Output: status code (always set to 0)
 *
 * Returns: 0 (always)
 *
 * Original address: 0x00E4E90A
 * Original size: 86 bytes
 */

#include "dir/dir_internal.h"

/* AST_$INVALIDATE - Invalidate (truncate) pages from an object */
extern void AST_$INVALIDATE(uid_t *uid, uint32_t start_page,
                            int32_t num_pages, int8_t flag,
                            void *result);

uint32_t dir_$truncate_pages(void *handle, uint16_t new_page_count,
                             status_$t *status_ret)
{
    uint32_t *h = (uint32_t *)handle;
    uid_t local_uid;
    uint8_t result_buf[4];
    uint32_t total_pages;
    int32_t pages_to_remove;

    /* Copy UID from handle (first 8 bytes) */
    local_uid.high = h[0];
    local_uid.low = h[1];

    /* Compute total pages from directory size at handle+0x10 */
    total_pages = h[4] >> 10;

    /* Compute pages to remove */
    pages_to_remove = (int16_t)(total_pages - new_page_count);

    /* Invalidate the excess pages */
    AST_$INVALIDATE(&local_uid, (uint32_t)new_page_count,
                    pages_to_remove, (int8_t)-1, result_buf);

    /* Clear dirty flag at handle+0x0E */
    *((uint8_t *)handle + 0x0E) = 0;

    /* Clear status */
    *status_ret = status_$ok;

    return 0;
}
