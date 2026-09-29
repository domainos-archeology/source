/*
 * ast/test/test_lookup_or_create_aste.c - Unit tests for
 *                                         ast_$lookup_or_create_aste (0x00E0255C)
 *
 * The test #includes ast/lookup_or_create_aste.c directly and drives the
 * real routine through mocked callees.  It pins:
 *
 *   - a dismounting local volume is refused through ast_$validate_uid
 *     before any counter moves;
 *   - the new ASTE's flags (bit 15 set, 14/13/12 cleared, bit 11 = remote),
 *     the R/L counters, segment at +0x0C;
 *   - insertion into the descending list at head / middle / tail, and the
 *     "already exists" path that frees the fresh ASTE (after waiting out a
 *     transition) and returns the existing one;
 *   - a remote object's map is zeroed; a local one goes through
 *     VTOCE_$LOOKUP_FM + FM_$READ, the block count grows aote+0x24 and
 *     dirties the AOTE, and the 32 entries are normalised;
 *   - failure unlinks and frees the ASTE, re-judging 0x20006;
 *   - the volume hold and ref_count are released on every path but the
 *     early refusal, waking AST_$DISM_EC for a dismounting volume.
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

#include "ast/lookup_or_create_aste.c"

#define TEST_N_PAGES 32
/* The AST_ module blocks (ast/ast.h) and the segment map (pmap/pmap.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);
MODULE_DATA_DEFINE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);

int8_t    NETLOG_$OK_TO_LOG;

static aote_t test_aote;
static aste_t fresh;                 /* what AST_$ALLOCATE_ASTE hands out */
static aste_t e1, e2, e3;            /* existing list entries */

static int lock_calls, unlock_calls;
void ML_$LOCK(int16_t id)   { lock_calls++; (void)id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; (void)id; }

static int alloc_calls;
aste_t *AST_$ALLOCATE_ASTE(void) { alloc_calls++; return &fresh; }

static int free_calls;
static aste_t *free_aste;
void AST_$FREE_ASTE(aste_t *aste) { free_calls++; free_aste = aste; }

static int wait_calls;
static aste_t *wait_clear;
static int wait_sets_dismount_bit;
void AST_$WAIT_FOR_AST_INTRANS(void)
{
    wait_calls++;
    if (wait_clear != NULL) { wait_clear->flags &= (uint16_t)~ASTE_FLAG_IN_TRANS; }
    if (wait_sets_dismount_bit) { AST_$DATA.vol_info_count |= 1u << 3; }
}

static int advance_calls;
static ec_$eventcount_t *advance_last;
void EC_$ADVANCE(ec_$eventcount_t *ec) { advance_calls++; advance_last = ec; }

static int       validate_calls;
static uint32_t  validate_flags;
static status_$t validate_result;
status_$t ast_$validate_uid(uid_t *uid, uint32_t flags)
{
    (void)uid;
    validate_calls++;
    validate_flags = flags;
    return validate_result;
}

static int       lookup_fm_calls;
static uint16_t  lookup_fm_block_num;
static uint32_t  lookup_fm_phys;
static uint32_t  lookup_fm_count;
static status_$t lookup_fm_status;
void VTOCE_$LOOKUP_FM(void *vtoce_loc, uint16_t block_num, uint16_t flags,
                      uint32_t *phys_block, uint32_t *alloc_count,
                      status_$t *status)
{
    (void)vtoce_loc; (void)flags;
    lookup_fm_calls++;
    lookup_fm_block_num = block_num;
    *phys_block = lookup_fm_phys;
    *alloc_count = lookup_fm_count;
    *status = lookup_fm_status;
}

static int       fm_read_calls;
static uint32_t  fm_read_block;
static uint16_t  fm_read_level;
static status_$t fm_read_status;
static uint32_t  fm_read_data[32];
void FM_$READ(fm_$file_ref_t *file_ref, uint32_t block_addr, uint16_t level,
              fm_$entry_t *entry_out, status_$t *status)
{
    (void)file_ref;
    fm_read_calls++;
    fm_read_block = block_addr;
    fm_read_level = level;
    memcpy(entry_out, fm_read_data, sizeof(fm_read_data));
    *status = fm_read_status;
}

static int log_calls;
void NETLOG_$LOG_IT(uint16_t kind, uint32_t *uid, uint16_t p3, uint16_t p4,
                    uint16_t p5, uint16_t p6, uint16_t p7, uint16_t p8)
{
    (void)kind; (void)uid; (void)p3; (void)p4; (void)p5; (void)p6; (void)p7; (void)p8;
    log_calls++;
}

static void reset_state(void)
{
    memset(&PMAP_$SEGMAP, 0xAA, sizeof(PMAP_$SEGMAP));
    memset(AST_$DATA.vol_indices, 0, sizeof(AST_$DATA.vol_indices));
    AST_$DATA.vol_info_count = 0;
    AST_$ASTE_R_CNT = 10; AST_$ASTE_L_CNT = 20;
    NETLOG_$OK_TO_LOG = 0;
    memset(&test_aote, 0, sizeof(test_aote));
    test_aote.vol_index = 3;
    test_aote.ref_count = 1;
    memset(&fresh, 0, sizeof(fresh));
    fresh.flags = 0xFFFF;                 /* stale bits everywhere */
    fresh.seg_index = 2;                  /* row 2 = PMAP_$SEGMAP.row[1] */
    memset(&e1, 0, sizeof(e1)); memset(&e2, 0, sizeof(e2)); memset(&e3, 0, sizeof(e3));
    lock_calls = unlock_calls = 0;
    alloc_calls = 0; free_calls = 0; free_aste = NULL;
    wait_calls = 0; wait_clear = NULL; wait_sets_dismount_bit = 0;
    advance_calls = 0; advance_last = NULL;
    validate_calls = 0; validate_flags = 0; validate_result = 0x30F00;
    lookup_fm_calls = 0; lookup_fm_block_num = 0; lookup_fm_phys = 0x5000;
    lookup_fm_count = 0; lookup_fm_status = status_$ok;
    fm_read_calls = 0; fm_read_status = status_$ok;
    memset(fm_read_data, 0, sizeof(fm_read_data));
    log_calls = 0;
}

static uint32_t *row2(void) { return (uint32_t *)PMAP_SEGMAP_ROW(2); }

TEST(dismounting_volume_refused)
{
    status_$t status = 0;
    aste_t *r;

    AST_$DATA.vol_info_count = 1u << 3;
    r = ast_$lookup_or_create_aste(&test_aote, 4, &status);

    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)r);
    ASSERT_EQ(1, validate_calls);
    ASSERT_EQ(0x30F00, validate_flags);
    ASSERT_EQ(0x30F00, status);
    ASSERT_EQ(0, alloc_calls);
    ASSERT_EQ(0, AST_$DATA.vol_indices[3]);
    ASSERT_EQ(1, test_aote.ref_count);
}

TEST(create_local_head_insert_and_normalise)
{
    status_$t status = 0x77;
    aste_t *r;
    uint32_t *row = row2();
    int i;

    NETLOG_$OK_TO_LOG = -1;
    lookup_fm_count = 5;
    test_aote.unknown_24 = 100;
    fm_read_data[0] = 0x80001234;         /* bit 31 -> bit 22 */
    fm_read_data[1] = 0x7FFFFFFF;         /* bits 30..23 cleared */
    fm_read_data[2] = 0x00000042;

    r = ast_$lookup_or_create_aste(&test_aote, 4, &status);

    ASSERT_EQ((uintptr_t)&fresh, (uintptr_t)r);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, alloc_calls);
    ASSERT_EQ(1, log_calls);
    /* flags: stale 0xFFFF, bits 14/13/12/11 cleared, bit 15 cleared at
     * the end */
    ASSERT_EQ(0x07FF, fresh.flags);
    ASSERT_EQ(4, fresh.segment);
    ASSERT_EQ((uintptr_t)&test_aote, (uintptr_t)fresh.aote);
    ASSERT_EQ(0, fresh.page_count);
    ASSERT_EQ(21, AST_$ASTE_L_CNT);
    ASSERT_EQ(10, AST_$ASTE_R_CNT);
    ASSERT_EQ((uintptr_t)&fresh, (uintptr_t)test_aote.aste_list);
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)fresh.next);
    ASSERT_EQ(1, test_aote.status_flags);
    /* the I/O */
    ASSERT_EQ(1, lookup_fm_calls);
    ASSERT_EQ(4, lookup_fm_block_num);
    ASSERT_EQ(0x5000, fresh.fm_block);
    ASSERT_EQ(105, test_aote.unknown_24);
    ASSERT_EQ(AOTE_FLAG_DIRTY, test_aote.flags);
    ASSERT_EQ(1, fm_read_calls);
    ASSERT_EQ(0x5000, fm_read_block);
    ASSERT_EQ(4, fm_read_level);
    /* normalised entries */
    ASSERT_EQ(0x00401234u, row[0]);
    ASSERT_EQ(0x007FFFFFu, row[1]);
    ASSERT_EQ(0x00000042u, row[2]);
    for (i = 3; i < 32; i++) { ASSERT_EQ(0, row[i]); }
    /* locks: AST unlock, PMAP lock/unlock, AST lock */
    ASSERT_EQ(2, lock_calls);
    ASSERT_EQ(2, unlock_calls);
    ASSERT_EQ(1, advance_calls);
    ASSERT_EQ((uintptr_t)&AST_$AST_IN_TRANS_EC, (uintptr_t)advance_last);
    /* holds released */
    ASSERT_EQ(0, AST_$DATA.vol_indices[3]);
    ASSERT_EQ(1, test_aote.ref_count);
}

TEST(create_remote_zeroes_map)
{
    status_$t status = 0x77;
    aste_t *r;
    uint32_t *row = row2();
    int i;

    test_aote.remote_flag = (int8_t)0x80;
    r = ast_$lookup_or_create_aste(&test_aote, 9, &status);

    ASSERT_EQ((uintptr_t)&fresh, (uintptr_t)r);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x0FFF, fresh.flags);       /* bit 11 set, bit 15 cleared */
    ASSERT_EQ(11, AST_$ASTE_R_CNT);
    ASSERT_EQ(20, AST_$ASTE_L_CNT);
    for (i = 0; i < 32; i++) { ASSERT_EQ(0, row[i]); }
    ASSERT_EQ(0, lookup_fm_calls);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(0, AST_$DATA.vol_indices[3]);   /* never held for remote */
}

TEST(middle_and_tail_insert)
{
    status_$t status = 0;
    aste_t *r;

    test_aote.remote_flag = (int8_t)0x80;
    e1.segment = 9; e1.next = &e2;
    e2.segment = 3; e2.next = NULL;
    test_aote.aste_list = &e1;

    r = ast_$lookup_or_create_aste(&test_aote, 5, &status);
    ASSERT_EQ((uintptr_t)&fresh, (uintptr_t)r);
    ASSERT_EQ((uintptr_t)&fresh, (uintptr_t)e1.next);
    ASSERT_EQ((uintptr_t)&e2, (uintptr_t)fresh.next);

    /* tail: below everything */
    reset_state();
    test_aote.remote_flag = (int8_t)0x80;
    e1.segment = 9; e1.next = &e2;
    e2.segment = 3; e2.next = NULL;
    test_aote.aste_list = &e1;
    r = ast_$lookup_or_create_aste(&test_aote, 1, &status);
    ASSERT_EQ((uintptr_t)&fresh, (uintptr_t)e2.next);
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)fresh.next);
}

TEST(existing_after_transition)
{
    status_$t status = 0x77;
    aste_t *r;

    test_aote.remote_flag = (int8_t)0x80;
    e1.segment = 9; e1.next = &e2;
    e2.segment = 5; e2.flags = ASTE_FLAG_IN_TRANS; e2.next = NULL;
    test_aote.aste_list = &e1;
    wait_clear = &e2;

    r = ast_$lookup_or_create_aste(&test_aote, 5, &status);

    ASSERT_EQ((uintptr_t)&e2, (uintptr_t)r);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, wait_calls);
    ASSERT_EQ(1, free_calls);
    ASSERT_EQ((uintptr_t)&fresh, (uintptr_t)free_aste);
    ASSERT_EQ(11, AST_$ASTE_R_CNT);        /* counted before the discovery */
    ASSERT_EQ(1, test_aote.ref_count);
    ASSERT_EQ(0, test_aote.status_flags);
}

TEST(fm_read_failure_unlinks)
{
    status_$t status = 0;
    aste_t *r;

    e1.segment = 9; e1.next = NULL;
    test_aote.aste_list = &e1;
    fm_read_status = 0x20006;
    validate_result = 0x000F0001;

    r = ast_$lookup_or_create_aste(&test_aote, 4, &status);

    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)r);
    ASSERT_EQ(1, validate_calls);
    ASSERT_EQ(0x20006, validate_flags);
    ASSERT_EQ(0x000F0001, status);
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)e1.next);     /* unlinked */
    ASSERT_EQ(0, test_aote.status_flags);
    ASSERT_EQ(1, free_calls);
    ASSERT_EQ(0, advance_calls);
    ASSERT_EQ(0, AST_$DATA.vol_indices[3]);
    ASSERT_EQ(1, test_aote.ref_count);
}

/* The wait hook can start a dismount while the hold is in place. */
TEST(last_hold_wakes_dismount)
{
    status_$t status = 0;

    e1.segment = 4; e1.next = NULL;
    e1.flags = ASTE_FLAG_IN_TRANS;        /* forces one wait */
    test_aote.aste_list = &e1;
    wait_clear = &e1;
    wait_sets_dismount_bit = 1;

    ast_$lookup_or_create_aste(&test_aote, 4, &status);

    ASSERT_EQ(1, wait_calls);
    ASSERT_EQ(0, AST_$DATA.vol_indices[3]);
    ASSERT_EQ(1, advance_calls);
    ASSERT_EQ((uintptr_t)&AST_$DISM_EC, (uintptr_t)advance_last);

    /* with another hold outstanding there is no wake */
    reset_state();
    e1.segment = 4; e1.next = NULL; e1.flags = ASTE_FLAG_IN_TRANS;
    test_aote.aste_list = &e1;
    wait_clear = &e1;
    wait_sets_dismount_bit = 1;
    AST_$DATA.vol_indices[3] = 1;
    ast_$lookup_or_create_aste(&test_aote, 4, &status);
    ASSERT_EQ(1, AST_$DATA.vol_indices[3]);
    ASSERT_EQ(0, advance_calls);
}

int main(void)
{
    printf("test_lookup_or_create_aste (ast_$lookup_or_create_aste 0x00E0255C)\n");

    RUN_TEST(dismounting_volume_refused);
    RUN_TEST(create_local_head_insert_and_normalise);
    RUN_TEST(create_remote_zeroes_map);
    RUN_TEST(middle_and_tail_insert);
    RUN_TEST(existing_after_transition);
    RUN_TEST(fm_read_failure_unlinks);
    RUN_TEST(last_hold_wakes_dismount);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
