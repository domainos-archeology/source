/*
 * area/test/test_init.c - AREA_$INIT (0x00E2F3A8)
 *
 * The regression this file exists for (source-vm49): the diskless loop at
 * 0x00E2F44A-0x00E2F4C2 sets A2 = globals+4 and hands WP_$CALLOC
 * `pea (-0x4,A2)`, so the three page cells it fills are globals+0x00, +0x04
 * and +0x08 - and MMU_$INSTALL reads each one straight back with
 * `move.l (-0x4,A2),-(SP)`.  The old code wrote init_ptr + i*4 - 4 where
 * init_ptr was globals+0x0C, i.e. +0x08/+0x0C/+0x10.
 *
 * Also covered: the loop trip counts, the RPMAP window VAs, the 0xFFFF
 * "empty" marker, the UID-hash free list and its nil terminator, and the
 * AREA_$FORMAT words.
 */

#include <stdio.h>
#include <string.h>

#include "area/area_internal.h"

/* AREA_RPMAP_CACHE_VA and AREA_PIT_PAGES_VA are ARCH_PTR_TO_VA of the
 * sau2.ld symbols AREA_$RPMAP_CACHE and PIT_PAGES, past OS_PAGE_END since
 * source-o7s2 (docs/rfc-cold-start.md section 8c); this test stands in for
 * the link with the map's values (os/test/test_vm_tables.c checks the
 * macros themselves). */
#undef AREA_RPMAP_CACHE_VA
#define AREA_RPMAP_CACHE_VA 0x00EE4C00u
#undef AREA_PIT_PAGES_VA
#define AREA_PIT_PAGES_VA 0x00EE6400u

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-48s ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long long _e = (unsigned long long)(expected); \
    unsigned long long _a = (unsigned long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_PTR_EQ(expected, actual) do { \
    const void *_e = (const void *)(expected); \
    const void *_a = (const void *)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: %p, Got: %p at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ==========================================================================
 * Module globals and mocked callees
 * ========================================================================== */

area_$globals_t AREA_$GLOBALS;

int8_t   NETWORK_$DISKLESS = 0;
uint32_t NETWORK_$MOTHER_NODE = 0;

#define MAX_CALLS 8

static int      calloc_calls;
static uint32_t *calloc_cell[MAX_CALLS];
static status_$t calloc_status;

static int      install_calls;
static uint32_t install_page[MAX_CALLS];
static uint32_t install_va[MAX_CALLS];
static uint32_t install_flags[MAX_CALLS];

static int      crash_calls;

void WP_$CALLOC(uint32_t *page_out, status_$t *status)
{
    if (calloc_calls < MAX_CALLS) {
        calloc_cell[calloc_calls] = page_out;
    }
    /* Give each page a recognisable, non-zero number. */
    *page_out = 0x00A00000u + (uint32_t)calloc_calls;
    *status = calloc_status;
    calloc_calls++;
}

void MMU_$INSTALL(uint32_t page, uint32_t va, uint32_t flags)
{
    if (install_calls < MAX_CALLS) {
        install_page[install_calls]  = page;
        install_va[install_calls]    = va;
        install_flags[install_calls] = flags;
    }
    install_calls++;
}

void CRASH_SYSTEM(const status_$t *status)
{
    (void)status;
    crash_calls++;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../init.c"

static void reset(void)
{
    memset(&AREA_$GLOBALS, 0xA5, sizeof(AREA_$GLOBALS));
    calloc_calls = 0;
    memset(calloc_cell, 0, sizeof(calloc_cell));
    calloc_status = status_$ok;
    install_calls = 0;
    memset(install_page, 0, sizeof(install_page));
    memset(install_va, 0, sizeof(install_va));
    memset(install_flags, 0, sizeof(install_flags));
    crash_calls = 0;
    NETWORK_$DISKLESS = 0;
    NETWORK_$MOTHER_NODE = 0;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * The regression: WP_$CALLOC is handed globals+0x00, +0x04, +0x08 - i.e.
 * &rpmap_page[0..2] - not globals+0x08/+0x0C/+0x10.
 */
TEST(diskless_page_cells_are_the_first_three_longwords)
{
    reset();
    NETWORK_$DISKLESS = -1;
    AREA_$INIT();

    ASSERT_EQ(AREA_DISKLESS_PAGE_COUNT, calloc_calls);
    ASSERT_PTR_EQ(&AREA_$GLOBALS.rpmap_page[0], calloc_cell[0]);
    ASSERT_PTR_EQ(&AREA_$GLOBALS.rpmap_page[1], calloc_cell[1]);
    ASSERT_PTR_EQ(&AREA_$GLOBALS.rpmap_page[2], calloc_cell[2]);

    /* the cells are the very start of the block */
    ASSERT_PTR_EQ(&AREA_$GLOBALS, calloc_cell[0]);
    ASSERT_EQ(4, (const char *)calloc_cell[1] - (const char *)calloc_cell[0]);
    ASSERT_EQ(8, (const char *)calloc_cell[2] - (const char *)calloc_cell[0]);

    /* and they keep the page numbers WP_$CALLOC wrote */
    ASSERT_EQ(0x00A00000u, AREA_$GLOBALS.rpmap_page[0]);
    ASSERT_EQ(0x00A00001u, AREA_$GLOBALS.rpmap_page[1]);
    ASSERT_EQ(0x00A00002u, AREA_$GLOBALS.rpmap_page[2]);
}

/* 0x00E2F490: MMU_$INSTALL re-reads the cell WP_$CALLOC just filled. */
TEST(mmu_install_gets_the_page_from_the_same_cell)
{
    int i;

    reset();
    NETWORK_$DISKLESS = -1;
    AREA_$INIT();

    ASSERT_EQ(AREA_DISKLESS_PAGE_COUNT, install_calls);
    for (i = 0; i < AREA_DISKLESS_PAGE_COUNT; i++) {
        ASSERT_EQ(AREA_$GLOBALS.rpmap_page[i], install_page[i]);
        ASSERT_EQ(AREA_RPMAP_MMU_FLAGS, install_flags[i]);
    }
}

/* 0x00E2F44E-0x00E2F458 / 0x00E2F482 / 0x00E2F4BA: 0xEE4C00 stepping by 0x400. */
TEST(rpmap_window_virtual_addresses)
{
    reset();
    NETWORK_$DISKLESS = -1;
    AREA_$INIT();

    ASSERT_EQ(0x00EE4C00u, install_va[0]);
    ASSERT_EQ(0x00EE5000u, install_va[1]);
    ASSERT_EQ(0x00EE5400u, install_va[2]);
}

/* 0x00E2F49E-0x00E2F4B4: each cache slot is cleared and marked empty. */
TEST(rpmap_cache_slots_are_marked_empty)
{
    int i;

    reset();
    NETWORK_$DISKLESS = -1;
    AREA_$INIT();

    for (i = 0; i < AREA_DISKLESS_PAGE_COUNT; i++) {
        ASSERT_EQ(0, AREA_$GLOBALS.rpmap_cache[i].seq);
        ASSERT_EQ(0, AREA_$GLOBALS.rpmap_cache[i].volx);
        ASSERT_EQ(0xFFFF, AREA_$GLOBALS.rpmap_cache[i].group);
        ASSERT_EQ(0, AREA_$GLOBALS.rpmap_cache[i].dirty);
        ASSERT_EQ(0, AREA_$GLOBALS.rpmap_cache[i].in_trans);
    }
    /* the trailing two bytes of each slot are never written */
    ASSERT_EQ(0xA5, AREA_$GLOBALS.rpmap_cache[0].reserved_0a[0]);
}

/* 0x00E2F440-0x00E2F446: a node with its own disk skips the whole block. */
TEST(non_diskless_wires_nothing)
{
    reset();
    NETWORK_$DISKLESS = 0;
    AREA_$INIT();

    ASSERT_EQ(0, calloc_calls);
    ASSERT_EQ(0, install_calls);
    ASSERT_EQ(0, AREA_$PARTNER.high);
    ASSERT_EQ(0, AREA_$PARTNER.low);
}

/* 0x00E2F42A-0x00E2F43E */
TEST(diskless_partner_is_the_mother_node)
{
    reset();
    NETWORK_$DISKLESS = -1;
    NETWORK_$MOTHER_NODE = 0x00ABCDEF;
    AREA_$INIT();

    ASSERT_EQ(0, AREA_$PARTNER.high);
    ASSERT_EQ(0x00ABCDEF, AREA_$PARTNER.low);
}

/* 0x00E2F46E-0x00E2F47E: a failed wire crashes the system. */
TEST(calloc_failure_crashes)
{
    reset();
    NETWORK_$DISKLESS = -1;
    calloc_status = status_$area_no_free_resources;
    AREA_$INIT();

    /* the image does not stop: it crashes and keeps going round the loop */
    ASSERT_EQ(AREA_DISKLESS_PAGE_COUNT, crash_calls);
}

/* 0x00E2F3CE: one `dbf` clears both 58-entry arrays. */
TEST(clears_both_58_entry_arrays)
{
    int i;

    reset();
    AREA_$INIT();

    for (i = 0; i < AREA_MAX_ENTRIES; i++) {
        ASSERT_PTR_EQ(NULL, AREA_$ASID_LIST[i]);
        ASSERT_PTR_EQ(NULL, AREA_$GLOBALS.seg_table_list[i]);
    }
}

/* 0x00E2F3EC-0x00E2F422: the 11 pool records are threaded and terminated. */
TEST(uid_hash_free_list)
{
    int i;

    reset();
    AREA_$INIT();

    ASSERT_PTR_EQ(&AREA_$UID_HASH_POOL[0], AREA_$UID_HASH_FREE);
    for (i = 0; i < AREA_UID_HASH_BUCKETS; i++) {
        ASSERT_PTR_EQ(NULL, AREA_$UID_HASH[i]);
    }
    for (i = 0; i < AREA_UID_HASH_BUCKETS - 1; i++) {
        ASSERT_PTR_EQ(&AREA_$UID_HASH_POOL[i + 1], AREA_$UID_HASH_POOL[i].next);
    }
    /* 0x00E2F414 nils the link the loop's last pass wrote */
    ASSERT_PTR_EQ(NULL, AREA_$UID_HASH_POOL[AREA_UID_HASH_BUCKETS - 1].next);
}

/* 0x00E2F3B6 / 0x00E2F4C6-0x00E2F4D2 */
TEST(format_words)
{
    reset();
    AREA_$INIT();

    ASSERT_EQ(0x540, AREA_$FORMAT.max_entries);
    ASSERT_EQ(0, AREA_$FORMAT.seg_table_next);
    ASSERT_EQ(0, AREA_$FORMAT.seg_table_count);
    /* +0x5D4 itself is never written */
    ASSERT_EQ(0xA5A5, AREA_$FORMAT.word_00);
}

/* 0x00E2F4D4-0x00E2F4E2: 64 records, the `allocated` byte of each.  Nothing
 * else in a pool record is touched, so the poison stays put. */
TEST(seg_table_pool_flag_bytes)
{
    int i;

    reset();
    AREA_$INIT();

    for (i = 0; i < AREA_SEG_TABLE_POOL_COUNT; i++) {
        ASSERT_EQ(0, AREA_$GLOBALS.seg_table_pool[i].allocated);
        ASSERT_EQ((int16_t)0xA5A5, AREA_$GLOBALS.seg_table_pool[i].area_id);
        ASSERT_EQ(0xA5, AREA_$GLOBALS.seg_table_pool[i].table_index);
    }
}

/* 0x00E2F3BC-0x00E2F3CC and 0x00E2F4E4-0x00E2F4F0 */
TEST(scalar_cells)
{
    reset();
    AREA_$INIT();

    ASSERT_PTR_EQ(NULL, AREA_$FREE_LIST);
    ASSERT_EQ(0, AREA_$N_AREAS);
    ASSERT_EQ(0, AREA_$N_FREE);
    ASSERT_EQ(0, AREA_$NEXT_CALLER_ID);
    ASSERT_EQ(0, AREA_$CR_DUP);
    ASSERT_EQ(0, AREA_$DEL_DUP);
    /* AREA_$INIT never touches the partner packet size */
    ASSERT_EQ((int16_t)0xA5A5, AREA_$PARTNER_PKT_SIZE);
}

int main(void)
{
    printf("AREA_$INIT tests\n");
    RUN_TEST(diskless_page_cells_are_the_first_three_longwords);
    RUN_TEST(mmu_install_gets_the_page_from_the_same_cell);
    RUN_TEST(rpmap_window_virtual_addresses);
    RUN_TEST(rpmap_cache_slots_are_marked_empty);
    RUN_TEST(non_diskless_wires_nothing);
    RUN_TEST(diskless_partner_is_the_mother_node);
    RUN_TEST(calloc_failure_crashes);
    RUN_TEST(clears_both_58_entry_arrays);
    RUN_TEST(uid_hash_free_list);
    RUN_TEST(format_words);
    RUN_TEST(seg_table_pool_flag_bytes);
    RUN_TEST(scalar_cells);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
