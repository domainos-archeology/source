/*
 * ast_$clear_transition_bits - Clear transition bits in segment map entries
 *
 * Iterates through segment map entries and clears the transition bit.
 * For entries with valid in-use pages (0x200 <= ppn < 0x1000), calls
 * MMAP_$AVAIL to mark the page as available.
 *
 * Original address: 0x00e0283c
 */

#include "ast/ast_internal.h"

/* External function prototypes */

void ast_$clear_transition_bits(uint32_t *segmap, uint16_t count)
{
    uint16_t *entry;
    uint16_t ppn;

    if (count == 0) {
        goto signal_complete;
    }

    /* Process each segment map entry */
    /* Segment map entries are 4 bytes each, treated as two 16-bit values */
    entry = (uint16_t *)segmap;
    count--;  /* Convert to loop counter */

    do {
        uint16_t flags = entry[0];

        /* Check if entry has SEGMAP_FLAG_IN_USE (0x4000) set */
        if (flags & 0x4000) {
            /* Get the PPN from the low word */
            ppn = entry[1];

            /* If PPN is in valid range (0x200-0xFFF), mark as available */
            if (ppn >= 0x200 && ppn <= 0xFFF) {
                MMAP_$AVAIL((uint32_t)ppn);
            }
        }

        /* Clear the transition bit (0x80) in high byte of flags word */
        entry[0] &= 0x7FFF;

        /* Move to next entry (4 bytes = 2 uint16_t) */
        entry += 2;
        count--;
    } while (count != 0xFFFF);

signal_complete:
    /* Signal that page transition operations are complete */
    EC_$ADVANCE(&AST_$PMAP_IN_TRANS_EC);
}
