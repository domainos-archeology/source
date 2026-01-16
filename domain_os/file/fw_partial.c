/*
 * FILE_$FW_PARTIAL - Force Write Partial File
 *
 * Original address: 0x00E5E680
 * Size: 158 bytes
 *
 * Forces dirty pages within a byte range to be written back to disk.
 * Iterates through pages covered by the byte range and purifies each.
 *
 * Operation:
 * 1. Calculates starting page and offset from byte offset
 * 2. Calls FILE_$DELETE_INT to check lock status
 * 3. Loops through pages calling AST_$PURIFY for each
 * 4. Continues until byte_count is exhausted or error occurs
 *
 * Page size is 32KB (0x8000 bytes).
 * Pages are numbered starting at 0.
 */

#include "file/file_internal.h"
#include "ml/ml.h"
#include "ast/ast.h"

/* Page size: 32KB */
#define FILE_PAGE_SIZE      0x8000
#define FILE_PAGE_MASK      0x7FFF   /* Mask for offset within page */

/* Purify flags for partial write */
#define FW_PARTIAL_LOCAL    0x0003   /* Local purify with update */
#define FW_PARTIAL_REMOTE   0x8003   /* Include remote sync */

void FILE_$FW_PARTIAL(uid_t *file_uid, uint32_t *start_offset,
                      int32_t *byte_count, status_$t *status_ret)
{
    int8_t was_locked;
    uint8_t delete_result[4];  /* Result buffer from DELETE_INT */
    uint16_t purify_flags;
    uint32_t offset;
    int32_t remaining;
    int32_t bytes_to_page_end;
    uint16_t page_num;

    /* Initialize status to success */
    *status_ret = status_$ok;

    /*
     * Calculate starting page and remaining bytes in first page.
     * Page number = offset >> 15 (divide by 32KB)
     * Bytes to page end = 32KB - (offset & 0x7FFF)
     */
    offset = *start_offset;
    bytes_to_page_end = FILE_PAGE_SIZE - (offset & FILE_PAGE_MASK);
    page_num = (uint16_t)(offset >> 15);  /* offset / 32KB */
    remaining = *byte_count;

    /*
     * Check if file is locked by calling FILE_$DELETE_INT with flags=0.
     */
    was_locked = FILE_$DELETE_INT(file_uid, 0, delete_result, status_ret);

    /*
     * Select purify flags based on lock status.
     */
    if (was_locked < 0) {
        purify_flags = FW_PARTIAL_LOCAL;
    } else {
        purify_flags = FW_PARTIAL_REMOTE;
    }

    /*
     * Loop through pages until all bytes are processed or error occurs.
     */
    while (remaining > 0) {
        /* Purify current page */
        AST_$PURIFY(file_uid, purify_flags, (int16_t)page_num, NULL, 0, status_ret);

        if (*status_ret != status_$ok) {
            /* Error occurred - stop processing */
            break;
        }

        /* Move to next page */
        remaining -= bytes_to_page_end;
        bytes_to_page_end = FILE_PAGE_SIZE;  /* Full page from now on */
        page_num++;
    }
}
