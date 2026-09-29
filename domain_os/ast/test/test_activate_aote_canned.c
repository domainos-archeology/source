/*
 * ast/test/test_activate_aote_canned.c - Unit tests for
 *                                        AST_$ACTIVATE_AOTE_CANNED (0x00E2F0C2)
 *
 * The test #includes ast/activate_aote_canned.c directly and drives the real
 * routine through mocked ML_$LOCK / ML_$UNLOCK, ast_$allocate_aote,
 * UID_$HASH and CRASH_SYSTEM.  It pins:
 *
 *   - the location word: remote objects get bit 31 | (bits 31..20 kept
 *     from the fresh AOTE with bits 25..20 cleared) | node; local objects
 *     get the block hint verbatim (0x00E2F112..0x00E2F13A);
 *   - the 36-longword attribute copy to aote+0x0C and the 8-longword
 *     obj_loc copy to aote+0x9C;
 *   - UID_$HASH is given aote->uid (aote+0x10) and the 251-bucket size
 *     word at 0x00E2F1CE;
 *   - duplicate detection compares aote+0x10, not the obj_loc copy, and
 *     the new entry goes on the head of the bucket.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Avoid the macOS uid_t conflict - must come AFTER the system includes. */
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

#include "ast/activate_aote_canned.c"

/* ==========================================================================
 * Host storage
 * ========================================================================== */

#define TEST_BUCKETS 251
/* The AST_ module blocks (ast/ast.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);

static aote_t fresh_aote;
static aote_t other_aote;

/* The crash status cell ast/ast_data.c owns (image bytes 80 03 00 03). */
status_$t status_$t_00e2f1d0 = (status_$t)0x80030003;

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

static int lock_calls, unlock_calls;
static int16_t last_lock_id, last_unlock_id;
void ML_$LOCK(int16_t id)   { lock_calls++; last_lock_id = id; }
void ML_$UNLOCK(int16_t id) { unlock_calls++; last_unlock_id = id; }

static int alloc_calls;
aote_t *ast_$allocate_aote(void)
{
    alloc_calls++;
    return &fresh_aote;
}

static uid_t   *hash_uid_arg;
static uint16_t hash_size_arg;
static uint32_t hash_result;
uint32_t UID_$HASH(uid_t *uid, uint16_t *table_size)
{
    hash_uid_arg = uid;
    hash_size_arg = *table_size;
    return hash_result;
}

static int crash_calls;
static const status_$t *crash_status;
void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_calls++;
    crash_status = status_p;
    /* the image never returns from here; the routine would re-test the
     * same chain entry forever (bra 0x00E2F1A4 without advancing A2), so
     * make the entry stop matching so the walk moves on and finishes */
    other_aote.uid.high ^= 0xFFFFFFFFu;
}

static void reset_state(void)
{
    memset(AST_$DATA.aoth, 0, sizeof(AST_$DATA.aoth));
    memset(&fresh_aote, 0xA5, sizeof(fresh_aote));   /* stale contents */
    memset(&other_aote, 0, sizeof(other_aote));
    lock_calls = unlock_calls = 0;
    alloc_calls = 0;
    hash_uid_arg = NULL;
    hash_size_arg = 0;
    hash_result = 7;
    crash_calls = 0;
    crash_status = NULL;
}

static void fill_inputs(uint32_t attrs[36], file_$obj_loc_t *loc,
                        uint32_t uid_high, uint32_t uid_low)
{
    int i;
    for (i = 0; i < 36; i++) {
        attrs[i] = 0x01000000u * (uint32_t)(i + 1) + (uint32_t)i;
    }
    /* aote->uid is at record offset 0x10 - 0x0C = longwords 1 and 2 */
    attrs[1] = uid_high;
    attrs[2] = uid_low;
    memset(loc, 0, sizeof(*loc));
    loc->block_hint = 0x00001234;
    loc->uid.high = 0xDEADBEEF;
    loc->uid.low = 0xFEEDFACE;
    loc->loc_info = 0x11111111;
    loc->node = 0x00012345;
    loc->rights_bits = 0x5A;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

TEST(local_object_fields_and_copies)
{
    uint32_t attrs[36];
    file_$obj_loc_t loc;
    uint32_t *dst;
    int i;

    fill_inputs(attrs, &loc, 0x10101010, 0x20202020);
    loc.flags = 0;                          /* local */

    AST_$ACTIVATE_AOTE_CANNED(attrs, (uint32_t *)&loc);

    ASSERT_EQ(1, alloc_calls);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(AST_LOCK_ID, last_lock_id);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(AST_LOCK_ID, last_unlock_id);

    /* four bclr.b on 0xBF: 0xA5 & ~0xF0 */
    ASSERT_EQ(0x05, fresh_aote.flags);
    ASSERT_EQ(1, fresh_aote.ref_count);
    ASSERT_EQ(0, fresh_aote.status_flags);
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)fresh_aote.aste_list);

    /* local: location := block hint */
    ASSERT_EQ(0x00001234, fresh_aote.location);

    /* 36 longwords from aote+0x0C */
    dst = (uint32_t *)&fresh_aote.obj_type;
    for (i = 0; i < 36; i++) {
        ASSERT_EQ(attrs[i], dst[i]);
    }
    ASSERT_EQ(0x10101010, fresh_aote.uid.high);
    ASSERT_EQ(0x20202020, fresh_aote.uid.low);

    /* 8 longwords from aote+0x9C */
    dst = (uint32_t *)&fresh_aote.obj_uid;
    for (i = 0; i < 8; i++) {
        ASSERT_EQ(((uint32_t *)&loc)[i], dst[i]);
    }
    ASSERT_EQ(0xDEADBEEF, fresh_aote.obj_loc_uid.high);
    ASSERT_EQ(0x5A, fresh_aote.vol_index);

    /* hashed on aote->uid with the 251 constant */
    ASSERT_EQ((uintptr_t)&fresh_aote.uid, (uintptr_t)hash_uid_arg);
    ASSERT_EQ(0x00FB, hash_size_arg);

    /* pushed on bucket 7, chain was empty */
    ASSERT_EQ((uintptr_t)&fresh_aote, (uintptr_t)AST_$DATA.aoth[7]);
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)fresh_aote.hash_next);
    ASSERT_EQ(0, crash_calls);
}

TEST(remote_object_location_word)
{
    uint32_t attrs[36];
    file_$obj_loc_t loc;

    fill_inputs(attrs, &loc, 1, 2);
    loc.flags = (int8_t)0x80;               /* remote */
    fresh_aote.location = 0xFFFFFFFF;       /* stale bits everywhere */

    AST_$ACTIVATE_AOTE_CANNED(attrs, (uint32_t *)&loc);

    /* bit 31 set; bits 25..20 cleared by the andi.w #0xFC0F on the high
     * word; bits 19..0 cleared then replaced by the node */
    ASSERT_EQ(0xFC000000u | 0x00012345u, fresh_aote.location);
}

TEST(insert_at_head_of_existing_chain)
{
    uint32_t attrs[36];
    file_$obj_loc_t loc;

    fill_inputs(attrs, &loc, 0xAAAA, 0xBBBB);
    loc.flags = 0;
    other_aote.uid.high = 0x1;              /* different UID */
    other_aote.uid.low = 0x2;
    AST_$DATA.aoth[7] = &other_aote;

    AST_$ACTIVATE_AOTE_CANNED(attrs, (uint32_t *)&loc);

    ASSERT_EQ((uintptr_t)&fresh_aote, (uintptr_t)AST_$DATA.aoth[7]);
    ASSERT_EQ((uintptr_t)&other_aote, (uintptr_t)fresh_aote.hash_next);
    ASSERT_EQ(0, crash_calls);
}

TEST(duplicate_uid_crashes)
{
    uint32_t attrs[36];
    file_$obj_loc_t loc;

    fill_inputs(attrs, &loc, 0xAAAA, 0xBBBB);
    loc.flags = 0;
    other_aote.uid.high = 0xAAAA;           /* same UID at aote+0x10 */
    other_aote.uid.low = 0xBBBB;
    /* a different obj_loc copy must not matter */
    other_aote.obj_loc_uid.high = 0x9999;
    AST_$DATA.aoth[7] = &other_aote;

    AST_$ACTIVATE_AOTE_CANNED(attrs, (uint32_t *)&loc);

    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ((uintptr_t)&status_$t_00e2f1d0, (uintptr_t)crash_status);
    ASSERT_EQ(0x80030003u, (uint32_t)*crash_status);
}

int main(void)
{
    printf("test_activate_aote_canned (AST_$ACTIVATE_AOTE_CANNED 0x00E2F0C2)\n");

    RUN_TEST(local_object_fields_and_copies);
    RUN_TEST(remote_object_location_word);
    RUN_TEST(insert_at_head_of_existing_chain);
    RUN_TEST(duplicate_uid_crashes);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
