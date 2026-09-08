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
 * 4. Orders each batch DESCENDING by unsigned page entry (0x00E5E80C `bls`)
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
 * Internal: order the batch, DESCENDING by unsigned page entry.
 *
 * 0x00E5E7BE-0x00E5E84A.  Selection/exchange sort over the 1-based local
 * array; the comparison at 0x00E5E80C-0x00E5E814 is
 *
 *      move.l (-0x84,A1),D0        ; D0 = batch[j]
 *      cmp.l  (-0x84,A3),D0        ; D0 - batch[i]
 *      bls.b  skip                 ; skip when batch[j] <= batch[i] (unsigned)
 *
 * so the exchange runs when batch[j] > batch[i]: the batch handed to
 * AST_$PURIFY is in DESCENDING unsigned order, not ascending.  (source-87da)
 *
 * The outer `dbf D4w` count is set from D0 = count - 2 at 0x00E5E7C8, so the
 * outer loop runs exactly count-1 times with i = 1 .. count-1; the inner
 * `dbf D2w` count is count - j, so j runs (i+1) .. count.  The image reaches
 * both operands through cursors that are only ever advanced by 4, so the
 * indices below are written 1-based to match.
 */
static void sort_pages_descending(uint32_t *pages, uint16_t count)
{
    uint16_t i, j;
    uint32_t temp;

    /* 0x00E5E7C2: subq.w #1,D0w / beq -> skip the whole sort when count == 1 */
    if (count == 1) {
        return;
    }

    for (i = 1; i <= (uint16_t)(count - 1); i++) {
        for (j = (uint16_t)(i + 1); j <= count; j++) {
            if (pages[j - 1] > pages[i - 1]) {
                /* 0x00E5E816-0x00E5E82A */
                temp = pages[i - 1];
                pages[i - 1] = pages[j - 1];
                pages[j - 1] = temp;
            }
        }
    }
}

void FILE_$FW_PAGES(uid_t *file_uid, uint32_t *page_list, uint16_t *page_count,
                    status_$t *status_ret)
{
    int8_t was_locked;
    uint8_t delete_result[6];  /* Result buffer from DELETE_INT; A6-0x90 */
    uint16_t purify_flags;     /* A6-0x86 */
    uint16_t start_index;      /* D6 */
    uint16_t batch_size;       /* D5 */
    uint16_t next_index;       /* D1 -> D6 */
    uint16_t i;
    uint32_t batch[FW_BATCH_SIZE];  /* A6-0x80 .. A6-0x04, 32 longwords */

    /* 0x00E5E72C-0x00E5E73A: empty page list */
    if (*page_count == 0) {
        *status_ret = status_$ok;
        return;
    }

    start_index = 1;  /* 0x00E5E740 moveq #1,D6 - the list is 1-based */

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
        /*
         * 0x00E5E76A-0x00E5E788: `*page_count` is re-read from the var
         * parameter on every batch, and the "how many are left" test is a
         * LONGWORD compare against 32 (`moveq #0x20,D2` / `cmp.l D2,D1` /
         * `ble`), so the equality case takes the ble arm and still yields 32.
         */
        if ((int32_t)((uint32_t)*page_count - (uint32_t)start_index + 1)
                <= (int32_t)FW_BATCH_SIZE) {
            batch_size = (uint16_t)(*page_count - start_index + 1);
        } else {
            batch_size = FW_BATCH_SIZE;
        }

        /* 0x00E5E78A-0x00E5E790: D1 = start + size, D0 = D1 - 1 (last index) */
        next_index = (uint16_t)(start_index + batch_size);

        /*
         * 0x00E5E792-0x00E5E7BC: copy pages start_index .. next_index-1 of the
         * 1-based caller list into batch[0 ..].  The `bcs` at 0x00E5E794 skips
         * the copy when (next_index - 1) < start_index, i.e. when batch_size
         * is 0; the `dbf` otherwise runs batch_size times.
         */
        for (i = 0; i < batch_size; i++) {
            batch[i] = page_list[start_index + i - 1];
        }

        /*
         * 0x00E5E7BE-0x00E5E84A: order the batch descending (see above).
         * The image skips the sort only for a batch of exactly one.
         */
        sort_pages_descending(batch, batch_size);

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

        /*
         * 0x00E5E86C-0x00E5E874: `tst.w (0x2,A1)` - only the LOW word of the
         * status longword is tested.  Use a mask rather than a byte cast so a
         * little-endian host sees the same word.
         */
        if ((*status_ret & 0xFFFF) != status_$ok) {
            break;
        }

        /* 0x00E5E7C0 already loaded D6 = next_index; 0x00E5E876-0x00E5E87E
         * re-reads *page_count and loops while next_index <= *page_count
         * (unsigned `bls`). */
        start_index = next_index;

    } while (start_index <= *page_count);
}
