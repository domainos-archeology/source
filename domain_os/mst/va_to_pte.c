/*
 * mst_$va_to_pte - Look up page table entry for a virtual address
 *
 * Translates an ASID and virtual address into a pointer to the
 * corresponding MST page table entry (PTE). Also returns protection
 * bits from the entry.
 *
 * The lookup process:
 * 1. Call MST_$VA_TO_SEGNO to get segment number and sub-segment index
 * 2. Validate segment number (must be < 0x3A)
 * 3. Look up the MST ASID base to get the page directory index
 * 4. Check that the page directory entry is non-zero (page table page exists)
 * 5. Compute PTE address: page_table_page * 0x400 + sub_index * 16 + 0xEF6000
 * 6. Verify PTE is valid (first longword non-zero)
 * 7. Extract protection bits from PTE byte at offset 0x0A
 *
 * On success:
 *   - *entry_out points to the PTE (16-byte entry starting at -0x400 offset)
 *   - *prot_out contains the protection class ((byte & 0x3E) >> 1)
 *   - *status = status_$ok
 *
 * On failure:
 *   - *status = status_$reference_to_illegal_address (0x40004)
 *   - *entry_out = NULL (if PTE was invalid)
 *
 * Parameters:
 *   asid      - Address Space ID
 *   va        - Virtual address to look up
 *   prot_out  - Output: protection class (0-31)
 *   entry_out - Output: pointer to the PTE
 *   status    - Output: status code
 *
 * Original address: 0x00E4411C
 * Size: 174 bytes
 */

#include "mst/mst_internal.h"
#include "ml/ml.h"

void mst_$va_to_pte(uint16_t asid, uint32_t va, uint16_t *prot_out,
                    void **entry_out, status_$t *status)
{
    uint16_t segno;
    uint16_t sub_index;
    uint16_t result;
    int16_t asid_base;
    int16_t dir_index;
    uint16_t page_entry;
    uint32_t pte_addr;
    uint8_t *pte;

    /* Convert VA to segment number */
    uint16_t local_seg;
    result = MST_$VA_TO_SEGNO(va, &local_seg, asid);

    /* Validate segment number (must be < 0x3A = 58) */
    if (result > 0x39) {
        *status = status_$reference_to_illegal_address;
        return;
    }

    /* Look up ASID base from the ASID base table */
    asid_base = *(int16_t *)((char *)&MST_ASID_BASE + (int16_t)(result * 2));

    /* Compute page directory index */
    dir_index = (asid_base + (local_seg >> 6)) * 2;

    /* Check if page table page exists */
    page_entry = *(uint16_t *)((char *)&MST + dir_index);
    if (page_entry == 0) {
        *status = status_$reference_to_illegal_address;
        return;
    }

    /* Compute PTE address:
     * page_table_base = page_entry * 0x400
     * sub_offset = (local_seg & 0x3F) * 16
     * PTE is at MST_PAGE_TABLE_BASE + page_table_base + sub_offset
     * But entry_out points to -0x400 from that (the actual entry start)
     */
    pte_addr = (uint32_t)page_entry * 0x400 +
               (int16_t)((local_seg & 0x3F) << 4);
    pte = (uint8_t *)(pte_addr + 0xEF6000);

    /* Store entry pointer (at -0x400 offset from computed address) */
    *entry_out = (void *)(pte - 0x400);

    /* Check if PTE is valid */
    if (*(uint32_t *)(pte - 0x400) == 0) {
        *status = status_$reference_to_illegal_address;
        *entry_out = NULL;
        return;
    }

    /* Extract protection bits: (byte[0x0A] & 0x3E) >> 1 */
    *prot_out = (uint16_t)((pte[0x0A - 0x400] & 0x3E) >> 1);
    *status = status_$ok;
}
