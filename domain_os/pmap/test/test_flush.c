/*
 * pmap/test/test_flush.c - Unit tests for PMAP_$FLUSH (0x00E1376C)
 *
 * pmap/flush.c is #included below; the two nested procedures
 * (pmap_$flush_write_batch, pmap_$update_seg_map), pmap_$write_page,
 * pmap_$wait_in_transit, the MMAP/MMU calls and the clocks are mocked.
 */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "pmap/pmap_internal.h"
#include "ast/ast.h"
#include "mmap/mmap.h"
#include "mmu/mmu.h"
#include "misc/misc.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* module data */
static uint32_t pft_store[0x1000];
MODULE_DATA_DEFINE(mmap_$mmape_table_t, MMAP_$MMAPE, 0x00EB4800);
uint32_t *mmu_pft_base = pft_store;
MODULE_DATA_DEFINE(pmap_$data_t, PMAP_$DATA, 0x00E24D44);
status_$t status_$t_00e13a14 = 0x00050003;

static int locks, unlocks, unavails, removes, writes, updates, batches, waits;
static uint32_t last_unavail, last_remove, last_write_vpn, last_update_vpn;
static int8_t last_write_sync;
static status_$t write_status;
static int16_t batch_seen_count;
static uint32_t batch_seen[17];
static status_$t batch_status;
static jmp_buf crash_jmp;
static status_$t crash_status;
static int clock_calls, abs_clock_calls;
static aste_t the_aste;
static aote_t the_aote;

void ML_$LOCK(int16_t id) { (void)id; locks++; }
void ML_$UNLOCK(int16_t id) { (void)id; unlocks++; }
void MMAP_$UNAVAIL_REMOV(uint32_t vpn, boolean f) { (void)f; unavails++; last_unavail = vpn; }
void MMU_$REMOVE(uint32_t ppn) { removes++; last_remove = ppn; }
void CRASH_SYSTEM(const status_$t *s) { crash_status = *s; longjmp(crash_jmp, 1); }
void TIME_$CLOCK(clock_t *c) { clock_calls++; c->high = 0x1111; c->low = 0x22; }
void TIME_$ABS_CLOCK(clock_t *c) { abs_clock_calls++; c->high = 0x3333; c->low = 0x44; }

void pmap_$write_page(uint32_t vpn, status_$t *status, int8_t sync_flag)
{
    writes++; last_write_vpn = vpn; last_write_sync = sync_flag;
    *status = write_status;
}
void pmap_$update_seg_map(struct aste_t *aste, uint16_t flags, uint32_t *e,
                          uint32_t vpn, uint16_t page_idx)
{
    (void)aste; (void)flags; (void)e; (void)page_idx;
    updates++; last_update_vpn = vpn;
}
void pmap_$flush_write_batch(int16_t *count, uint32_t *vpns, uint32_t *segmap,
                             status_$t *status, struct aste_t *aste, uint16_t flags)
{
    int i;
    (void)segmap; (void)aste; (void)flags;
    batches++;
    batch_seen_count = *count;
    for (i = 1; i <= *count && i < 17; i++) batch_seen[i] = vpns[i];
    *count = 0;
    *status = batch_status;
}
void pmap_$wait_in_transit(void)
{
    waits++;
    the_aste.page_count = 0;        /* let the outer loop end */
}

#include "../flush.c"

static uint32_t segmap[32];

static void reset(void)
{
    memset(&MMAP_$MMAPE, 0, sizeof(MMAP_$MMAPE));
    memset(pft_store, 0, sizeof pft_store);
    memset(segmap, 0, sizeof segmap);
    memset(&the_aste, 0, sizeof the_aste);
    memset(&the_aote, 0, sizeof the_aote);
    the_aste.aote = &the_aote;
    the_aste.page_count = 1;
    locks = unlocks = unavails = removes = writes = updates = batches = waits = 0;
    clock_calls = abs_clock_calls = 0;
    write_status = batch_status = 0;
    PMAP_$DATA.shutting_down_flag = 0;
}

/* page index i holds vpn, pageable, seg_offset matching.  MMAP_$MMAPE has
 * entries only for ppn 0x200..0xFFF, so a non-pageable vpn below that gets
 * no MMAPE (PMAP_$FLUSH never reads one for it). */
static void map(int i, uint32_t vpn, uint32_t extra)
{
    segmap[i] = PMAP_SEGMAP_L_VALID | vpn | extra;
    if (vpn >= MMAP_MMAPE_FIRST_PPN) {
        MMAPE_FOR_VPN(vpn)->seg_offset = (uint8_t)i;
    }
}

TEST(local_modified_page_is_batched)
{
    status_$t st = 0x77;
    int16_t r;
    reset();
    map(3, 0x300, 0);
    pft_store[0x300] = PFT_FLAG_MODIFIED;
    r = PMAP_$FLUSH(&the_aste, segmap, 3, 1, 0, &st);
    ASSERT_EQ(1, r);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, unavails);
    ASSERT_EQ(0x300, last_unavail);
    ASSERT_EQ(0, pft_store[0x300]);
    ASSERT_EQ(PMAP_SEGMAP_L_VALID | PMAP_SEGMAP_L_WRITING | 0x300, segmap[3]);
    ASSERT_EQ(1, batches);
    ASSERT_EQ(1, batch_seen_count);
    ASSERT_EQ(0x300, batch_seen[1]);
    ASSERT_EQ(0, updates);            /* was_modified: 0x00E13962 skips update_seg_map */
    ASSERT_EQ(0, writes);
    ASSERT_EQ(0, waits);              /* no in-use entry: the pass ends at 0x00E139A2 */
    /* any_modified with bits 11/12 clear: the object clocks are stamped */
    ASSERT_EQ(1, clock_calls);
    ASSERT_EQ(1, abs_clock_calls);
    ASSERT_EQ(0x1111, the_aote.dta_high);
    ASSERT_EQ(0x22, the_aote.dta_low);
    ASSERT_EQ(0x20, the_aote.flags);
    ASSERT_EQ(1, locks);
    ASSERT_EQ(1, unlocks);
}

TEST(bit11_page_is_written_directly)
{
    status_$t st = 0;
    int16_t r;
    reset();
    the_aste.flags = 0x0800;
    map(0, 0x400, 0);
    MMAPE_FOR_VPN(0x400)->flags2 = 0x40;         /* dirty via flags2, PFT clean */
    r = PMAP_$FLUSH(&the_aste, segmap, 0, 1, 0, &st);
    ASSERT_EQ(1, r);
    ASSERT_EQ(1, writes);
    ASSERT_EQ(0x400, last_write_vpn);
    ASSERT_EQ(0xFF, (uint8_t)last_write_sync);
    ASSERT_EQ(1, updates);            /* after the write only (0x00E1392A) */
    ASSERT_EQ(0, batches);
    ASSERT_EQ(0, MMAPE_FOR_VPN(0x400)->flags2);
    ASSERT_EQ(0, waits);              /* wrote_direct -> no wait; loop ends? */
    ASSERT_EQ(0, clock_calls);        /* PFT bit never set: not any_modified */
}

TEST(async_flag_and_write_error)
{
    status_$t st = 0;
    int16_t r;
    reset();
    the_aste.flags = 0x0800;
    map(0, 0x400, 0);
    pft_store[0x400] = PFT_FLAG_MODIFIED;
    write_status = 0x9;
    r = PMAP_$FLUSH(&the_aste, segmap, 0, 1, 4, &st);
    ASSERT_EQ(1, r);
    ASSERT_EQ(0, (uint8_t)last_write_sync);
    ASSERT_EQ(0x9, st);
    ASSERT_EQ(0, updates);
    /* the failure leaves through `done': the clocks are still stamped */
    ASSERT_EQ(0, clock_calls);        /* ... unless bit 11 is set, as here */
    ASSERT_EQ(1, unlocks);
}

TEST(nowrite_flag_only_updates)
{
    status_$t st = 0;
    int16_t r;
    reset();
    map(0, 0x400, 0);
    pft_store[0x400] = PFT_FLAG_MODIFIED;
    r = PMAP_$FLUSH(&the_aste, segmap, 0, 1, 2, &st);
    ASSERT_EQ(0, r);
    ASSERT_EQ(0, writes);
    ASSERT_EQ(0, batches);
    ASSERT_EQ(1, updates);
    ASSERT_EQ(0, pft_store[0x400]);
}

TEST(wired_page_fails_after_flushing_batch)
{
    status_$t st = 0;
    reset();
    map(0, 0x400, 0);
    pft_store[0x400] = PFT_FLAG_MODIFIED;
    map(1, 0x401, 0);
    MMAPE_FOR_VPN(0x401)->wire_count = 1;
    (void)PMAP_$FLUSH(&the_aste, segmap, 0, 2, 0, &st);
    ASSERT_EQ(status_$pmap_pages_wired, st);
    ASSERT_EQ(1, batches);
    ASSERT_EQ(1, batch_seen_count);
    ASSERT_EQ(1, unlocks);
}

TEST(unmap_flag_removes_installed_page)
{
    status_$t st = 0;
    reset();
    map(0, 0x400, PMAP_SEGMAP_L_INSTALLED);
    (void)PMAP_$FLUSH(&the_aste, segmap, 0, 1, 1, &st);
    ASSERT_EQ(1, removes);
    ASSERT_EQ(0x400, last_remove);
    ASSERT_EQ(PMAP_SEGMAP_L_VALID | 0x400, segmap[0]);
    ASSERT_EQ(1, updates);            /* a clean page is updated (0x00E13970) */
}

TEST(invalid_entry_repeats_until_pages_gone)
{
    status_$t st = 0;
    reset();
    segmap[0] = PMAP_SEGMAP_L_WRITING;   /* bit 31: in use elsewhere */
    (void)PMAP_$FLUSH(&the_aste, segmap, 0, 1, 0, &st);
    ASSERT_EQ(1, waits);
    ASSERT_EQ(0, unavails);
}

TEST(seg_offset_mismatch_crashes)
{
    status_$t st = 0;
    reset();
    map(2, 0x400, 0);
    MMAPE_FOR_VPN(0x400)->seg_offset = 7;
    if (setjmp(crash_jmp) == 0) {
        (void)PMAP_$FLUSH(&the_aste, segmap, 2, 1, 0, &st);
        ASSERT_EQ(1, 0);
    }
    ASSERT_EQ(0x00050003, crash_status);
}

TEST(non_pageable_vpn_skipped_unless_shutting_down)
{
    status_$t st = 0;
    reset();
    map(0, 0x100, 0);
    (void)PMAP_$FLUSH(&the_aste, segmap, 0, 1, 0, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, unavails);
    reset();
    map(0, 0x100, 0);
    PMAP_$DATA.shutting_down_flag = -1;
    (void)PMAP_$FLUSH(&the_aste, segmap, 0, 1, 0, &st);
    ASSERT_EQ(status_$pmap_pages_wired, st);
}

TEST(batch_of_sixteen_flushes_inline)
{
    status_$t st = 0;
    int16_t r;
    int i;
    reset();
    for (i = 0; i < 17; i++) {
        map(i, 0x400 + i, 0);
        pft_store[0x400 + i] = PFT_FLAG_MODIFIED;
    }
    r = PMAP_$FLUSH(&the_aste, segmap, 0, 17, 0, &st);
    ASSERT_EQ(17, r);
    ASSERT_EQ(2, batches);
    ASSERT_EQ(1, batch_seen_count);       /* the second batch holds page 17 */
    ASSERT_EQ(0x410, batch_seen[1]);
}

int main(void)
{
    printf("test_flush:\n");
    RUN_TEST(local_modified_page_is_batched);
    RUN_TEST(bit11_page_is_written_directly);
    RUN_TEST(async_flag_and_write_error);
    RUN_TEST(nowrite_flag_only_updates);
    RUN_TEST(wired_page_fails_after_flushing_batch);
    RUN_TEST(unmap_flag_removes_installed_page);
    RUN_TEST(invalid_entry_repeats_until_pages_gone);
    RUN_TEST(seg_offset_mismatch_crashes);
    RUN_TEST(non_pageable_vpn_skipped_unless_shutting_down);
    RUN_TEST(batch_of_sixteen_flushes_inline);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
