/*
 * ast/test/test_update_aste.c - Unit tests for ast_$update_aste (0x00E01566),
 *                               ast_$setup_page_read (0x00E02898),
 *                               AST_$UPDATE (0x00E016D0), ast_$validate_uid
 *                               (0x00E00BE8) and the two eventcount waits
 *                               (0x00E00C54, 0x00E00C08)
 *
 * The .c files are #included directly and driven through mocks.  Pins:
 *
 *   update_aste: DIRTY/REMOTE gating; the on-disk image (address from the
 *   entry or the MMAPE, bit 22 -> bit 31); FM_$WRITE's arguments; the
 *   "write protected" swallow and the bit-31 / re-dirty failure.
 *
 *   setup_page_read: the attribute-bit-1 early out; AREA allocation with
 *   the three hint sources and per-page logs; RESERVE for others; the
 *   length extension vs TOUCHED; the block counter.
 *
 *   update: the diskless early out; the ASTE selection and the 32-segment
 *   stop; purify per AOTE; the wrap with DBUF_$UPDATE_VOL.
 *
 *   validate_uid / waits: the cells written and the lock bracket.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define uid_t ast_uid_t

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

static void reset_state(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %-48s", #name);                                         \
    current_failed = 0;                                                       \
    reset_state();                                                            \
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

#include "ast/update_aste.c"
#include "ast/setup_page_read.c"
#include "ast/update.c"
#include "ast/validate_uid.c"
#include "ast/wait_for_ast_intrans.c"
#include "ast/wait_for_page_transition.c"

#define TEST_N_PAGES 32
#define TEST_N_FRAMES 0x400
#define TEST_N_AOTES 4
/* The AST_ module blocks (ast/ast.h) and the segment map (pmap/pmap.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);
MODULE_DATA_DEFINE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);
static mmape_t        test_mmapes[TEST_N_FRAMES];
mmape_t        *mmap_mmape_base = test_mmapes;
int8_t    NETLOG_$OK_TO_LOG;
int8_t    NETWORK_$REALLY_DISKLESS;
uid_t     UID_$NIL = { 0, 0 };

static aste_t s1, s2;

#define MAX_REC 8
static int lock_calls, unlock_calls; static int16_t lock_ids[MAX_REC], unlock_ids[MAX_REC];
void ML_$LOCK(int16_t id)   { if (lock_calls < MAX_REC) lock_ids[lock_calls] = id; lock_calls++; }
void ML_$UNLOCK(int16_t id) { if (unlock_calls < MAX_REC) unlock_ids[unlock_calls] = id; unlock_calls++; }
static int advance_calls;
void EC_$ADVANCE(ec_$eventcount_t *ec) { (void)ec; advance_calls++; }
static int waitn_calls; static ec_$eventcount_t *waitn_ec; static int32_t waitn_val; static int16_t waitn_n;
uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *vals, int16_t n)
{
    waitn_calls++; waitn_ec = ecs[0]; waitn_val = vals[0]; waitn_n = n; return 0;
}
static int log_calls; static uint16_t log_kind[MAX_REC], log_p3[MAX_REC], log_p4[MAX_REC], log_p5[MAX_REC], log_p6[MAX_REC], log_p7[MAX_REC];
void NETLOG_$LOG_IT(uint16_t kind, uint32_t *uid, uint16_t p3, uint16_t p4, uint16_t p5, uint16_t p6, uint16_t p7, uint16_t p8)
{
    (void)uid; (void)p8;
    if (log_calls < MAX_REC) { log_kind[log_calls] = kind; log_p3[log_calls] = p3; log_p4[log_calls] = p4; log_p5[log_calls] = p5; log_p6[log_calls] = p6; log_p7[log_calls] = p7; }
    log_calls++;
}
static int fmw_calls; static uint32_t fmw_block; static uint16_t fmw_level; static int8_t fmw_now; static uint32_t fmw_data[32]; static status_$t fmw_status;
void FM_$WRITE(fm_$file_ref_t *ref, uint32_t block, uint16_t level, fm_$entry_t *in, int8_t now, status_$t *st)
{
    (void)ref; fmw_calls++; fmw_block = block; fmw_level = level; fmw_now = now;
    memcpy(fmw_data, in, sizeof(fmw_data)); *st = fmw_status;
}
static int alloc_calls; static int16_t alloc_vol, alloc_count, alloc_res; static uint32_t alloc_hint; static status_$t alloc_status;
void BAT_$ALLOCATE(int16_t vol, uint32_t hint, int16_t count, int16_t res, uint32_t *out, status_$t *st)
{
    int i; alloc_calls++; alloc_vol = vol; alloc_hint = hint; alloc_count = count; alloc_res = res;
    for (i = 0; i < count; i++) out[i] = 0x2000 + i;
    *st = alloc_status;
}
static int reserve_calls; static int16_t reserve_vol; static uint32_t reserve_count; static status_$t reserve_status;
void BAT_$RESERVE(int16_t vol, uint32_t count, status_$t *st) { reserve_calls++; reserve_vol = vol; reserve_count = count; *st = reserve_status; }
static int clock_calls;
void TIME_$CLOCK(clock_t *c) { clock_calls++; c->high = 0xC1; c->low = 0xC2; }
static int purify_calls; static aote_t *purify_aote_arg;
void ast_$purify_aote(aote_t *a, boolean f, status_$t *st) { (void)f; purify_calls++; purify_aote_arg = a; *st = status_$ok; }
static int dbuf_calls;
void DBUF_$UPDATE_VOL(uint16_t vol, void *uid) { (void)vol; (void)uid; dbuf_calls++; }

static uint32_t *row(int seg) { return (uint32_t *)PMAP_SEGMAP_ROW(seg); }

static void reset_state(void)
{
    memset(&PMAP_$SEGMAP, 0, sizeof(PMAP_$SEGMAP));
    memset(test_mmapes, 0, sizeof(test_mmapes));
    memset(AST_$AOT.aote, 0, sizeof(AST_$AOT.aote));
    memset(&s1, 0, sizeof(s1)); memset(&s2, 0, sizeof(s2));
    s1.aote = &AST_$AOT.aote[0]; s1.segment = 3; s1.seg_index = 1; s1.fm_block = 0x7770;
    s2.aote = &AST_$AOT.aote[0]; s2.segment = 1; s2.seg_index = 2;
    AST_$AOTE_LIMIT = &AST_$AOT.aote[TEST_N_AOTES];
    AST_$UPDATE_SCAN = &AST_$AOT.aote[0];
    AST_$UPDATE_TIMESTAMP = 0xFFFF;
    AST_$GROW_AHEAD_CNT = 4;
    NETLOG_$OK_TO_LOG = 0; NETWORK_$REALLY_DISKLESS = 0;
    lock_calls = unlock_calls = 0; advance_calls = 0;
    waitn_calls = 0; log_calls = 0;
    fmw_calls = 0; fmw_status = status_$ok;
    alloc_calls = 0; alloc_status = status_$ok;
    reserve_calls = 0; reserve_status = status_$ok;
    clock_calls = 0; purify_calls = 0; dbuf_calls = 0;
}

TEST(update_aste_gating)
{
    status_$t status = 0x77;

    s1.flags = 0;
    ast_$update_aste(&s1, (segmap_entry_t *)row(1), 0, &status);
    ASSERT_EQ(status_$ok, status); ASSERT_EQ(0, fmw_calls);

    s1.flags = ASTE_FLAG_DIRTY | ASTE_FLAG_REMOTE;
    ast_$update_aste(&s1, (segmap_entry_t *)row(1), 0, &status);
    ASSERT_EQ(0, fmw_calls);
    ASSERT_EQ(ASTE_FLAG_DIRTY | ASTE_FLAG_REMOTE, s1.flags);
}

TEST(update_aste_image_and_write)
{
    status_$t status = 0;
    uint32_t *r = row(1);

    s1.flags = ASTE_FLAG_DIRTY;
    r[0] = 0x00001234;                          /* on disk */
    r[1] = 0x00401235;                          /* on disk, bit 22 */
    r[2] = SEGMAP_VALID | 0x300;                /* installed */
    test_mmapes[0x300].disk_addr = 0x00001236;
    r[3] = SEGMAP_VALID | 0x301;
    test_mmapes[0x301].disk_addr = 0x00401237;
    NETLOG_$OK_TO_LOG = -1;
    s1.page_count = 2;

    ast_$update_aste(&s1, (segmap_entry_t *)r, -1, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, s1.flags);
    ASSERT_EQ(1, fmw_calls);
    ASSERT_EQ(0x7770, fmw_block); ASSERT_EQ(3, fmw_level); ASSERT_EQ((int8_t)-1, fmw_now);
    ASSERT_EQ(0x00001234u, fmw_data[0]);
    ASSERT_EQ(0x80001235u, fmw_data[1]);
    ASSERT_EQ(0x00001236u, fmw_data[2]);
    ASSERT_EQ(0x80001237u, fmw_data[3]);
    ASSERT_EQ(0, fmw_data[4]);
    ASSERT_EQ(1, log_calls); ASSERT_EQ(12, log_kind[0]); ASSERT_EQ(3, log_p3[0]);
    ASSERT_EQ(2, log_p4[0]); ASSERT_EQ(2, log_p5[0]);
    ASSERT_EQ(1, lock_calls); ASSERT_EQ(1, unlock_calls);

    reset_state();
    s1.flags = ASTE_FLAG_DIRTY; fmw_status = status_$disk_write_protected;
    ast_$update_aste(&s1, (segmap_entry_t *)r, 0, &status);
    ASSERT_EQ(status_$ok, status); ASSERT_EQ(0, s1.flags);

    reset_state();
    s1.flags = ASTE_FLAG_DIRTY; fmw_status = 0x00080001;
    ast_$update_aste(&s1, (segmap_entry_t *)r, 0, &status);
    ASSERT_EQ(0x80080001u, (uint32_t)status); ASSERT_EQ(ASTE_FLAG_DIRTY, s1.flags);
}

TEST(setup_area_hints_and_extend)
{
    status_$t status = 0x77;
    uint32_t *r = row(1);
    aote_t *a = &AST_$AOT.aote[0];

    a->attr_flags_hi = 0x10; a->vol_index = 2; a->length = 0x1000;
    NETLOG_$OK_TO_LOG = -1;

    /* page 0: hint = fm_block >> 4 */
    ast_$setup_page_read(&s1, &r[0], 0, 2, 0, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, alloc_calls); ASSERT_EQ(2, alloc_vol); ASSERT_EQ(0x777, alloc_hint);
    ASSERT_EQ(2, alloc_count); ASSERT_EQ(0, alloc_res);
    ASSERT_EQ(0x00402000u, r[0]); ASSERT_EQ(0x00402001u, r[1]);
    ASSERT_EQ(2, log_calls); ASSERT_EQ(9, log_kind[0]); ASSERT_EQ(1, log_p4[1]);
    ASSERT_EQ(0x2001, log_p6[1]); ASSERT_EQ(1, log_p7[1]);
    ASSERT_EQ(ASTE_FLAG_DIRTY, s1.flags);
    /* (3*32 + 0 + 2 - 1) << 10 = 0x18400 >= 0x1000: extended */
    ASSERT_EQ(0x18800, a->length);
    ASSERT_EQ(1, clock_calls); ASSERT_EQ(0xC1, a->dta_high); ASSERT_EQ(0xC1, a->dtm_high);
    ASSERT_EQ(2, a->unknown_24);
    ASSERT_EQ(AOTE_FLAG_DIRTY, a->flags);

    /* page 2: previous entry on disk -> its address */
    reset_state();
    a->attr_flags_hi = 0x10; a->length = 0x100000;
    r[1] = 0x00005555;
    ast_$setup_page_read(&s1, &r[2], 2, 1, 0, &status);
    ASSERT_EQ(0x5555, alloc_hint);
    ASSERT_EQ(0x100000, a->length);
    ASSERT_EQ(AOTE_FLAG_TOUCHED | AOTE_FLAG_DIRTY, a->flags);   /* flags bit 6 clear */

    /* previous entry installed -> MMAPE's address; flags 0x40 -> no TOUCHED */
    reset_state();
    a->attr_flags_hi = 0x10; a->length = 0x100000;
    r[1] = SEGMAP_VALID | 0x300; test_mmapes[0x300].disk_addr = 0x00C06666;
    ast_$setup_page_read(&s1, &r[2], 2, 1, 0x40, &status);
    ASSERT_EQ(0x006666, alloc_hint);
    ASSERT_EQ(AOTE_FLAG_DIRTY, a->flags);

    /* allocation failure: bit 31, map untouched */
    reset_state();
    a->attr_flags_hi = 0x10; alloc_status = 0x00010002;
    ast_$setup_page_read(&s1, &r[4], 4, 1, 0, &status);
    ASSERT_EQ(0x80010002u, (uint32_t)status);
    ASSERT_EQ(0, r[4]); ASSERT_EQ(0, a->unknown_24);
}

TEST(setup_reserve_and_early_out)
{
    status_$t status = 0x77;
    uint32_t *r = row(1);
    aote_t *a = &AST_$AOT.aote[0];

    a->vol_index = 5; a->length = 0x100000;
    r[3] = 0x12345678;
    NETLOG_$OK_TO_LOG = -1;
    ast_$setup_page_read(&s1, &r[3], 3, 2, 0, &status);
    ASSERT_EQ(1, reserve_calls); ASSERT_EQ(5, reserve_vol); ASSERT_EQ(2, reserve_count);
    ASSERT_EQ(0, alloc_calls);
    ASSERT_EQ(0x12400000u, r[3]); ASSERT_EQ(0x00400000u, r[4]);
    ASSERT_EQ(1, log_calls); ASSERT_EQ(3, log_p4[0]); ASSERT_EQ(2, log_p7[0]);
    ASSERT_EQ(2, a->unknown_24);

    reset_state();
    a->attr_flags_lo = 0x02;
    ast_$setup_page_read(&s1, &r[3], 3, 2, 0, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, reserve_calls); ASSERT_EQ(0, s1.flags);
}

TEST(update_scan)
{
    aote_t *a = &AST_$AOT.aote[0];

    NETWORK_$REALLY_DISKLESS = -1;
    AST_$UPDATE();
    ASSERT_EQ(0, lock_calls);

    reset_state();
    a->attr_flags_hi = 0x10;
    s1.flags = ASTE_FLAG_DIRTY; s1.next = &s2; s2.flags = ASTE_FLAG_DIRTY | ASTE_FLAG_REMOTE;
    a->aste_list = &s1;
    AST_$AOT.aote[1].attr_flags_hi = 0x10; AST_$AOT.aote[1].ref_count = 1;    /* skipped */
    AST_$AOT.aote[2].attr_flags_hi = 0x10;
    AST_$UPDATE();
    /* s1 written (FM), s2 remote (update_aste returns clean); both AOTEs
     * purified; end of table -> DBUF flush and wrap */
    ASSERT_EQ(1, fmw_calls);
    ASSERT_EQ(2, purify_calls);
    ASSERT_EQ(1, dbuf_calls);
    ASSERT_EQ((uintptr_t)&AST_$AOT.aote[0], (uintptr_t)AST_$UPDATE_SCAN);
    ASSERT_EQ(0xFFFF, AST_$UPDATE_TIMESTAMP);
    ASSERT_EQ(0, s1.flags);
    ASSERT_EQ(4, advance_calls);                /* two ASTEs, two AOTEs */

    /* the timestamp gate: a segment above it is skipped */
    reset_state();
    a->attr_flags_hi = 0x10; s1.flags = ASTE_FLAG_DIRTY; a->aste_list = &s1;
    AST_$UPDATE_TIMESTAMP = 2;
    AST_$UPDATE();
    ASSERT_EQ(0, fmw_calls); ASSERT_EQ(ASTE_FLAG_DIRTY, s1.flags);
}

TEST(validate_uid_and_waits)
{
    uid_t u = { 0xAAAA, 0xBBBB };
    status_$t r = ast_$validate_uid(&u, 0x30F00);
    ASSERT_EQ(file_$object_not_found, r);
    ASSERT_EQ(0xAAAA, AST_$NOT_FOUND.uid.high); ASSERT_EQ(0xBBBB, AST_$NOT_FOUND.uid.low);
    ASSERT_EQ(0x30F00, AST_$NOT_FOUND.flags);

    AST_$AST_IN_TRANS_EC.value = 41;
    AST_$WAIT_FOR_AST_INTRANS();
    ASSERT_EQ(1, waitn_calls); ASSERT_EQ((uintptr_t)&AST_$AST_IN_TRANS_EC, (uintptr_t)waitn_ec);
    ASSERT_EQ(42, waitn_val); ASSERT_EQ(1, waitn_n);
    ASSERT_EQ(AST_LOCK_ID, unlock_ids[0]); ASSERT_EQ(AST_LOCK_ID, lock_ids[0]);

    AST_$PMAP_IN_TRANS_EC.value = 7;
    ast_$wait_for_page_transition();
    ASSERT_EQ(2, waitn_calls); ASSERT_EQ((uintptr_t)&AST_$PMAP_IN_TRANS_EC, (uintptr_t)waitn_ec);
    ASSERT_EQ(8, waitn_val);
    ASSERT_EQ(PMAP_LOCK_ID, unlock_ids[1]); ASSERT_EQ(PMAP_LOCK_ID, lock_ids[1]);
}

int main(void)
{
    printf("test_update_aste (ast_$update_aste, ast_$setup_page_read, AST_$UPDATE, ast_$validate_uid, waits)\n");

    RUN_TEST(update_aste_gating);
    RUN_TEST(update_aste_image_and_write);
    RUN_TEST(setup_area_hints_and_extend);
    RUN_TEST(setup_reserve_and_early_out);
    RUN_TEST(update_scan);
    RUN_TEST(validate_uid_and_waits);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
