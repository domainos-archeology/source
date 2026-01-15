/*
 * ast_$allocate_pages - Allocate physical pages for segment mapping
 *
 * Allocates pages from the free pool and pure pool. Updates page
 * attributes and wakes the purifier if memory is low.
 *
 * Parameters:
 *   count_flags - High word is minimum count, low word is flags (1 = from pure pool OK)
 *   ppn_array - Array to receive allocated page numbers
 *
 * Returns: Number of pages actually allocated
 *
 * Original address: 0x00e00d46
 */

#include "ast/ast_internal.h"

/* External function prototypes */

/* External variables */

/* Local netlog function */
static void FUN_00e00cac(void *pmape, int16_t ppn_high);

/* Error status */

/*
 * Allocation stats at A5+0x460 and A5+0x464
 */
#if defined(M68K)
#define AST_$ALLOC_FAIL_CNT (*(uint32_t *)0xE1E0E0)  /* A5+0x460 */
#define AST_$ALLOC_TRY_CNT  (*(uint32_t *)0xE1E0E4)  /* A5+0x464 */
#else
#define AST_$ALLOC_FAIL_CNT ast_$alloc_fail_cnt
#define AST_$ALLOC_TRY_CNT  ast_$alloc_try_cnt
#endif

int16_t ast_$allocate_pages(uint32_t count_flags, uint32_t *ppn_array)
{
    uint16_t num_pages;
    uint16_t min_count;
    uint16_t allocated;
    uint16_t count;
    int16_t i;
    uint32_t ppn;
    uint32_t pmape_offset;
    uint16_t seg_index;

    /* Increment allocation attempt counter */
    AST_$ALLOC_TRY_CNT++;

    allocated = 0;
    num_pages = (uint16_t)count_flags;           /* Low word - requested count */
    min_count = (uint16_t)(count_flags >> 16);   /* High word - minimum required */

    while (1) {
        /* First try to allocate from free pool */
        count = MMAP_$ALLOC_FREE(ppn_array, num_pages);
        if (count != 0) {
            allocated += count;
            num_pages -= count;
            if (num_pages == 0) {
                goto done;
            }
            ppn_array += count;
        }

        /* Then try to allocate from pure pool */
        count = MMAP_$ALLOC_PURE(ppn_array, num_pages);
        num_pages -= count;

        if (count != 0) {
            /* Process each pure page - update PMAPE and segment map */
            i = count - 1;
            do {
                ppn = *ppn_array;
                pmape_offset = ppn * 16;  /* PMAPE entries are 16 bytes */

                /* Get segment index from PMAPE (offset 2) */
                seg_index = *(uint16_t *)(PMAPE_BASE + pmape_offset + 2);

                /* Calculate segment map position */
                uint32_t segmap_offset = ((uint32_t)(*(uint8_t *)(PMAPE_BASE + pmape_offset + 1) << 2) +
                                         (uint32_t)seg_index * 0x80);
                uint16_t *segmap_entry = (uint16_t *)(0xED5000 + segmap_offset - 0x80);

                /* Verify PMAPE matches segment map entry */
                uint32_t entry_ppn = *(uint16_t *)(segmap_entry + 1);  /* Low word */
                if (entry_ppn != ppn ||
                    (int16_t)segmap_entry[0] < 0 ||  /* In transition? */
                    (segmap_entry[0] & 0x4000) == 0 ||  /* Not in use? */
                    (segmap_entry[0] & 0x2000) != 0) {  /* Wired? */
                    CRASH_SYSTEM(&OS_PMAP_mismatch_err);
                }

                /* Clear the in-use flag (0x40) in high byte */
                *(uint8_t *)segmap_entry &= 0xBF;

                /* Clear disk address in segment map entry */
                *(uint32_t *)segmap_entry &= 0xFF800000;

                /* Copy disk address from PMAPE (offset 0x0C) */
                *(uint32_t *)segmap_entry |= *(uint32_t *)(PMAPE_BASE + pmape_offset + 0x0C);

                /* Decrement ASTE page count */
                /* ASTE table at 0xEC5400, entry size 0x14 (20 bytes) */
                char *aste_entry = (char *)(0xEC5400 + (uint32_t)seg_index * 0x14 - 4);
                (*aste_entry)--;

                /* Log if enabled */
                if (NETLOG_$OK_TO_LOG < 0) {
                    FUN_00e00cac((void *)(PMAPE_BASE + pmape_offset), (int16_t)(ppn >> 16));
                }

                allocated++;
                ppn_array++;
                i--;
            } while (i != -1);
        }

        /* Check if we have enough pages */
        if (allocated >= min_count) {
            break;
        }

        /* Wake purifier to free more pages */
        PMAP_$WAKE_PURIFIER(0xFF);
    }

    /* Track allocation failures */
    if (num_pages != 0) {
        AST_$ALLOC_FAIL_CNT++;
    }

done:
    /* Check if memory is low and wake purifier if needed */
    if ((DAT_00e232b4 + DAT_00e232d8 + DAT_00e232fc) < (uint32_t)PMAP_$LOW_THRESH) {
        PMAP_$WAKE_PURIFIER(0);
    }

    return (int16_t)allocated;
}

/* Stub for netlog function - to be implemented */
static void FUN_00e00cac(void *pmape, int16_t ppn_high)
{
    /* TODO: Implement netlog call */
    (void)pmape;
    (void)ppn_high;
}
