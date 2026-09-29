/*
 * dir/test/test_compact_page_entries.c - Unit tests for dir_$compact_page_entries
 *
 * Tests the directory page compaction function that reclaims space from
 * dead entries.  The DIR block supplies the globals; we set up page buffers
 * with dead entries and verify the compaction shifts data and updates indices correctly.
 *
 * Note: page flags use byte-level operations for portability across
 * endiannesses. The reclaimable flag is bit 5 of byte 0 (0x20), and the
 * dead entry flag is bit 7 of byte 0 (0x80).
 */

/* Suppress POSIX uid_t so base/base.h can define Apollo's uid_t struct */
#define uid_t posix_uid_t
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#undef uid_t

/*
 * The function under test sizes entries with dir_$calc_entry_size, which
 * reads DIR_$NAME_OFFSET_TABLE from the DIR module block DIR_$DATA
 * (dir/dir_data.c; the image's 0xE7FC00: 0, 4, 16, 20, 12, 0, 0, 0 - type 2
 * is a 16-byte file entry header).  Both are compiled in, so the test runs
 * against the real table and the real dir_insert_ctx_t.
 */
#include "dir/dir_internal.h"
#include "../dir_data.c"
#include "../calc_entry_size.c"
#include "../compact_page_entries.c"

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_MEM_EQ(expected, actual, len) do { \
    if (memcmp((expected), (actual), (len)) != 0) { \
        printf("FAILED\n    Memory mismatch at line %d\n", __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)



/*
 * Helper: create a 1KB page buffer (zeroed).
 *
 * Page layout:
 *   0x00: flags byte (bit 5 = 0x20 = reclaimable entries)
 *   0x0E: end-of-index-table offset (int16)
 *   0x10: start-of-free-space offset (int16)
 *   0x12+: index table (2 bytes per entry)
 *   Entries grow downward from 0x400
 */
static uint8_t *create_page(void) {
    uint8_t *page = calloc(1, 0x400);
    return page;
}

/* Helper: write a type-2 file entry at a specific page offset.
 * Type 2: 16-byte header + name_len bytes, aligned to 4.
 * If dead=1, sets byte 0 bit 7 (0x80) to mark as dead.
 * Returns the aligned entry size. */
static uint16_t write_type2_entry(uint8_t *page, int16_t offset,
                                   uint8_t name_len, const char *name,
                                   int dead) {
    uint8_t *entry = page + offset;
    uint16_t size = (uint16_t)((16 + name_len + 3) & ~3);
    memset(entry, 0, size);
    entry[0] = 0x02;
    if (dead) entry[0] |= 0x80;  /* Dead flag: bit 7 of byte 0 */
    entry[1] = name_len;
    memcpy(entry + 16, name, name_len);
    return size;
}

/* ===================================================================
 * Tests
 * =================================================================== */

/*
 * Test: page without reclaimable flag is a no-op
 */
TEST(no_reclaimable_flag) {
    uint8_t *page = create_page();
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_data = page;
    ctx.idx_base = page + 0x12;

    /* No reclaimable flag set (bit 5 of byte 0 clear) */
    page[0] = 0x00;
    *(int16_t *)(page + 0x0E) = 0x12;  /* empty index table */
    *(int16_t *)(page + 0x10) = 0x400; /* no entries */

    dir_$compact_page_entries(&ctx);

    /* Nothing should change */
    ASSERT_EQ(0x00, page[0]);
    ASSERT_EQ(0x400, *(int16_t *)(page + 0x10));

    free(page);
}

/*
 * Test: page with reclaimable flag but no dead entries.
 * The flag should be cleared but entries stay the same.
 */
TEST(no_dead_entries) {
    uint8_t *page = create_page();
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_data = page;
    ctx.idx_base = page + 0x12;

    /* Set reclaimable flag: bit 5 of byte 0 */
    page[0] = 0x20;

    /* One live type-2 entry with 4-byte name "test" */
    uint16_t esize = (16 + 4 + 3) & ~3;  /* = 20 */
    int16_t entry_off = 0x400 - esize;
    write_type2_entry(page, entry_off, 4, "test", 0);

    /* Set up page header */
    *(int16_t *)(page + 0x0E) = 0x14;  /* one index entry: 0x12 + 2 */
    *(int16_t *)(page + 0x10) = entry_off;
    *(int16_t *)(page + 0x12) = entry_off;  /* index entry points to our entry */

    dir_$compact_page_entries(&ctx);

    /* Reclaimable flag should be cleared */
    ASSERT_EQ(0, page[0] & 0x20);

    /* Entry should be unchanged */
    ASSERT_EQ(entry_off, *(int16_t *)(page + 0x10));
    ASSERT_EQ(entry_off, *(int16_t *)(page + 0x12));
    ASSERT_EQ(0x02, page[entry_off] & 0x07);
    ASSERT_EQ(4, page[entry_off + 1]);

    free(page);
}

/*
 * Test: single dead entry with no entries before it.
 * Dead entry is the only entry and is at free_space_start.
 * After compaction, free_space_start should advance by entry_size.
 */
TEST(single_dead_entry_no_preceding) {
    uint8_t *page = create_page();
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_data = page;
    ctx.idx_base = page + 0x12;

    page[0] = 0x20;  /* reclaimable flag */

    /* One dead type-2 entry with 4-byte name */
    uint16_t esize = (16 + 4 + 3) & ~3;  /* = 20 */
    int16_t entry_off = 0x400 - esize;
    write_type2_entry(page, entry_off, 4, "dead", 1);  /* dead=1 sets bit 7 */

    *(int16_t *)(page + 0x0E) = 0x14;  /* one index entry */
    *(int16_t *)(page + 0x10) = entry_off;
    *(int16_t *)(page + 0x12) = entry_off;

    dir_$compact_page_entries(&ctx);

    /* free_space_start should advance by entry size */
    ASSERT_EQ(entry_off + esize, *(int16_t *)(page + 0x10));

    free(page);
}

/*
 * Test: dead entry with a live entry before it.
 * Setup: [free_start -> live_entry | dead_entry | end_of_page]
 * After compaction: live_entry should shift right by dead_entry_size,
 * its index should be updated, and free_space_start should advance.
 */
TEST(dead_entry_with_preceding_live) {
    uint8_t *page = create_page();
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_data = page;
    ctx.idx_base = page + 0x12;

    page[0] = 0x20;  /* reclaimable flag */

    /* Layout: two entries at end of page.
     * Entry B (dead) is closer to 0x400.
     * Entry A (live) is just below entry B. */
    uint16_t size_a = (16 + 4 + 3) & ~3;  /* 20 bytes, name="live" */
    uint16_t size_b = (16 + 4 + 3) & ~3;  /* 20 bytes, name="dead" */

    int16_t off_b = 0x400 - size_b;
    int16_t off_a = off_b - size_a;

    write_type2_entry(page, off_a, 4, "live", 0);
    write_type2_entry(page, off_b, 4, "dead", 1);  /* dead=1 sets bit 7 */

    /* Two index entries */
    *(int16_t *)(page + 0x0E) = 0x16;  /* 0x12 + 2*2 */
    *(int16_t *)(page + 0x10) = off_a;
    *(int16_t *)(page + 0x12) = off_a;  /* idx[0] -> entry A */
    *(int16_t *)(page + 0x14) = off_b;  /* idx[1] -> entry B */

    /* Save original entry A data for comparison */
    uint8_t saved_a[20];
    memcpy(saved_a, page + off_a, size_a);

    dir_$compact_page_entries(&ctx);

    /* free_space_start should advance by dead entry size */
    ASSERT_EQ(off_a + size_b, *(int16_t *)(page + 0x10));

    /* Entry A should have been shifted right by size_b */
    int16_t new_off_a = off_a + size_b;
    ASSERT_EQ(new_off_a, *(int16_t *)(page + 0x12));

    /* Verify entry A data was preserved at its new location */
    ASSERT_MEM_EQ(saved_a, page + new_off_a, size_a);

    free(page);
}

/*
 * Test: two dead entries in sequence.
 * Setup: [free_start -> dead_A | dead_B | end]
 * Both should be compacted and free_space should advance by both sizes.
 */
TEST(two_consecutive_dead_entries) {
    uint8_t *page = create_page();
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_data = page;
    ctx.idx_base = page + 0x12;

    page[0] = 0x20;  /* reclaimable flag */

    uint16_t size_a = (16 + 4 + 3) & ~3;  /* 20 bytes */
    uint16_t size_b = (16 + 4 + 3) & ~3;  /* 20 bytes */

    int16_t off_b = 0x400 - size_b;
    int16_t off_a = off_b - size_a;

    write_type2_entry(page, off_a, 4, "dea1", 1);
    write_type2_entry(page, off_b, 4, "dea2", 1);

    *(int16_t *)(page + 0x0E) = 0x16;  /* 2 index entries */
    *(int16_t *)(page + 0x10) = off_a;
    *(int16_t *)(page + 0x12) = off_a;
    *(int16_t *)(page + 0x14) = off_b;

    dir_$compact_page_entries(&ctx);

    /* Both dead entries reclaimed */
    ASSERT_EQ(off_a + size_a + size_b, *(int16_t *)(page + 0x10));

    free(page);
}

/*
 * Test: live entry between two dead entries.
 * Setup: [free_start -> dead_A | live_B | dead_C | end]
 * After compaction: B should shift right by dead_A size.
 * Then dead_C is at original position (not shifted by A),
 * and free_space advances by both dead entry sizes total.
 */
TEST(live_between_two_dead) {
    uint8_t *page = create_page();
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_data = page;
    ctx.idx_base = page + 0x12;

    page[0] = 0x20;  /* reclaimable flag */

    uint16_t size = (16 + 4 + 3) & ~3;  /* 20 bytes each */

    int16_t off_c = 0x400 - size;
    int16_t off_b = off_c - size;
    int16_t off_a = off_b - size;

    write_type2_entry(page, off_a, 4, "dea1", 1);
    write_type2_entry(page, off_b, 4, "live", 0);
    write_type2_entry(page, off_c, 4, "dea2", 1);

    *(int16_t *)(page + 0x0E) = 0x18;  /* 3 index entries */
    *(int16_t *)(page + 0x10) = off_a;
    *(int16_t *)(page + 0x12) = off_a;  /* idx[0] -> dead A */
    *(int16_t *)(page + 0x14) = off_b;  /* idx[1] -> live B */
    *(int16_t *)(page + 0x16) = off_c;  /* idx[2] -> dead C */

    /* Save entry B data */
    uint8_t saved_b[20];
    memcpy(saved_b, page + off_b, size);

    dir_$compact_page_entries(&ctx);

    /* Free space should advance by both dead entries' sizes */
    ASSERT_EQ(off_a + size + size, *(int16_t *)(page + 0x10));

    /* Entry B is only shifted once (by dead C compaction).
     * Dead A compaction doesn't shift B because dead A is at
     * free_space_start with no entries before it to shift.
     * So B shifts right by one entry_size only. */
    int16_t expected_b_off = off_b + size;
    ASSERT_EQ(expected_b_off, *(int16_t *)(page + 0x14));

    /* Verify entry B data preserved */
    ASSERT_MEM_EQ(saved_b, page + expected_b_off, size);

    free(page);
}

/*
 * Test: reclaimable flag is cleared after compaction.
 */
TEST(flag_cleared_after_compaction) {
    uint8_t *page = create_page();
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_data = page;
    ctx.idx_base = page + 0x12;

    /* Set reclaimable flag: bit 5 of byte 0 */
    page[0] = 0x20;

    *(int16_t *)(page + 0x0E) = 0x12;  /* no entries */
    *(int16_t *)(page + 0x10) = 0x400; /* no entry data */

    dir_$compact_page_entries(&ctx);

    /* Bit 5 (0x20) should be cleared */
    ASSERT_EQ(0, page[0] & 0x20);

    free(page);
}

/*
 * Test: index table entries for entries AFTER dead entry are not modified.
 * Only entries with offsets LESS than the dead entry offset get adjusted.
 */
TEST(index_table_selective_update) {
    uint8_t *page = create_page();
    dir_insert_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_data = page;
    ctx.idx_base = page + 0x12;

    page[0] = 0x20;  /* reclaimable flag */

    /* Three entries: live_A, dead_B, live_C
     * A is at lowest offset (closest to free_space_start)
     * C is at highest offset (closest to 0x400) */
    uint16_t size = (16 + 4 + 3) & ~3;  /* 20 bytes */

    int16_t off_c = 0x400 - size;
    int16_t off_b = off_c - size;
    int16_t off_a = off_b - size;

    write_type2_entry(page, off_a, 4, "aaa1", 0);
    write_type2_entry(page, off_b, 4, "bbb1", 1);
    write_type2_entry(page, off_c, 4, "ccc1", 0);

    *(int16_t *)(page + 0x0E) = 0x18;  /* 3 index entries */
    *(int16_t *)(page + 0x10) = off_a;
    /* Index entries can be in any order (they're not sorted by offset) */
    *(int16_t *)(page + 0x12) = off_c;  /* idx[0] -> C (after dead) */
    *(int16_t *)(page + 0x14) = off_a;  /* idx[1] -> A (before dead) */
    *(int16_t *)(page + 0x16) = off_b;  /* idx[2] -> B (the dead one) */

    dir_$compact_page_entries(&ctx);

    /* C should NOT be shifted (it's after dead B) */
    ASSERT_EQ(off_c, *(int16_t *)(page + 0x12));

    /* A should be shifted by size (it's before dead B) */
    ASSERT_EQ(off_a + size, *(int16_t *)(page + 0x14));

    free(page);
}

int main(void)
{
    printf("dir_$compact_page_entries tests:\n");

    RUN_TEST(no_reclaimable_flag);
    RUN_TEST(no_dead_entries);
    RUN_TEST(single_dead_entry_no_preceding);
    RUN_TEST(dead_entry_with_preceding_live);
    RUN_TEST(two_consecutive_dead_entries);
    RUN_TEST(live_between_two_dead);
    RUN_TEST(flag_cleared_after_compaction);
    RUN_TEST(index_table_selective_update);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
