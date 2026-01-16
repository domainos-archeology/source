/*
 * FILE_$FW_PAGES - Force Write Specific Pages
 *
 * Original address: 0x00E5E71E
 * Size: 364 bytes
 *
 * Forces specific pages to be written back to disk. Takes a list of
 * page numbers (as 32-bit integers with page number in bits 5-31 and
 * sub-page index in bits 0-4).
 *
 * Operation:
 * 1. Checks if page count is 0 (early exit)
 * 2. Calls FILE_$DELETE_INT to check lock status
 * 3. Processes pages in batches of up to 32
 * 4. Sorts each batch using bubble sort (for sequential I/O optimization)
 * 5. Calls AST_$PURIFY for each batch
 *
 * The page_list entries are uint32_t where:
 *   bits 5-31: page number (>> 5)
 *   bits 0-4:  sub-page index within page
 */

#include "file/file_internal.h"
#include "ml/ml.h"
#include "ast/ast.h"

/* Maximum pages per batch */
#define FW_BATCH_SIZE   32

/* Purify flags for page write */
#define FW_PAGES_LOCAL  0x0012   /* Local purify with batch flag */
#define FW_PAGES_REMOTE 0x8012   /* Include remote sync */

/*
 * Internal: Sort page list using bubble sort
 *
 * Sorts an array of uint32 page entries in ascending order.
 * Used to optimize disk I/O by writing pages sequentially.
 */
static void sort_pages(uint32_t *pages, uint16_t count)
{
    uint16_t i, j;
    uint32_t temp;

    if (count <= 1) {
        return;
    }

    /* Bubble sort - simple and sufficient for small batches */
    for (i = 0; i < count - 1; i++) {
        for (j = i + 1; j < count; j++) {
            if (pages[j] < pages[i]) {
                /* Swap */
                temp = pages[i];
                pages[i] = pages[j];
                pages[j] = temp;
            }
        }
    }
}

void FILE_$FW_PAGES(uid_t *file_uid, uint32_t *page_list, uint16_t *page_count,
                    status_$t *status_ret)
{
    int8_t was_locked;
    uint8_t delete_result[6];  /* Result buffer from DELETE_INT */
    uint16_t purify_flags;
    uint16_t start_index;
    uint16_t total_pages;
    uint16_t batch_size;
    uint16_t i;
    uint32_t batch[FW_BATCH_SIZE];  /* Local copy for sorting */

    /* Check for empty page list */
    if (*page_count == 0) {
        *status_ret = status_$ok;
        return;
    }

    total_pages = *page_count;
    start_index = 1;  /* Page indices are 1-based in the list */

    /*
     * Check if file is locked by calling FILE_$DELETE_INT with flags=0.
     */
    was_locked = FILE_$DELETE_INT(file_uid, 0, delete_result, status_ret);

    /*
     * Select purify flags based on lock status.
     * 0x12 = 0x10 (batch mode) | 0x02 (update timestamp)
     */
    if (was_locked < 0) {
        purify_flags = FW_PAGES_LOCAL;
    } else {
        purify_flags = FW_PAGES_REMOTE;
    }

    /*
     * Process pages in batches of up to 32.
     */
    do {
        /* Calculate batch size */
        if ((total_pages - start_index + 1) < FW_BATCH_SIZE) {
            batch_size = (total_pages - start_index) + 1;
        } else {
            batch_size = FW_BATCH_SIZE;
        }

        /*
         * Copy pages to local batch array.
         * The original code uses 1-based indexing for the page list.
         */
        for (i = 0; i < batch_size; i++) {
            batch[i] = page_list[start_index + i - 1];
        }

        /*
         * Sort the batch for sequential I/O optimization.
         * Skip if only one page (no sorting needed).
         */
        if (batch_size > 1) {
            sort_pages(batch, batch_size);
        }

        /*
         * Purify the batch.
         * Parameters:
         *   uid        - file UID
         *   flags      - purify flags (0x12 or 0x8012)
         *   segment    - 0 (not used with page list)
         *   page_list  - sorted batch of page entries
         *   page_count - number of pages in batch
         *   status_ret - output status
         */
        AST_$PURIFY(file_uid, purify_flags, 0, batch, batch_size, status_ret);

        /* Check for error */
        if ((*status_ret & 0xFFFF) != status_$ok) {
            /* Error or EOF - stop processing */
            break;
        }

        /* Move to next batch */
        start_index += batch_size;

    } while (start_index <= total_pages);
}
