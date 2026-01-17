/*
 * AREA_$INIT - Initialize the area subsystem
 *
 * This function initializes the area management subsystem during system boot.
 * It sets up the area table, free lists, per-ASID area lists, UID hash table,
 * and diskless node support structures.
 *
 * Original address: 0x00E2F3A8
 */

#include "area/area_internal.h"
#include "misc/crash_system.h"

/*
 * Module-local data at AREA_GLOBALS_BASE (0xE1E118)
 * These are accessed via the A5 register in the original code.
 *
 * Offset layout from base:
 *   +0x000: First area entry (inline data)
 *   +0x048: AREA_$IN_TRANS_EC (event count for in-transition waits)
 *   +0x068: Per-ASID extended segment table list (11 entries)
 *   +0x0E8: Start of inline bitmap/page data
 *   +0x450: UID hash table free list head
 *   +0x454: UID hash table buckets (11 entries, indexed by hash)
 *   +0x480: UID hash table entry pool
 *   +0x4D8: Per-ASID area list heads (58 entries)
 *   +0x5C4: Area ID counter
 *   +0x5C8: AREA_$FREE_LIST
 *   +0x5CC: AREA_$PARTNER
 *   +0x5D0: Mother node ID (for diskless)
 *   +0x5D6: Diskless page allocation base (0x540)
 *   +0x5D8: Reserved
 *   +0x5DA: Reserved
 *   +0x5DC: AREA_$DEL_DUP
 *   +0x5DE: AREA_$CR_DUP
 *   +0x5E0: AREA_$N_FREE
 *   +0x5E2: AREA_$N_AREAS
 *   +0x5E4: AREA_$PARTNER_PKT_SIZE
 */

/* Number of per-ASID list slots */
#define ASID_LIST_COUNT     58

/* Number of UID hash buckets */
#define UID_HASH_BUCKETS    11

/* Number of UID hash entries in pool */
#define UID_HASH_POOL_SIZE  11

/* Number of diskless area slots (for nodes booting over network) */
#define DISKLESS_AREA_COUNT 3

/* Diskless area base virtual address */
#define DISKLESS_VA_BASE    0x00EE5000

/* Bitmap array for tracking area segment allocation */
#define AREA_BITMAP_COUNT   58

/* Extended segment table count */
#define SEG_TABLE_COUNT     64

/*
 * AREA_$INIT - Initialize the area subsystem
 *
 * Assembly analysis:
 * - Clears per-ASID area lists and segment table lists
 * - Initializes UID hash table with free pool
 * - For diskless nodes, allocates wired pages for area backing
 * - Clears segment bitmap tracking array
 * - Initializes duplicate operation counters
 */
void AREA_$INIT(void)
{
    int i;
    status_$t status;
    uint32_t *globals = (uint32_t *)AREA_GLOBALS_BASE;

    /*
     * Set diskless page allocation offset (0x540)
     * Offset +0x5D6 from base = (0x5D6 / 4) = entry [0x175]
     * This is stored as a 16-bit value at offset 0x5D6
     */
    *(int16_t *)((char *)globals + 0x5D6) = 0x540;

    /* Clear free list, area count, and N_FREE */
    AREA_$FREE_LIST = NULL;     /* +0x5C8 */
    AREA_$N_AREAS = 0;          /* +0x5E2 */
    AREA_$N_FREE = 0;           /* +0x5E0 */

    /* Clear area ID counter at +0x5C4 */
    *(uint32_t *)((char *)globals + 0x5C4) = 0;

    /*
     * Clear per-ASID area lists (+0x4D8) and segment table lists (+0x68)
     * Loop count: 0x39 + 1 = 58 iterations
     */
    for (i = 0; i < ASID_LIST_COUNT; i++) {
        /* Clear per-ASID area list head at +0x4D8 + i*4 */
        ((uint32_t *)((char *)globals + 0x4D8))[i] = 0;
        /* Clear per-ASID segment table list at +0x68 + i*4 */
        ((uint32_t *)((char *)globals + 0x68))[i] = 0;
    }

    /*
     * Initialize UID hash table
     * - Clear per-bucket entry counts at +0x454 + i*4
     * - Link hash pool entries (+0x480) into buckets (+0x488...)
     * Loop count: 0xA + 1 = 11 iterations
     */
    for (i = 0; i < UID_HASH_BUCKETS; i++) {
        /* Clear hash bucket entry count */
        ((uint32_t *)((char *)globals + 0x454))[i] = 0;
        /* Link pool entry to next pool entry */
        ((uint32_t *)((char *)globals + 0x480))[i * 2] =
            (uint32_t)((char *)globals + 0x488 + i * 8);
    }

    /* Set up hash table entry pool at +0x4D0 (initially empty) */
    *(uint32_t *)((char *)globals + 0x4D0) = 0;

    /* Set free list head to first pool entry */
    *(uint32_t *)((char *)globals + 0x450) = (uint32_t)((char *)globals + 0x480);

    /* Clear partner pointer */
    AREA_$PARTNER = NULL;       /* +0x5CC */

    /*
     * Set mother node ID for diskless nodes
     * If NETWORK_$DISKLESS < 0, we're a diskless node - use mother node
     * Otherwise, set to 0 (local node)
     */
    if (NETWORK_$DISKLESS < 0) {
        *(uint32_t *)((char *)globals + 0x5D0) = NETWORK_$MOTHER_NODE;
    } else {
        *(uint32_t *)((char *)globals + 0x5D0) = 0;
    }

    /*
     * For diskless nodes, allocate wired pages for area backing
     * Allocates 3 pages at VA 0xEE4C00, 0xEE5000, 0xEE5400
     */
    if (NETWORK_$DISKLESS < 0) {
        uint32_t va = DISKLESS_VA_BASE;
        uint8_t *init_ptr = (uint8_t *)globals + 0x0C;  /* Initialization data */

        for (i = 0; i < DISKLESS_AREA_COUNT; i++) {
            uint32_t page_ptr;
            uint32_t page_va = va - 0x400;

            /* Allocate a wired page */
            WP_$CALLOC(&page_ptr, &status);
            if (status != status_$ok) {
                CRASH_SYSTEM(&status);
            }

            /* Store page pointer */
            *(uint32_t *)(init_ptr + i * 4 - 4) = page_ptr;

            /* Install page in MMU with flags 0x16 */
            MMU_$INSTALL(page_ptr, page_va, 0x16);

            /* Initialize diskless area entry data */
            uint8_t *entry_ptr = init_ptr + i * 0x0C;
            *(uint32_t *)(entry_ptr + 4) = 0;   /* Clear field */
            *(int16_t *)(entry_ptr + 8) = 0;    /* Clear field */
            *(int16_t *)(entry_ptr + 10) = -1;  /* Set to -1 (0xFFFF) */
            *(uint8_t *)(entry_ptr + 12) = 0;   /* Clear byte */
            *(uint8_t *)(entry_ptr + 13) = 0;   /* Clear byte */

            va += 0x400;
        }
    }

    /* Clear reserved words at +0x5DA and +0x5D8 */
    *(int16_t *)((char *)globals + 0x5DA) = 0;
    *(int16_t *)((char *)globals + 0x5D8) = 0;

    /*
     * Clear segment tracking bitmap
     * Loop count: 0x3F + 1 = 64 iterations
     * Each entry is 12 bytes apart, clearing byte at offset +0x153
     */
    for (i = 0; i < SEG_TABLE_COUNT; i++) {
        *((uint8_t *)globals + 0x153 + i * 12) = 0;
    }

    /* Clear duplicate operation counters */
    AREA_$CR_DUP = 0;           /* +0x5DE */
    AREA_$DEL_DUP = 0;          /* +0x5DC */
}
