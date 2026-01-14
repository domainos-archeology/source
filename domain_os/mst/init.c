/*
 * MST_$INIT - Full MST subsystem initialization
 *
 * This function performs complete initialization of the Memory Segment Table
 * subsystem. It is called during boot after basic memory management is
 * available but before process creation begins.
 *
 * Initialization steps:
 * 1. Set default touch-ahead count
 * 2. Clear ASID allocation bitmap
 * 3. Allocate physical pages for MST segment table
 * 4. Initialize all MST entries to zero
 * 5. Allocate pages for global segment table regions
 * 6. Calculate and set MST page limits
 * 7. Initialize MST page availability bitmap
 */

#include "mst.h"

/* External functions */
extern uint32_t M_MIU_LLW(uint32_t dividend, uint16_t divisor);  /* Unsigned divide */
extern uint32_t M_DIU_LLW(int32_t dividend, int32_t divisor);     /* Signed divide */
extern uint16_t M_OIS_WLW(int32_t value, uint16_t divisor);       /* Modulo operation */
extern void CRASH_SYSTEM(status_$t *status);

/* External: Memory map functions */
extern int16_t MMAP_$ALLOC_FREE(uint32_t *phys_addr_out, int16_t count);
extern void MMU_$INSTALL(uint32_t phys_addr, int32_t virt_addr, uint16_t flags);

/* External: Memory statistics */
extern int32_t MMAP_$REAL_PAGES;
extern int32_t MMAP_$PAGEABLE_PAGES_LOWER_LIMIT;
extern int8_t M68020;

/* MST page tracking globals */
extern uint32_t MST_$ASID_LIST_LONG;    /* First 32 bits of ASID bitmap */
extern uint32_t DAT_00e24388;           /* Second part of ASID list */
extern uint16_t MST_$MST_PAGES_WIRED;
extern uint16_t MST_$MST_PAGES_LIMIT;

/* MST page bitmap - tracks available MST pages */
extern uint32_t DAT_00e7cf0c[];         /* Bitmap of available MST pages */
extern uint8_t DAT_00e7cf0f;            /* Flags byte in page bitmap */

/* Error status for resource exhaustion */
extern status_$t PMAP_VM_Resources_exhausted_err;

/*
 * Static helper: Initialize an MST page (nested Pascal procedure)
 *
 * Allocates a physical page and maps it at the given virtual address,
 * then clears the page to zeros.
 *
 * @param virt_addr  Virtual address to map the page
 * @return Physical address of the allocated page
 */
static uint32_t mst_init_page(int32_t virt_addr)
{
    uint32_t phys_addr[3];  /* Buffer for MMAP_$ALLOC_FREE */
    int16_t pages_allocated;
    int16_t i;

    /* Allocate one physical page */
    pages_allocated = MMAP_$ALLOC_FREE(phys_addr, 1);
    if (pages_allocated == 0) {
        CRASH_SYSTEM(&PMAP_VM_Resources_exhausted_err);
    }

    /* Map the physical page at the virtual address */
    /* Flag 0x16 = kernel, read/write, cached */
    MMU_$INSTALL(phys_addr[0], virt_addr, 0x16);

    /* Clear the entire page (256 longwords = 1024 bytes) */
    uint32_t *ptr = (uint32_t *)(uintptr_t)virt_addr;
    for (i = 0; i < 256; i++) {
        *ptr++ = 0;
    }

    return phys_addr[0];
}

/*
 * Static helper: Initialize a global segment table page
 *
 * Called during init to set up pages for global segment mappings.
 * Updates the MST table entry and page tracking bitmap.
 *
 * @param seg_index  Index in global segment table
 *
 * Note: This function accesses parent's local variables via A6 register
 * chain, which is a Pascal nested procedure pattern. We simulate this
 * by using static variables set by the caller.
 */

/* Shared state between MST_$INIT and mst_init_global_page */
static int32_t init_virt_addr;
static int16_t init_bit_index;
static int16_t init_word_index;

static void mst_init_global_page(int16_t seg_index)
{
    uint16_t page_num;

    /* Allocate and clear the page */
    mst_init_page(init_virt_addr);

    /* Increment wired page count */
    MST_$MST_PAGES_WIRED++;

    /* Calculate MST table entry value */
    page_num = init_bit_index + init_word_index * 32;

    /* Store in MST table */
    MST[seg_index] = page_num;

    /* Clear bit in availability bitmap to mark page as used */
    DAT_00e7cf0c[init_word_index] &= ~(1 << (init_bit_index & 0x1f));

    /* Advance to next page */
    if (DAT_00e7cf0c[init_word_index] == 0) {
        /* All bits in this word are used, move to next word */
        init_word_index++;
        init_bit_index = 0;
    } else {
        init_bit_index++;
    }

    /* Move virtual address to next page (1KB pages) */
    init_virt_addr += 0x400;
}

/*
 * MST_$INIT - Initialize the MST subsystem
 */
void MST_$INIT(void)
{
    uint32_t num_mst_pages;
    uint32_t max_pages;
    int16_t limit;
    int16_t i;
    uint16_t global_a_pages;
    uint16_t global_b_pages;
    uint16_t word_index;
    uint16_t bit_index;
    int32_t mst_table_addr;
    int32_t page_table_addr;

    /* Set default touch-ahead count to 4 pages */
    MST_$TOUCH_COUNT = 4;

    /* Clear ASID allocation bitmap */
    MST_$ASID_LIST_LONG = 0;
    DAT_00e24388 = 1;  /* Mark ASID 0 as allocated (reserved for kernel) */

    /* Calculate number of MST pages needed */
    /* Each page covers 64 segments (1024 bytes / 16 bytes per entry) */
    num_mst_pages = M_MIU_LLW((uint32_t)(MST_$SEG_TN + 0x3f), MST_MAX_ASIDS);

    /* Allocate and initialize MST table pages */
    mst_table_addr = (int32_t)(uintptr_t)MST;  /* 0xee5800 */
    page_table_addr = mst_table_addr;

    do {
        mst_init_page(page_table_addr);
        page_table_addr += 0x400;  /* Next 1KB page */
    } while (page_table_addr < mst_table_addr + (int32_t)(num_mst_pages * 2));

    /* Clear all MST table entries */
    for (i = (int16_t)num_mst_pages - 1; i >= 0; i--) {
        MST[i] = 0;
    }

    /* Clear flags in page bitmap */
    DAT_00e7cf0f &= 0xfe;

    /* Initialize page table base address */
    page_table_addr = MST_PAGE_TABLE_BASE;

    /* Clear wired page count */
    MST_$MST_PAGES_WIRED = 0;

    /* Initialize shared state for helper function */
    init_virt_addr = page_table_addr;
    init_bit_index = 0;
    init_word_index = 1;

    /*
     * Allocate pages for Global A segment table
     * Number of pages = GLOBAL_A_SIZE / 64 (64 segments per page)
     */
    global_a_pages = (MST_$GLOBAL_A_SIZE >> 6) - 1;
    if (global_a_pages >= 0) {
        for (i = 0; i <= global_a_pages; i++) {
            mst_init_global_page(i);
        }
    }

    /*
     * Allocate pages for Global B segment table
     * Starts after Global A entries
     */
    global_b_pages = (uint16_t)((MST_$GLOBAL_A_SIZE + MST_$GLOBAL_B_SIZE) >> 6) - 1;
    global_b_pages -= (MST_$GLOBAL_A_SIZE >> 6);
    if (global_b_pages >= 0) {
        uint16_t start_index = MST_$GLOBAL_A_SIZE >> 6;
        for (i = 0; i <= global_b_pages; i++) {
            mst_init_global_page(start_index + i);
        }
    }

    /*
     * Calculate MST page limit based on available physical memory.
     * Use 10% of real pages, capped between 125 (0x7d) and 358 (0x166) pages.
     */
    int32_t page_base;
    if (M68020 >= 0) {
        page_base = MMAP_$PAGEABLE_PAGES_LOWER_LIMIT;
    } else {
        page_base = MMAP_$REAL_PAGES;
    }

    /* Calculate 10% of pages */
    max_pages = M_DIU_LLW(page_base * 10, 100);

    /* Also limit by segment count */
    num_mst_pages = M_MIU_LLW((uint32_t)(MST_$SEG_TN >> 6), MST_MAX_ASIDS);
    if (max_pages > num_mst_pages) {
        max_pages = num_mst_pages;
    }

    /* Apply minimum limit of 125 pages */
    limit = 0x7d;
    if ((int16_t)max_pages > limit) {
        limit = (int16_t)max_pages;
    }

    /* Apply maximum limit of 358 pages */
    if (limit > 0x166) {
        limit = 0x166;
    }

    /* Round down to multiple of 32 */
    if (limit < 0) {
        limit += 0x1f;
    }
    MST_$MST_PAGES_LIMIT = (limit >> 5) << 5;

    /* Clear DAT_00e7cf3c (offset 0x30 from DAT_00e7cf0c) */
    DAT_00e7cf0c[12] = 0;  /* word index 12 = offset 0x30 */

    /*
     * Initialize remaining bits in page availability bitmap.
     * Mark pages beyond the limit as unavailable.
     */
    int32_t limit_plus_one = (int16_t)MST_$MST_PAGES_LIMIT + 1;
    word_index = (limit_plus_one < 0 ? limit_plus_one + 0x1f : limit_plus_one) >> 5;
    bit_index = M_OIS_WLW(limit_plus_one, 32);

    /* Clear bits from bit_index to 31 in current word */
    for (i = 31 - bit_index; i >= 0; i--) {
        DAT_00e7cf0c[word_index] &= ~(1 << (bit_index & 0x1f));
        bit_index++;
    }

    /* Clear remaining words in bitmap */
    word_index++;
    for (i = 11 - word_index; i >= 0; i--) {
        DAT_00e7cf0c[word_index] = 0;
        word_index++;
    }
}
