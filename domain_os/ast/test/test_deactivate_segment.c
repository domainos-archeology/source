/*
 * ast/test/test_deactivate_segment.c - Unit tests for
 *                                      AST_$DEACTIVATE_SEGMENT (0x00E01950)
 *
 * The test #includes ast/deactivate_segment.c directly and drives the real
 * routine through mocked callees.  Its subjects are the three defects bead
 * source-jddw found, plus the surrounding walk:
 *
 *   - 0x00E0199A-0x00E019A4: a process type of 8 or 9 REFUSES the
 *     deactivation and any other type allows it.  The tree had the test
 *     inverted.
 *   - 0x00E019FC-0x00E01A16: PMAP_$FLUSH really does take six Pascal
 *     arguments; the single `pea (0x20).w` supplies the two constant words
 *     start_page = 0 and count = 0x20 at once.
 *   - 0x00E01AA2 `bset.b #0x7,(A2)` sets bit 31 of the status longword, not
 *     bit 7 of its least significant byte.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Avoid the macOS uid_t conflict - must come AFTER the system includes. */
#define uid_t ast_uid_t

/* ==========================================================================
 * Test infrastructure
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

static void reset_mocks(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %-48s", #name);                                         \
    current_failed = 0;                                                       \
    reset_mocks();                                                            \
    test_##name();                                                            \
    if (current_failed == 0) { tests_passed++; printf("PASSED\n"); }          \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {      \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",       \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                       \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#define ASSERT_TRUE(cond) do {                                                \
    if (!(cond)) {                                                            \
        printf("FAILED\n    Assertion failed at line %d: %s\n",               \
               __LINE__, #cond);                                              \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#include "ast/ast_internal.h"
#include "netlog/netlog.h"
#include "anon/anon.h"

/* ==========================================================================
 * Storage for the tables the routine walks
 * ========================================================================== */

#define TEST_N_SEGS   4
#define TEST_N_PAGES  32
#define TEST_N_FRAMES 64

static segmap_entry_t test_segmap[(TEST_N_SEGS + 1) * TEST_N_PAGES];
static aste_t         test_astes[TEST_N_SEGS];
static mmape_t        test_mmapes[TEST_N_FRAMES];
/* The MMU page frame table (0xFFB800 on the target): four bytes per page. */
static uint32_t       test_pft[TEST_N_FRAMES];

/*
 * SEGMAP_BASE is the BIASED base: the row for segment n lives at
 * SEGMAP_BASE + n*0x80 - 0x80, so offer one spare row below it.
 */
segmap_entry_t *ast_segmap_base = &test_segmap[TEST_N_PAGES];
aste_t         *ast_aste_base   = test_astes;
mmape_t        *mmap_mmape_base = test_mmapes;
uint32_t       *mmu_pft_base     = test_pft;

ec_$eventcount_t ast_ast_in_trans_ec;
ec_$eventcount_t ast_pmap_in_trans_ec;

uint16_t PROC1_$CURRENT;
uint16_t PROC1_$TYPE[PROC1_MAX_PROCESSES];

int8_t   NETLOG_$OK_TO_LOG;
uid_t    ANON_$UID;

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

#define MAX_LOCKS 16
static int      lock_calls;
static int      unlock_calls;
static int16_t  lock_ids[MAX_LOCKS];
static int16_t  unlock_ids[MAX_LOCKS];

void ML_$LOCK(int16_t resource_id)
{
    if (lock_calls < MAX_LOCKS) { lock_ids[lock_calls] = resource_id; }
    lock_calls++;
}

void ML_$UNLOCK(int16_t resource_id)
{
    if (unlock_calls < MAX_LOCKS) { unlock_ids[unlock_calls] = resource_id; }
    unlock_calls++;
}

static int ec_advance_calls;
static ec_$eventcount_t *ec_advance_last;

void EC_$ADVANCE(ec_$eventcount_t *ec)
{
    ec_advance_calls++;
    ec_advance_last = ec;
}

/* PMAP_$FLUSH */
static int        flush_calls;
static aste_t    *flush_aste;
static uint32_t  *flush_segmap;
static uint16_t   flush_start_page;
static int16_t    flush_count;
static uint16_t   flush_flags;
static status_$t *flush_status_ptr;
static status_$t  flush_status;

int16_t PMAP_$FLUSH(struct aste_t *aste, uint32_t *segmap, uint16_t start_page,
                    int16_t count, uint16_t flags, status_$t *status)
{
    flush_calls++;
    flush_aste = (aste_t *)aste;
    flush_segmap = segmap;
    flush_start_page = start_page;
    flush_count = count;
    flush_flags = flags;
    flush_status_ptr = status;
    *status = flush_status;
    return 0;
}

/* AREA_$DEACTIVATE_ASTE */
static int        area_deact_calls;
static void      *area_deact_aste;
static status_$t  area_deact_status;

void AREA_$DEACTIVATE_ASTE(void *aste, status_$t *status_ret)
{
    area_deact_calls++;
    area_deact_aste = aste;
    *status_ret = area_deact_status;
}

/* ast_$update_aste */
static int             update_calls;
static aste_t         *update_aste;
static segmap_entry_t *update_segmap;
static uint16_t        update_flags;
static status_$t       update_status;

void ast_$update_aste(aste_t *aste, segmap_entry_t *segmap, boolean flags,
                      status_$t *status)
{
    update_calls++;
    update_aste = aste;
    update_segmap = segmap;
    update_flags = flags;
    *status = update_status;
}

/* NETLOG_$LOG_IT */
static int      log_calls;
static uint16_t log_kind;
static uint32_t log_uid_high;
static uint32_t log_uid_low;
static uint16_t log_p3, log_p4, log_p5, log_p6, log_p7, log_p8;

void NETLOG_$LOG_IT(uint16_t kind, uint32_t *uid,
                    uint16_t param3, uint16_t param4,
                    uint16_t param5, uint16_t param6,
                    uint16_t param7, uint16_t param8)
{
    log_calls++;
    log_kind = kind;
    log_uid_high = uid[0];
    log_uid_low = uid[1];
    log_p3 = param3; log_p4 = param4; log_p5 = param5;
    log_p6 = param6; log_p7 = param7; log_p8 = param8;
}

/* ==========================================================================
 * The unit under test
 * ========================================================================== */

#include "../deactivate_segment.c"

/* ==========================================================================
 * Fixtures
 * ========================================================================== */

#define TEST_SEG  2

static aote_t test_aote;

static aste_t *the_aste(void) { return &test_astes[TEST_SEG - 1]; }

static uint32_t *segmap_row_of_test_seg(void)
{
    return (uint32_t *)((char *)SEGMAP_BASE + (uint32_t)TEST_SEG * 0x80 - 0x80);
}

static void reset_mocks(void)
{
    memset(test_segmap, 0, sizeof(test_segmap));
    memset(test_astes, 0, sizeof(test_astes));
    memset(test_mmapes, 0, sizeof(test_mmapes));
    memset(test_pft, 0, sizeof(test_pft));
    memset(&test_aote, 0, sizeof(test_aote));
    memset(PROC1_$TYPE, 0, sizeof(PROC1_$TYPE));

    lock_calls = unlock_calls = 0;
    memset(lock_ids, 0, sizeof(lock_ids));
    memset(unlock_ids, 0, sizeof(unlock_ids));
    ec_advance_calls = 0;
    ec_advance_last = NULL;
    flush_calls = 0;
    flush_status = status_$ok;
    area_deact_calls = 0;
    area_deact_status = status_$ok;
    update_calls = 0;
    update_status = status_$ok;
    log_calls = 0;

    PROC1_$CURRENT = 1;
    PROC1_$TYPE[1] = 4;                 /* not 8, not 9 */
    NETLOG_$OK_TO_LOG = 0;
    ANON_$UID.high = 0x00000407;
    ANON_$UID.low = 0;

    the_aste()->flags = 0;
    the_aste()->wire_count = 0;
    the_aste()->page_count = 3;
    the_aste()->seg_index = TEST_SEG;
    the_aste()->segment = 0x5A5A;
    the_aste()->aote = &test_aote;
    the_aste()->next = NULL;

    test_aote.aste_list = the_aste();
    test_aote.status_flags = 5;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E01968 */
TEST(already_in_transition_is_refused)
{
    status_$t status = 0;

    the_aste()->flags = ASTE_FLAG_IN_TRANS;

    AST_$DEACTIVATE_SEGMENT(the_aste(), 0, 0, &status);

    ASSERT_EQ(status_$ast_segment_not_deactivatable, status);
    ASSERT_EQ(0, flush_calls);
}

/* 0x00E0196E */
TEST(nonzero_wire_count_is_refused)
{
    status_$t status = 0;

    the_aste()->wire_count = 1;

    AST_$DEACTIVATE_SEGMENT(the_aste(), 0, 0, &status);

    ASSERT_EQ(status_$ast_segment_not_deactivatable, status);
    ASSERT_EQ(0, flush_calls);
}

/*
 * 0x00E01974-0x00E01986: the process-type gate only applies when BOTH
 * ASTE_FLAG_DIRTY (0x2000) and ASTE_FLAG_REMOTE (0x0800) are set.
 */
TEST(one_flag_alone_skips_the_process_type_gate)
{
    status_$t status = 0;

    the_aste()->flags = ASTE_FLAG_DIRTY;
    PROC1_$TYPE[1] = 8;                 /* would be refused if it were read */

    AST_$DEACTIVATE_SEGMENT(the_aste(), 0, 0, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, flush_calls);
}

/* 0x00E0199A: type 8 refuses */
TEST(proc_type_8_is_refused)
{
    status_$t status = 0;

    the_aste()->flags = ASTE_FLAG_DIRTY | ASTE_FLAG_REMOTE;
    PROC1_$TYPE[1] = 8;

    AST_$DEACTIVATE_SEGMENT(the_aste(), 0, 0, &status);

    ASSERT_EQ(status_$ast_segment_not_deactivatable, status);
    ASSERT_EQ(0, flush_calls);
}

/* 0x00E019A0: type 9 refuses too (it falls into the same error store) */
TEST(proc_type_9_is_refused)
{
    status_$t status = 0;

    the_aste()->flags = ASTE_FLAG_DIRTY | ASTE_FLAG_REMOTE;
    PROC1_$TYPE[1] = 9;

    AST_$DEACTIVATE_SEGMENT(the_aste(), 0, 0, &status);

    ASSERT_EQ(status_$ast_segment_not_deactivatable, status);
    ASSERT_EQ(0, flush_calls);
}

/* 0x00E019A4 `bne 0x00E019B0`: anything else proceeds */
TEST(other_proc_types_proceed)
{
    status_$t status = 0;

    the_aste()->flags = ASTE_FLAG_DIRTY | ASTE_FLAG_REMOTE;
    PROC1_$TYPE[1] = 7;

    AST_$DEACTIVATE_SEGMENT(the_aste(), 0, 0, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, flush_calls);
}

/* 0x00E019B0 / 0x00E019DE / 0x00E01A5A */
TEST(sets_in_transition_and_drops_the_ast_lock)
{
    status_$t status = 0;

    AST_$DEACTIVATE_SEGMENT(the_aste(), 0, 0, &status);

    ASSERT_TRUE((the_aste()->flags & ASTE_FLAG_IN_TRANS) != 0);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(AST_LOCK_ID, unlock_ids[0]);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(AST_LOCK_ID, lock_ids[0]);
}

/* 0x00E019FC-0x00E01A16 */
TEST(pmap_flush_argument_list)
{
    status_$t status = 0;

    AST_$DEACTIVATE_SEGMENT(the_aste(), 0, 0, &status);

    ASSERT_EQ(1, flush_calls);
    ASSERT_EQ((uintptr_t)the_aste(), (uintptr_t)flush_aste);
    ASSERT_EQ((uintptr_t)segmap_row_of_test_seg(), (uintptr_t)flush_segmap);
    ASSERT_EQ(0, flush_start_page);       /* the high half of `pea (0x20).w` */
    ASSERT_EQ(0x20, flush_count);         /* its low half */
    ASSERT_EQ(1, flush_flags);
    ASSERT_EQ((uintptr_t)&status, (uintptr_t)flush_status_ptr);
}

/* 0x00E019F2-0x00E019F6 */
TEST(negative_purge_selects_flush_mode_3)
{
    status_$t status = 0;

    AST_$DEACTIVATE_SEGMENT(the_aste(), (int8_t)-1, 0, &status);

    ASSERT_EQ(3, flush_flags);
}

/*
 * 0x00E01AA2: a flush failure marks bit 31 of the status, re-takes the AST
 * lock, clears the in-transition bit and advances the eventcount.
 */
TEST(flush_failure_sets_bit_31_and_backs_out)
{
    status_$t status = 0;

    flush_status = 0x00120034;

    AST_$DEACTIVATE_SEGMENT(the_aste(), 0, 0, &status);

    ASSERT_EQ((status_$t)0x80120034, status);
    ASSERT_EQ(0, (the_aste()->flags & ASTE_FLAG_IN_TRANS));
    ASSERT_EQ(1, ec_advance_calls);
    ASSERT_EQ((uintptr_t)&AST_$AST_IN_TRANS_EC, (uintptr_t)ec_advance_last);
    /* the AOTE list is untouched */
    ASSERT_EQ((uintptr_t)the_aste(), (uintptr_t)test_aote.aste_list);
    ASSERT_EQ(5, test_aote.status_flags);
}

/* 0x00E01A20-0x00E01A24: both flags negative skips the write-back */
TEST(both_flags_negative_skips_the_update)
{
    status_$t status = 0;

    AST_$DEACTIVATE_SEGMENT(the_aste(), (int8_t)-1, (int8_t)-1, &status);

    ASSERT_EQ(1, flush_calls);
    ASSERT_EQ(0, update_calls);
    ASSERT_EQ(0, area_deact_calls);
    /* the unlink still runs */
    ASSERT_EQ(0, (uintptr_t)test_aote.aste_list);
}

/* 0x00E01A42-0x00E01A52 */
TEST(non_area_segment_updates_the_segment_map)
{
    status_$t status = 0;

    AST_$DEACTIVATE_SEGMENT(the_aste(), 0, 0, &status);

    ASSERT_EQ(1, update_calls);
    ASSERT_EQ((uintptr_t)the_aste(), (uintptr_t)update_aste);
    ASSERT_EQ((uintptr_t)segmap_row_of_test_seg(), (uintptr_t)update_segmap);
    ASSERT_EQ(0, update_flags);
    ASSERT_EQ(0, area_deact_calls);
}

/* 0x00E01A34-0x00E01A3E and 0x00E01A70-0x00E01A74 */
TEST(area_segment_deactivates_the_area_and_keeps_the_aote_list)
{
    status_$t status = 0;

    the_aste()->flags = ASTE_FLAG_AREA;

    AST_$DEACTIVATE_SEGMENT(the_aste(), 0, 0, &status);

    ASSERT_EQ(1, area_deact_calls);
    ASSERT_EQ((uintptr_t)the_aste(), (uintptr_t)area_deact_aste);
    ASSERT_EQ(0, update_calls);
    /* 0x00E01A74 `bne` - the unlink is skipped for an area segment */
    ASSERT_EQ((uintptr_t)the_aste(), (uintptr_t)test_aote.aste_list);
    ASSERT_EQ(5, test_aote.status_flags);
}

/* 0x00E01A7E-0x00E01A88: the ASTE is at the head of the AOTE's list */
TEST(unlink_from_aote_list_head)
{
    status_$t status = 0;
    aste_t tail;

    memset(&tail, 0, sizeof(tail));
    the_aste()->next = &tail;
    test_aote.aste_list = the_aste();

    AST_$DEACTIVATE_SEGMENT(the_aste(), 0, 0, &status);

    ASSERT_EQ((uintptr_t)&tail, (uintptr_t)test_aote.aste_list);
    ASSERT_EQ(4, test_aote.status_flags);
}

/* 0x00E01A8A-0x00E01A9A: the ASTE is further down the list */
TEST(unlink_from_aote_list_middle)
{
    status_$t status = 0;
    aste_t head;
    aste_t tail;

    memset(&head, 0, sizeof(head));
    memset(&tail, 0, sizeof(tail));
    head.next = the_aste();
    the_aste()->next = &tail;
    test_aote.aste_list = &head;

    AST_$DEACTIVATE_SEGMENT(the_aste(), 0, 0, &status);

    ASSERT_EQ((uintptr_t)&head, (uintptr_t)test_aote.aste_list);
    ASSERT_EQ((uintptr_t)&tail, (uintptr_t)head.next);
    ASSERT_EQ(4, test_aote.status_flags);
}

/*
 * 0x00E019CC-0x00E019DC and the nested helper at 0x00E01872.  One installed
 * page (bit 30 set) whose PMAPE second word has bit 13 set counts as
 * referenced.
 */
TEST(netlog_helper_counts_referenced_pages)
{
    status_$t status = 0;
    uint32_t *row = segmap_row_of_test_seg();

    NETLOG_$OK_TO_LOG = (int8_t)-1;

    /* entry 0: installed, PPN 7 */
    row[0] = 0x40000000u | 7u;
    /* written through the same accessor the helper reads it with */
    PMAPE_FOR_VPN(7)[1] = PMAPE_FLAG_REFERENCED;
    /* entry 1: installed, PPN 8, not referenced */
    row[1] = 0x40000000u | 8u;
    /* entry 2: not installed */
    row[2] = 9u;

    test_aote.obj_loc_uid.high = 0x11112222;
    test_aote.obj_loc_uid.low  = 0x33334444;

    AST_$DEACTIVATE_SEGMENT(the_aste(), 0, 0, &status);

    ASSERT_EQ(1, log_calls);
    ASSERT_EQ(1, log_kind);
    ASSERT_EQ(0x11112222u, log_uid_high);
    ASSERT_EQ(0x33334444u, log_uid_low);
    ASSERT_EQ(0x5A5A, log_p3);
    ASSERT_EQ(3, log_p4);
    ASSERT_EQ(TEST_SEG, log_p5);
    ASSERT_EQ(0, log_p6);
    ASSERT_EQ(0, log_p7);
    ASSERT_EQ(1, log_p8);
}

/* 0x00E018E0-0x00E018FC: an area segment logs ANON_$UID.high + aote+0x2A */
TEST(netlog_helper_area_segment_uses_anon_uid)
{
    status_$t status = 0;

    NETLOG_$OK_TO_LOG = (int8_t)-1;
    the_aste()->flags = ASTE_FLAG_AREA;
    the_aste()->page_count = 0;         /* skip the scan */
    test_aote.dtm_high = 0x1234BEEF;

    AST_$DEACTIVATE_SEGMENT(the_aste(), 0, 0, &status);

    ASSERT_EQ(1, log_calls);
    ASSERT_EQ(0x00000407u, log_uid_high);
    ASSERT_EQ(0x0000BEEFu, log_uid_low);
    ASSERT_EQ(0, log_p8);               /* the scan never ran */
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("AST_$DEACTIVATE_SEGMENT tests:\n");

    RUN_TEST(already_in_transition_is_refused);
    RUN_TEST(nonzero_wire_count_is_refused);
    RUN_TEST(one_flag_alone_skips_the_process_type_gate);
    RUN_TEST(proc_type_8_is_refused);
    RUN_TEST(proc_type_9_is_refused);
    RUN_TEST(other_proc_types_proceed);
    RUN_TEST(sets_in_transition_and_drops_the_ast_lock);
    RUN_TEST(pmap_flush_argument_list);
    RUN_TEST(negative_purge_selects_flush_mode_3);
    RUN_TEST(flush_failure_sets_bit_31_and_backs_out);
    RUN_TEST(both_flags_negative_skips_the_update);
    RUN_TEST(non_area_segment_updates_the_segment_map);
    RUN_TEST(area_segment_deactivates_the_area_and_keeps_the_aote_list);
    RUN_TEST(unlink_from_aote_list_head);
    RUN_TEST(unlink_from_aote_list_middle);
    RUN_TEST(netlog_helper_counts_referenced_pages);
    RUN_TEST(netlog_helper_area_segment_uses_anon_uid);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
