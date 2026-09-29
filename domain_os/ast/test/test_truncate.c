/*
 * ast/test/test_truncate.c - Unit tests for AST_$TRUNCATE (0x00E05C40)
 *
 * The test #includes ast/truncate.c directly and drives the real routine
 * through mocks.  Pins:
 *
 *   - the working UID handed to the lookups: `bclr.b #0,(-0x4,A6)` at
 *     0x00E05CAA clears bit 0 of the FIRST byte of the low longword,
 *     i.e. bit 24 on the big-endian target - bit 0 itself survives;
 *   - "not found" is an error on the first pass and swallowed on the
 *     second (0x00E05CFE..0x00E05D14);
 *   - a read-only volume refuses with 0xF0016 after marking BUSY;
 *   - a local delete: VTOCE_$TRUNCATE with the delete byte, then
 *     ast_$process_aote(purge, no keep, wait) and ast_$release_aote, the
 *     result byte TRUE, and the second pass for the ACL UID with `force`.
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

#include "ast/truncate.c"

#define TEST_N_PAGES 32
#define TEST_N_FRAMES 0x400
/* The AST_ module blocks (ast/ast.h) and the segment map (pmap/pmap.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);
MODULE_DATA_DEFINE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);
MODULE_DATA_DEFINE(mmap_$mmape_table_t, MMAP_$MMAPE, 0x00EB4800);
uint16_t  PROC1_$CURRENT;
#include "proc1/proc1.h"
MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);

static aote_t test_aote;

static int inhibit_begin, inhibit_end;
void PROC1_$INHIBIT_BEGIN(void) { inhibit_begin++; }
void PROC1_$INHIBIT_END(void)   { inhibit_end++; }
static int lock_calls, unlock_calls;
void ML_$LOCK(int16_t id)   { lock_calls++; (void)id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; (void)id; }

#define MAX_REC 4
static int lookup_calls; static uid_t lookup_uid[MAX_REC]; static aote_t *lookup_result[MAX_REC];
aote_t *ast_$lookup_aote_by_uid(uid_t *uid)
{
    aote_t *r = (lookup_calls < MAX_REC) ? lookup_result[lookup_calls] : NULL;
    if (lookup_calls < MAX_REC) { lookup_uid[lookup_calls] = *uid; }
    lookup_calls++;
    return r;
}
static int force_calls; static status_$t force_status;
aote_t *ast_$force_activate_segment(uid_t *uid, uint32_t loc, status_$t *st, int8_t f)
{
    (void)uid; (void)loc; (void)f; force_calls++; *st = force_status; return NULL;
}
static int set_attr_calls;
void AST_$SET_ATTRIBUTE(uid_t *uid, uint16_t attr, void *value, status_$t *st)
{
    (void)uid; (void)attr; (void)value; set_attr_calls++; *st = status_$ok;
}
void AST_$WAIT_FOR_AST_INTRANS(void) { }
void ast_$wait_for_page_transition(void) { }
static int deact_calls;
void AST_$DEACTIVATE_SEGMENT(aste_t *a, int8_t purge, int8_t keep, status_$t *st)
{
    (void)a; (void)purge; (void)keep; deact_calls++; *st = status_$ok;
}
void AST_$FREE_ASTE(aste_t *a) { (void)a; }
void MMU_$REMOVE(uint32_t ppn) { (void)ppn; }
void MMAP_$FREE_REMOVE(mmape_t *m, uint32_t ppn) { (void)m; (void)ppn; }
static int crash_calls;
void CRASH_SYSTEM(const status_$t *s) { (void)s; crash_calls++; }
static int update_calls;
void ast_$update_aste(aste_t *a, segmap_entry_t *m, boolean w, status_$t *st)
{
    (void)a; (void)m; (void)w; update_calls++; *st = status_$ok;
}
static int bat_free_calls;
void BAT_$FREE(uint32_t *b, int16_t n, int16_t vol, int16_t r, status_$t *st)
{
    (void)b; (void)n; (void)vol; (void)r; bat_free_calls++; *st = status_$ok;
}
static int advance_calls;
void EC_$ADVANCE(ec_$eventcount_t *ec) { (void)ec; advance_calls++; }
static int clock_calls;
void TIME_$CLOCK(clock_t *c) { clock_calls++; c->high = 0x1111; c->low = 0x22; }
static int vtoce_calls; static uint32_t vtoce_size; static int32_t vtoce_rounded, vtoce_delete;
void VTOCE_$TRUNCATE(vtoc_$lookup_req_t *loc, uint32_t size, int32_t rounded, boolean del,
                     uint32_t *freed, status_$t *st)
{
    (void)loc; vtoce_calls++; vtoce_size = size; vtoce_rounded = rounded; vtoce_delete = del;
    *freed = 0; *st = status_$ok;
}
static int process_calls; static boolean process_purge, process_keep, process_wait;
uint16_t ast_$process_aote(aote_t *a, boolean purge, boolean keep, boolean wait, status_$t *st)
{
    (void)a; process_calls++; process_purge = purge; process_keep = keep; process_wait = wait;
    *st = status_$ok; return 0;
}
static int release_calls;
void ast_$release_aote(aote_t *a) { (void)a; release_calls++; }
static int rem_calls;
void REM_FILE_$TRUNCATE(uid_t *vol, uid_t *uid, uint32_t size, uint8_t del, clock_t *dtm, status_$t *st)
{
    (void)vol; (void)uid; (void)size; (void)del; dtm->high = 0; dtm->low = 0; rem_calls++; *st = status_$ok;
}

static void reset_state(void)
{
    memset(&PMAP_$SEGMAP, 0, sizeof(PMAP_$SEGMAP));
    memset(&MMAP_$MMAPE, 0, sizeof(MMAP_$MMAPE));
    memset(&test_aote, 0, sizeof(test_aote));
    memset(lookup_uid, 0, sizeof(lookup_uid));
    memset(lookup_result, 0, sizeof(lookup_result));
    memset(PROC1_$DATA.type, 0, sizeof(PROC1_$DATA.type));
    PROC1_$CURRENT = 2;
    lookup_calls = force_calls = set_attr_calls = deact_calls = crash_calls = 0;
    update_calls = bat_free_calls = advance_calls = clock_calls = vtoce_calls = 0;
    process_calls = release_calls = rem_calls = 0;
    inhibit_begin = inhibit_end = lock_calls = unlock_calls = 0;
    force_status = file_$object_not_found;
    test_aote.length = 0x8000;
}

TEST(uid_bit24_cleared_for_lookup)
{
    uid_t uid = { 0x01010101, 0x01FF00FF };
    status_$t status = 0; boolean result = 0x55;

    AST_$TRUNCATE(&uid, 0x100, 0, &result, &status);

    ASSERT_EQ(1, lookup_calls); ASSERT_EQ(1, force_calls);
    ASSERT_EQ(0x01010101, lookup_uid[0].high);
    ASSERT_EQ(0x00FF00FF, lookup_uid[0].low);        /* bit 24 off, bit 0 kept */
    ASSERT_EQ(0x01FF00FF, uid.low);                  /* the caller's cell untouched */
    ASSERT_EQ(file_$object_not_found, status);       /* first pass: an error */
    ASSERT_EQ(0, result);
    ASSERT_EQ(1, inhibit_begin); ASSERT_EQ(1, inhibit_end);
    ASSERT_EQ(1, lock_calls); ASSERT_EQ(1, unlock_calls);
}

TEST(read_only_volume_refused)
{
    uid_t uid = { 0x1234, 0x5678 };
    status_$t status = 0; boolean result = 0;

    lookup_result[0] = &test_aote;
    test_aote.attr_flags_lo = 0x02;
    AST_$TRUNCATE(&uid, 0x100, 0, &result, &status);

    ASSERT_EQ(status_$file_volume_has_been_mounted_read_only, status);
    ASSERT_EQ(AOTE_FLAG_BUSY, test_aote.flags);
    ASSERT_EQ(0, vtoce_calls); ASSERT_EQ(0, advance_calls);
    ASSERT_EQ(1, unlock_calls); ASSERT_EQ(1, inhibit_end);
}

TEST(local_delete_then_second_pass)
{
    uid_t uid = { 0x1234, 0x5678 };
    status_$t status = 0x77; boolean result = 0;

    lookup_result[0] = &test_aote;                   /* pass 1: found */
    lookup_result[1] = NULL;                         /* pass 2: not found */
    test_aote.acl_uid.high = 0xAA000001; test_aote.acl_uid.low = 0x01000002;
    AST_$TRUNCATE(&uid, 0x100, 0x01, &result, &status);

    /* pass 1 */
    ASSERT_EQ(0xFF, (uint8_t)result);
    ASSERT_EQ(1, vtoce_calls); ASSERT_EQ(0, vtoce_size);           /* a delete truncates to 0 */
    ASSERT_EQ(0, vtoce_rounded); ASSERT_EQ(-1, vtoce_delete);
    ASSERT_EQ(0, clock_calls);                                     /* no length stamp on a delete */
    ASSERT_EQ(1, process_calls);
    ASSERT_EQ(0xFF, (uint8_t)process_purge); ASSERT_EQ(0, process_keep); ASSERT_EQ(0xFF, (uint8_t)process_wait);
    ASSERT_EQ(1, release_calls);
    ASSERT_EQ(AOTE_FLAG_BUSY, test_aote.flags);                    /* IN_TRANS cleared */
    /* pass 2: the ACL UID, bit 24 of its low longword cleared; "not found"
     * is swallowed */
    ASSERT_EQ(2, lookup_calls);
    ASSERT_EQ(0xAA000001, lookup_uid[1].high);
    ASSERT_EQ(0x00000002, lookup_uid[1].low);
    ASSERT_EQ(1, force_calls);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, rem_calls);
    /* pass 1 releases and retakes the AST lock around VTOCE_$TRUNCATE */
    ASSERT_EQ(3, lock_calls); ASSERT_EQ(3, unlock_calls);
    ASSERT_EQ(1, inhibit_begin); ASSERT_EQ(1, inhibit_end);
}

int main(void)
{
    printf("test_truncate (AST_$TRUNCATE 0x00E05C40)\n");
    RUN_TEST(uid_bit24_cleared_for_lookup);
    RUN_TEST(read_only_volume_refused);
    RUN_TEST(local_delete_then_second_pass);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
