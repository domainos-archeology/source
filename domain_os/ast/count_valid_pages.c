/*
 * ast_$count_valid_pages - Count and allocate pages for reading
 *
 * This is a nested subprocedure that accesses parent frame variables.
 * For read-only objects (per-boot flag set), it clears transition bits
 * and returns an error. Otherwise, it allocates pages and zeros them.
 *
 * Parameters (from caller's frame at A6):
 *   param_1 - ASTE pointer (A6+0x08)
 *   param_2 - Count (A6+0x0C)
 *   A6-0x1D byte with bit 1 = per-boot flag
 *   A6-0x14 = PPN array pointer
 *   A6-0x18 = status pointer
 *
 * Returns: Number of pages allocated (or original count on error)
 *
 * Original address: 0x00e0305c
 */

#include "ast/ast_internal.h"

/* External function prototypes */
extern void ZERO_PAGE(uint32_t ppn);

/*
 * NOTE: This function is a nested procedure accessing parent frame variables.
 * The parameters shown are what would be passed explicitly if restructured.
 *
 * Parent frame layout:
 *   A6-0x1D: flags byte (bit 1 = per-boot/read-only)
 *   A6-0x14: pointer to PPN array
 *   A6-0x18: pointer to status
 *
 * The actual implementation needs to be integrated with the page fault
 * handler that calls it, accessing those frame variables directly.
 */

int16_t ast_$count_valid_pages(aste_t *aste, int16_t count,
                                uint8_t per_boot_flag,
                                uint32_t *ppn_array,
                                status_$t *status)
{
    int16_t i;
    int16_t allocated;

    /* Check if per-boot (read-only) flag is set */
    if ((per_boot_flag & 2) != 0) {
        /* Read-only object - clear transition bits and return error */
        ast_$clear_transition_bits(aste, count);
        *status = 0x50008;  /* status_$file_read_only or similar */
        return count;
    }

    /* Allocate pages */
    allocated = ast_$allocate_pages(1, count, ppn_array);

    if (allocated != 0) {
        /* Zero each allocated page */
        for (i = allocated - 1; i >= 0; i--) {
            ZERO_PAGE(ppn_array[i]);
        }
    }

    return allocated;
}
