/*
 * PMAP_$FLUSH - Flush dirty pages in a segment to disk
 *
 * Scans a segment's page map and flushes any dirty pages to disk.
 * This is the core function for writing modified pages back to storage.
 *
 * Parameters:
 *   aste       - Pointer to Active Segment Table Entry
 *   segmap     - Pointer to segment map
 *   start_page - Starting page number
 *   count      - Number of pages to scan
 *   flags      - Flush flags:
 *                bit 0: Remove from MMU after flush
 *                bit 1: Skip actual write (just clear dirty bits)
 *                bit 2: Force synchronous write
 *   status     - Pointer to status return value
 *
 * Returns:
 *   Number of pages flushed
 *
 * Original address: 0x00e1376c
 */

#include "pmap/pmap_internal.h"
#include "misc/misc.h"

/* PMAPE base addresses */
#if defined(M68K)
    #define PMAPE_BASE          0xEB2800
    #define MMU_PTE_BASE        0xFFB800
#else
    extern uint8_t pmape_base[];
    extern uint8_t mmu_pte_base[];
    #define PMAPE_BASE          ((uintptr_t)pmape_base)
    #define MMU_PTE_BASE        ((uintptr_t)mmu_pte_base)
#endif

/* PMAPE structure offsets within 16-byte entries */
#define PMAPE_LOCK_OFFSET       0x00
#define PMAPE_PAGE_IDX_OFFSET   0x01
#define PMAPE_SEG_IDX_OFFSET    0x02
#define PMAPE_STATE_OFFSET      0x04
#define PMAPE_FLAGS_OFFSET      0x09

int16_t PMAP_$FLUSH(struct aste_t *aste, uint32_t *segmap, uint16_t start_page,
                    int16_t count, uint16_t flags, status_$t *status)
{
    uint16_t *segmap_ptr;
    uint16_t page_idx;
    int16_t pages_to_scan;
    int16_t pages_flushed = 0;
    int16_t batch_count = 0;
    uint32_t vpn;
    int pmape_offset;
    int8_t is_remote;
    int8_t any_invalid;
    int8_t any_dirty;
    int8_t modified_bit;
    int8_t did_write;
    uint32_t batch_vpns[16];

    *status = 0;

    /* Check ASTE flags for remote segment */
    is_remote = -((*(uint16_t *)((char *)aste + 0x12) & 0x800) != 0);

    any_dirty = 0;

    ML_$LOCK(PMAP_LOCK_ID);

    /* Main loop - continues while segment is active */
    while (*(char *)((char *)aste + 0x10) != '\0') {
        any_invalid = 0;
        did_write = 0;

        /* Scan pages if count > 0 */
        pages_to_scan = count - 1;
        if (pages_to_scan >= 0) {
            segmap_ptr = (uint16_t *)((char *)segmap + (int16_t)(start_page << 2));
            page_idx = start_page;

            do {
                /* Check if page is valid (bit 15 set = invalid) */
                if ((int16_t)*segmap_ptr < 0) {
                    any_invalid = -1;
                }
                /* Check if page is dirty (bit 14 set) */
                else if ((*segmap_ptr & 0x4000) != 0 &&
                        (PMAP_$SHUTTING_DOWN_FLAG < 0 ||
                         (segmap_ptr[1] >= 0x200 && segmap_ptr[1] < 0x1000))) {

                    vpn = (uint32_t)segmap_ptr[1];
                    pmape_offset = vpn * 0x10;

                    /* Validate page is in valid range and locked */
                    if (vpn < 0x200 || vpn > 0xFFF ||
                        *(char *)(PMAPE_BASE + pmape_offset) != '\0') {
                        /* Flush any pending batch */
                        if (batch_count > 0) {
                            FUN_00e1360c();
                        }
                        *status = 0x50007;  /* Error: invalid page */
                        goto done;
                    }

                    /* Verify page index matches */
                    if (*(uint8_t *)(PMAPE_BASE + pmape_offset + PMAPE_PAGE_IDX_OFFSET) != page_idx) {
                        CRASH_SYSTEM(&status_$t_00e13a14);
                    }

                    /* Remove page from available pool */
                    MMAP_$UNAVAIL_REMOV(vpn);

                    /* Optionally remove from MMU */
                    if ((flags & 1) != 0 && (*segmap_ptr & 0x2000) != 0) {
                        *(uint8_t *)segmap_ptr &= 0xDF;
                        MMU_$REMOVE(vpn);
                    }

                    /* Check MMU dirty bit */
                    int pte_offset = vpn * 4;
                    if ((*(uint16_t *)(MMU_PTE_BASE + pte_offset + 2) & 0x4000) == 0 &&
                        (*(uint8_t *)(PMAPE_BASE + pmape_offset + PMAPE_FLAGS_OFFSET) & 0x40) == 0) {
                        modified_bit = 0;
                    } else {
                        /* Page was modified */
                        if ((*(uint16_t *)(MMU_PTE_BASE + pte_offset + 2) & 0x4000) != 0) {
                            any_dirty = -1;
                        }
                        /* Clear MMU dirty bit */
                        *(uint16_t *)(MMU_PTE_BASE + pte_offset + 2) &= 0xBFFF;
                        modified_bit = -1;
                        /* Clear PMAPE modified flag */
                        *(uint8_t *)(PMAPE_BASE + pmape_offset + PMAPE_FLAGS_OFFSET) &= 0xBF;

                        /* If not skipping writes */
                        if ((flags & 2) == 0) {
                            did_write = -1;
                            pages_flushed++;

                            if (is_remote < 0) {
                                /* Remote write - synchronous */
                                FUN_00e12e5e(vpn, status, -((flags & 4) == 0));
                                if (*status != 0) goto done;
                                FUN_00e1359c(segmap_ptr, vpn, page_idx);
                            } else {
                                /* Local write - batch it */
                                *(uint8_t *)segmap_ptr |= 0x80;
                                batch_count++;
                                batch_vpns[batch_count] = vpn;

                                /* Flush batch if full (16 pages) */
                                if (batch_count == 0x10) {
                                    FUN_00e1360c();
                                    if (*status != 0) goto done;
                                }
                            }
                        }
                    }

                    /* Release page if not modified or skipping writes */
                    if (modified_bit >= 0 || (flags & 2) != 0) {
                        FUN_00e1359c(segmap_ptr, vpn, page_idx);
                    }
                }

                page_idx++;
                segmap_ptr += 2;
                pages_to_scan--;
            } while (pages_to_scan != -1);
        }

        /* Flush any remaining batch */
        if (batch_count > 0) {
            FUN_00e1360c();
            if (*status != 0) break;
        }

        /* Check if we found invalid pages */
        if (any_invalid >= 0) break;

        /* Call cleanup if no writes performed */
        if (did_write >= 0) {
            FUN_00e12d38();
        }
    }

done:
    /* Update timestamps if pages were modified */
    if (any_dirty < 0 && is_remote >= 0 &&
        ((*(uint16_t *)((char *)aste + 0x12) & 0x1000) == 0)) {
        struct aote_t *aote = *(struct aote_t **)((char *)aste + 4);
        TIME_$ABS_CLOCK((clock_t *)((char *)aote + 0x38));
        TIME_$CLOCK((clock_t *)((char *)aote + 0x28));
        *(uint32_t *)((char *)aote + 0x40) = *(uint32_t *)((char *)aote + 0x28);
        *(uint16_t *)((char *)aote + 0x44) = *(uint16_t *)((char *)aote + 0x2c);
        *(uint8_t *)((char *)aote + 0xBF) |= 0x20;
    }

    ML_$UNLOCK(PMAP_LOCK_ID);

    return pages_flushed;
}
