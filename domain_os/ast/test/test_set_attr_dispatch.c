/*
 * ast/test/test_set_attr_dispatch.c - Unit tests for the Pascal procedure
 * nested inside ast_$set_attribute_internal (AST_$SET_ATTR_DISPATCH,
 * 0x00E04B00) and for its parent.
 *
 * The tests include ast/set_attribute_internal.c directly, so they exercise
 * the real dispatcher (a static function in that translation unit) with the
 * real aote_t layout, through mocked callees.
 *
 * They pin the behaviours the 2026-09-06 audit found wrong:
 *   - the purify/refcount/truncate epilogue is gated on needs_purify (D2b at
 *     0xE0514C), so a BLOCKS change must not run it and an ACL change must;
 *   - the two ACL "is it nil" tests look at the top byte only (0xE0518E and
 *     0xE051C6), so a UID whose top byte is zero but whose other bytes are not
 *     counts as nil;
 *   - AST_$TRUNCATE receives a real byte out-parameter and the caller's status
 *     pointer as arguments 4 and 5 (0xE051D4/0xE051D0);
 *   - status 0x000F0001 coming back from AST_$TRUNCATE is rewritten to
 *     status_$ok (0xE051EE).
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

/* Avoid the host's uid_t; must come after the system headers. */
#define uid_t ast_uid_t

/* ------------------------------------------------------------------ */
/* Test framework                                                      */
/* ------------------------------------------------------------------ */

static int tests_passed = 0;
static int tests_failed = 0;
static int test_failed_flag = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                          \
    printf("  Running %s... ", #name);               \
    test_failed_flag = 0;                            \
    reset_mocks();                                   \
    test_##name();                                   \
    if (test_failed_flag) { tests_failed++; }        \
    else { tests_passed++; printf("PASSED\n"); }     \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                    \
    if ((unsigned long)(expected) != (unsigned long)(actual)) {             \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n",      \
               (unsigned long)(expected), (unsigned long)(actual),          \
               __LINE__);                                                   \
        test_failed_flag = 1;                                               \
        return;                                                             \
    }                                                                       \
} while (0)

#define ASSERT_TRUE(cond) do {                                              \
    if (!(cond)) {                                                          \
        printf("FAILED\n    Assertion failed at line %d: %s\n",             \
               __LINE__, #cond);                                            \
        test_failed_flag = 1;                                               \
        return;                                                             \
    }                                                                       \
} while (0)

/* ------------------------------------------------------------------ */
/* Real headers, mocked callees                                        */
/* ------------------------------------------------------------------ */

#include "ast/ast_internal.h"

/* Globals the AST subsystem expects from elsewhere. */
uid_t UID_$NIL = { 0, 0 };
/* The AST_ module blocks (ast/ast.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);

/* Mock state. */
static int mock_lock_depth[64];
static int mock_purify_calls;
static aote_t *mock_purify_aote_arg;
static status_$t mock_purify_status;

static int mock_truncate_calls;
static uid_t mock_truncate_uid;
static uint32_t mock_truncate_len;
static uint16_t mock_truncate_flags;
static boolean *mock_truncate_out;
static status_$t *mock_truncate_status_ptr;
static status_$t mock_truncate_status;

static int mock_abs_clock_calls;
static void *mock_abs_clock_arg;

static int mock_rem_file_calls;

/*
 * PROC1 globals read by ast_$set_attribute_internal
 * (0xE05252 "PROC1_$DATA.type[PROC1_$CURRENT]").
 */
uint16_t PROC1_$CURRENT = 0;
#include "proc1/proc1.h"
MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);

static void reset_mocks(void)
{
    memset(mock_lock_depth, 0, sizeof(mock_lock_depth));
    mock_purify_calls = 0;
    mock_purify_aote_arg = NULL;
    mock_purify_status = 0;
    mock_truncate_calls = 0;
    memset(&mock_truncate_uid, 0, sizeof(mock_truncate_uid));
    mock_truncate_len = 0xDEADBEEF;
    mock_truncate_flags = 0xFFFF;
    mock_truncate_out = NULL;
    mock_truncate_status_ptr = NULL;
    mock_truncate_status = 0;
    mock_abs_clock_calls = 0;
    mock_abs_clock_arg = NULL;
    mock_rem_file_calls = 0;
}

void ML_$LOCK(int16_t lock_id)
{
    mock_lock_depth[lock_id & 0x3F]++;
}

void ML_$UNLOCK(int16_t lock_id)
{
    mock_lock_depth[lock_id & 0x3F]--;
}

void ast_$purify_aote(aote_t *aote, boolean flags, status_$t *status)
{
    (void)flags;
    mock_purify_calls++;
    mock_purify_aote_arg = aote;
    *status = mock_purify_status;
}

void AST_$TRUNCATE(uid_t *uid, uint32_t len, uint16_t flags,
                   boolean *out, status_$t *status)
{
    mock_truncate_calls++;
    mock_truncate_uid = *uid;
    mock_truncate_len = len;
    mock_truncate_flags = flags;
    mock_truncate_out = out;
    mock_truncate_status_ptr = status;
    *out = 0;                     /* 0xE05C70: the callee clears the byte */
    *status = mock_truncate_status;
}

void TIME_$ABS_CLOCK(clock_t *clk)
{
    mock_abs_clock_calls++;
    mock_abs_clock_arg = clk;
    clk->high = 0x11112222;
    clk->low = 0x3333;
}

status_$t ast_$validate_uid(uid_t *uid, uint32_t flags)
{
    (void)uid;
    (void)flags;
    return 0x00030001;
}

aote_t *ast_$lookup_aote_by_uid(uid_t *uid)
{
    (void)uid;
    return NULL;
}

aote_t *ast_$force_activate_segment(uid_t *uid, uint32_t segment,
                                    status_$t *status, int8_t force)
{
    (void)uid; (void)segment; (void)force;
    *status = 0x000F0003;
    return NULL;
}

void REM_FILE_$SET_ATTRIBUTE(void *net_info, uid_t *uid, uint16_t attr_type,
                             void *value, status_$t *status)
{
    (void)net_info; (void)uid; (void)attr_type; (void)value;
    mock_rem_file_calls++;
    *status = status_$ok;
}

/* The implementation under test (carries the nested dispatcher). */
#include "../set_attribute_internal.c"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static aote_t test_aote;
static aote_t *test_aote_slot;
static clock_t test_clock;
static ast_$subject_t test_subject;

static void setup_aote(void)
{
    memset(&test_aote, 0, sizeof(test_aote));
    memset(&test_subject, 0, sizeof(test_subject));
    test_aote_slot = &test_aote;
    test_clock.high = 0x0A0B0C0D;
    test_clock.low = 0x0E0F;
}

static void call_dispatch(uint16_t attr_type, void *value, status_$t *status)
{
    ast_$set_attr_dispatch(attr_type, value, false, &test_subject,
                           &test_aote_slot, &test_clock, status);
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/*
 * 0xE04D6E: a BLOCKS change leaves needs_purify clear, so the epilogue at
 * 0xE0514C must take the plain-unlock arm.  The old code gated the epilogue on
 * an invented needs_truncate that BLOCKS assigned, which ran purify here.
 */
TEST(blocks_change_does_not_purify)
{
    status_$t status = 0xFFFFFFFF;
    uint32_t new_blocks = 0x1234;

    setup_aote();
    test_aote.blocks = 0x1000;

    call_dispatch(ATTR_TYPE_BLOCKS, &new_blocks, &status);

    ASSERT_EQ(0x1234, test_aote.blocks);
    ASSERT_EQ(0, mock_purify_calls);
    ASSERT_EQ(0, mock_truncate_calls);
    ASSERT_EQ(status_$ok, status);
}

/*
 * 0xE04D84 branches to 0xE05110, skipping the attribute-modified clock store
 * at 0xE05100 that every other case performs.
 */
TEST(blocks_change_skips_attr_clock)
{
    status_$t status = 0xFFFFFFFF;
    uint32_t new_blocks = 7;

    setup_aote();
    test_aote.blocks = 1;
    test_aote.dta_high = 0x99999999;
    test_aote.dta_low = 0x9999;

    call_dispatch(ATTR_TYPE_BLOCKS, &new_blocks, &status);

    ASSERT_EQ(0x99999999, test_aote.dta_high);
    ASSERT_EQ(0x9999, test_aote.dta_low);
    /* 0xE05114 still sets the dirty bit. */
    ASSERT_TRUE((test_aote.flags & AOTE_FLAG_DIRTY) != 0);
}

/* 0xE04D78: an unchanged block count unlocks without setting the dirty bit. */
TEST(blocks_unchanged_is_a_no_op)
{
    status_$t status = 0xFFFFFFFF;
    uint32_t same = 0x500;

    setup_aote();
    test_aote.blocks = 0x500;

    call_dispatch(ATTR_TYPE_BLOCKS, &same, &status);

    ASSERT_EQ(0, test_aote.flags);
    ASSERT_EQ(status_$ok, status);
}

/*
 * 0xE04C98: an ACL UID change sets needs_purify, so the epilogue runs purify
 * and, because both saved UIDs have a zero top byte, neither the refcount bump
 * nor the truncate fires.
 */
TEST(acl_change_purifies)
{
    status_$t status = 0xFFFFFFFF;
    uid_t new_acl = { 0x00001111, 0x22223333 };

    setup_aote();
    test_aote.acl_uid.high = 0x00004444;
    test_aote.acl_uid.low = 0x55556666;

    call_dispatch(ATTR_TYPE_ACL_UID, &new_acl, &status);

    ASSERT_EQ(0x00001111, test_aote.acl_uid.high);
    ASSERT_EQ(0x22223333, test_aote.acl_uid.low);
    ASSERT_EQ(1, mock_purify_calls);
    ASSERT_TRUE(mock_purify_aote_arg == &test_aote);
    /* 0xE04CA6 resets the rights bytes. */
    ASSERT_EQ(0x10, test_aote.rights1);
    ASSERT_EQ(0x10, test_aote.rights2);
    ASSERT_EQ(0x10, test_aote.rights3);
    ASSERT_EQ(0, test_aote.rights4);
    ASSERT_EQ(0, test_aote.rights5);
    ASSERT_EQ(0, mock_truncate_calls);
}

/* 0xE04C7C: an unchanged ACL UID short-circuits before needs_purify is set. */
TEST(acl_unchanged_does_not_purify)
{
    status_$t status = 0xFFFFFFFF;
    uid_t same = { 0xAA004444, 0x55556666 };

    setup_aote();
    test_aote.acl_uid = same;

    call_dispatch(ATTR_TYPE_ACL_UID, &same, &status);

    ASSERT_EQ(0, mock_purify_calls);
    ASSERT_EQ(0, test_aote.rights1);
}

/*
 * 0xE051C6: only the top byte of the old ACL UID is tested.  A UID whose top
 * byte is zero but whose remaining bytes are set must NOT be truncated - the
 * pre-audit 32-bit test truncated it.
 */
TEST(old_acl_nil_test_uses_top_byte_only)
{
    status_$t status = 0xFFFFFFFF;
    uid_t new_acl = { 0x00000000, 0x00000000 };

    setup_aote();
    test_aote.acl_uid.high = 0x00FFFFFF;   /* top byte zero, rest non-zero */
    test_aote.acl_uid.low = 0xFFFFFFFF;

    call_dispatch(ATTR_TYPE_ACL_UID, &new_acl, &status);

    ASSERT_EQ(1, mock_purify_calls);
    ASSERT_EQ(0, mock_truncate_calls);
}

/* The mirror case: a non-zero top byte does truncate the old ACL. */
TEST(old_acl_with_top_byte_is_truncated)
{
    status_$t status = 0xFFFFFFFF;
    uid_t new_acl = { 0x00000000, 0x00000000 };

    setup_aote();
    test_aote.acl_uid.high = 0x77000000;   /* only the top byte is set */
    test_aote.acl_uid.low = 0x00000000;

    call_dispatch(ATTR_TYPE_ACL_UID, &new_acl, &status);

    ASSERT_EQ(1, mock_truncate_calls);
    ASSERT_EQ(0x77000000, mock_truncate_uid.high);
    /* 0xE051DC / 0xE051D8: arguments 2 and 3 are the long 0 and the word 3. */
    ASSERT_EQ(0, mock_truncate_len);
    ASSERT_EQ(3, mock_truncate_flags);
    /* 0xE051D4: argument 4 is a real byte out-parameter, not NULL. */
    ASSERT_TRUE(mock_truncate_out != NULL);
    /* 0xE051D0: argument 5 is the caller's status pointer, not a local. */
    ASSERT_TRUE(mock_truncate_status_ptr == &status);
}

/*
 * 0xE051EE: AST_$TRUNCATE returning "object not found" is rewritten to
 * status_$ok through the caller's own status pointer.
 */
TEST(truncate_object_not_found_is_cleared)
{
    status_$t status = 0xFFFFFFFF;
    uid_t new_acl = { 0x00000000, 0x00000000 };

    setup_aote();
    test_aote.acl_uid.high = 0x77000000;
    test_aote.acl_uid.low = 0;
    mock_truncate_status = status_$file_object_not_found;

    call_dispatch(ATTR_TYPE_ACL_UID, &new_acl, &status);

    ASSERT_EQ(1, mock_truncate_calls);
    ASSERT_EQ(status_$ok, status);
}

/* Any other truncate status is left alone. */
TEST(truncate_other_status_is_kept)
{
    status_$t status = 0xFFFFFFFF;
    uid_t new_acl = { 0x00000000, 0x00000000 };

    setup_aote();
    test_aote.acl_uid.high = 0x77000000;
    test_aote.acl_uid.low = 0;
    mock_truncate_status = 0x000F0009;

    call_dispatch(ATTR_TYPE_ACL_UID, &new_acl, &status);

    ASSERT_EQ(0x000F0009, status);
}

/*
 * 0xE0518E: the new ACL UID's top byte gates the recursive ADD_REFCOUNT.
 * With a zero top byte no recursion happens, so no second purify occurs.
 */
TEST(new_acl_nil_test_uses_top_byte_only)
{
    status_$t status = 0xFFFFFFFF;
    uid_t new_acl = { 0x00FFFFFF, 0xFFFFFFFF };

    setup_aote();
    test_aote.acl_uid.high = 0;
    test_aote.acl_uid.low = 0;

    call_dispatch(ATTR_TYPE_ACL_UID, &new_acl, &status);

    ASSERT_EQ(1, mock_purify_calls);   /* only the epilogue's own purify */
    ASSERT_EQ(0, mock_truncate_calls);
}

/*
 * 0xE0515C: a remote AOTE skips the whole epilogue even when needs_purify is
 * set, and takes the plain ML_$UNLOCK(0x12) arm.
 */
TEST(remote_aote_skips_purify_epilogue)
{
    status_$t status = 0xFFFFFFFF;
    uid_t new_acl = { 0x77000000, 0 };

    setup_aote();
    test_aote.remote_flag = -1;
    test_aote.acl_uid.high = 0x77000000;

    call_dispatch(ATTR_TYPE_ACL_UID, &new_acl, &status);

    ASSERT_EQ(0, mock_purify_calls);
    ASSERT_EQ(0, mock_truncate_calls);
    /* Both locks are released exactly once each. */
    ASSERT_EQ(-1, mock_lock_depth[AST_LOCK_ID]);
    ASSERT_EQ(0, mock_lock_depth[PMAP_LOCK_ID]);
}

/*
 * 0xE04D1E: SUB_REFCOUNT hitting zero clears attr_flags_hi bit 4, reports
 * underflow, and still runs the common tail (0xE04D6A branches to 0xE05100).
 */
TEST(sub_refcount_to_zero_still_stamps_clock)
{
    status_$t status = 0xFFFFFFFF;

    setup_aote();
    test_aote.refcount = 1;
    test_aote.sub_type = 3;          /* neither 1 nor 2 */
    test_aote.attr_flags_hi = 0xFF;

    call_dispatch(ATTR_TYPE_SUB_REFCOUNT, NULL, &status);

    ASSERT_EQ(0, test_aote.refcount);
    ASSERT_EQ(0xEF, test_aote.attr_flags_hi);
    ASSERT_EQ(status_$ast_refcount_says_unused, status);
    ASSERT_EQ(0x0A0B0C0D, test_aote.dta_high);
    ASSERT_EQ(0x0E0F, test_aote.dta_low);
}

/* 0xE04D2E: sub_type 1 and 2 objects refuse to drop their last reference. */
TEST(sub_refcount_last_ref_on_sub_type_1_fails)
{
    status_$t status = 0xFFFFFFFF;

    setup_aote();
    test_aote.refcount = 1;
    test_aote.sub_type = 1;

    call_dispatch(ATTR_TYPE_SUB_REFCOUNT, NULL, &status);

    ASSERT_EQ(1, test_aote.refcount);              /* unchanged */
    ASSERT_EQ(status_$ast_refcount_says_unused, status);
    ASSERT_EQ(0, test_aote.flags);                 /* no dirty bit: 0xE04D4E */
}

/* 0xE04B30: obj_type 0 restricts the legal attribute set to 0x3FFF. */
TEST(basic_attr_set_rejects_high_types)
{
    status_$t status = 0xFFFFFFFF;
    uid_t v = { 1, 2 };

    setup_aote();
    test_aote.obj_type = 0;

    call_dispatch(ATTR_TYPE_UID_84, &v, &status);

    ASSERT_EQ(status_$ast_incompatible_request, status);
    ASSERT_EQ(0, test_aote.uid_84.high);
}

/* A non-zero obj_type lifts that restriction (0xE04B32 skips the test). */
TEST(non_basic_obj_type_allows_high_types)
{
    status_$t status = 0xFFFFFFFF;
    uid_t v = { 0x12345678, 0x9ABCDEF0 };

    setup_aote();
    test_aote.obj_type = 4;

    call_dispatch(ATTR_TYPE_UID_84, &v, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x12345678, test_aote.uid_84.high);
    ASSERT_EQ(0x9ABCDEF0, test_aote.uid_84.low);
}

/* 0xE04B94: types at or above 0x1C fall into the invalid-type arm. */
TEST(out_of_range_attr_type_is_invalid)
{
    status_$t status = 0xFFFFFFFF;

    setup_aote();
    test_aote.obj_type = 4;

    call_dispatch(0x1C, NULL, &status);

    ASSERT_EQ(status_$ast_incompatible_request, status);
}

/* 0xE04B46: a special object accepts MOD_TIME and BLOCKS and nothing else. */
TEST(special_object_rejects_other_attrs)
{
    status_$t status = 0xFFFFFFFF;
    uint32_t v = 5;

    setup_aote();
    test_aote.obj_type = 4;
    test_aote.attr_flags_lo = AOTE_ATTR_SPECIAL;

    call_dispatch(ATTR_TYPE_DTM, &v, &status);

    ASSERT_EQ(status_$file_volume_has_been_mounted_read_only, status);
    ASSERT_EQ(0, test_aote.dtm_high);
}

TEST(special_object_accepts_blocks)
{
    status_$t status = 0xFFFFFFFF;
    uint32_t v = 0x321;

    setup_aote();
    test_aote.obj_type = 4;
    test_aote.attr_flags_lo = AOTE_ATTR_SPECIAL;

    call_dispatch(ATTR_TYPE_BLOCKS, &v, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x321, test_aote.blocks);
    /* 0xE04B78 goes straight to the unlock: no dirty bit, no clock. */
    ASSERT_EQ(0, test_aote.flags);
}

/*
 * 0xE04E68 / 0xE04E82 / 0xE04E9E: the three owner cases each take their UID
 * from a different offset in the record and their extension word from
 * 0x20/0x24/0x28.  These were among the fourteen unimplemented cases.
 */
TEST(owner_cases_use_the_right_record_slots)
{
    status_$t status;
    ast_$attr_rec_t rec;

    memset(&rec, 0, sizeof(rec));
    rec.owner1.high = 0x11; rec.owner1.low = 0x12; rec.owner1_ext = 0xA1;
    rec.owner2.high = 0x21; rec.owner2.low = 0x22; rec.owner2_ext = 0xA2;
    rec.owner3.high = 0x31; rec.owner3.low = 0x32; rec.owner3_ext = 0xA3;

    setup_aote();
    test_aote.obj_type = 4;
    call_dispatch(ATTR_TYPE_OWNER1, &rec, &status);
    ASSERT_EQ(0x11, test_aote.owner1.high);
    ASSERT_EQ(0xA1, test_aote.owner1_ext);
    ASSERT_EQ(0, test_aote.owner2.high);

    setup_aote();
    test_aote.obj_type = 4;
    call_dispatch(ATTR_TYPE_OWNER2, &rec, &status);
    ASSERT_EQ(0x21, test_aote.owner2.high);
    ASSERT_EQ(0xA2, test_aote.owner2_ext);
    ASSERT_EQ(0, test_aote.owner1.high);

    setup_aote();
    test_aote.obj_type = 4;
    call_dispatch(ATTR_TYPE_OWNER3, &rec, &status);
    ASSERT_EQ(0x31, test_aote.owner3.high);
    ASSERT_EQ(0xA3, test_aote.owner3_ext);
}

/* 0xE04EC8: SET_RIGHTS copies four bytes then mirrors rights4 into rights5. */
TEST(set_rights_mirrors_rights4_into_rights5)
{
    status_$t status;
    ast_$attr_rec_t rec;

    memset(&rec, 0, sizeof(rec));
    rec.rights1 = 0x41; rec.rights2 = 0x42; rec.rights3 = 0x43; rec.rights4 = 0x44;
    rec.rights5 = 0x99;              /* must be ignored */

    setup_aote();
    test_aote.obj_type = 4;

    call_dispatch(ATTR_TYPE_SET_RIGHTS, &rec, &status);

    ASSERT_EQ(0x41, test_aote.rights1);
    ASSERT_EQ(0x44, test_aote.rights4);
    ASSERT_EQ(0x44, test_aote.rights5);
}

/*
 * 0xE04EDC: SET_ACL_IMAGE copies the 44-byte image but preserves
 * access_flags bits 6, 5 and 4 across the copy, and marks the ACL dirty.
 */
TEST(set_acl_image_preserves_mode_bits)
{
    status_$t status;
    ast_$attr_rec_t rec;

    memset(&rec, 0, sizeof(rec));
    rec.owner1.high = 0xAB;
    rec.rights1 = 0x07;
    rec.access_flags = (int8_t)0x80;        /* all three mode bits clear */
    rec.acl_uid.high = 0x5A000000;
    rec.acl_uid.low = 0x1234;

    setup_aote();
    test_aote.obj_type = 4;
    test_aote.access_flags = (int8_t)0x70;  /* bits 6, 5 and 4 set */

    call_dispatch(ATTR_TYPE_SET_ACL_IMAGE, &rec, &status);

    ASSERT_EQ(0xAB, test_aote.owner1.high);
    ASSERT_EQ(0x07, test_aote.rights1);
    ASSERT_EQ(0x5A000000, test_aote.acl_uid.high);
    ASSERT_EQ(0xF0, (uint8_t)test_aote.access_flags);   /* 0x80 | 0x70 */
    /* It is an ACL change, so the epilogue runs. */
    ASSERT_EQ(1, mock_purify_calls);
}

/*
 * 0xE04F66: MERGE_ACL leaves an owner alone when the record's entry is nil,
 * refuses a rights byte whose bit 5 is set, and clears bit 5 of the AOTE's
 * rights byte when the owner does not match the subject's.
 */
TEST(merge_acl_skips_nil_owner_and_masks_rights)
{
    status_$t status;
    ast_$attr_rec_t rec;

    memset(&rec, 0, sizeof(rec));
    rec.owner1.high = 0;  rec.owner1.low = 0;      /* nil: leave owner1 alone */
    rec.rights1 = 0x20;                            /* bit 5 set: do not store */
    rec.owner3.high = 0xCC;
    rec.rights3 = 0x03;
    rec.rights5 = 0xFF;                            /* 0xE050C2 masks with 0xCF */
    rec.access_flags = 0;
    rec.acl_uid.high = 0x5B000000;

    setup_aote();
    test_aote.obj_type = 4;
    test_aote.owner1.high = 0xDEAD;
    test_aote.rights1 = 0x2F;
    test_subject.owner1.high = 0;                  /* does not match 0xDEAD */

    call_dispatch(ATTR_TYPE_MERGE_ACL, &rec, &status);

    ASSERT_EQ(0xDEAD, test_aote.owner1.high);      /* untouched */
    ASSERT_EQ(0x0F, test_aote.rights1);            /* kept, bit 5 cleared */
    ASSERT_EQ(0xCC, test_aote.owner3.high);
    ASSERT_EQ(0x03 & ~0x20, test_aote.rights3);
    ASSERT_EQ(0xCF, test_aote.rights5);
    ASSERT_EQ(0x5B000000, test_aote.acl_uid.high);
    ASSERT_EQ(1, mock_purify_calls);
}

/* 0xE04FBE: a matching subject owner leaves bit 5 of the rights byte alone. */
TEST(merge_acl_keeps_bit5_when_owner_matches_subject)
{
    status_$t status;
    ast_$attr_rec_t rec;

    memset(&rec, 0, sizeof(rec));
    rec.owner1.high = 0x1234; rec.owner1.low = 0x5678;
    rec.rights1 = 0x2F;                            /* bit 5 set: not stored */

    setup_aote();
    test_aote.obj_type = 4;
    test_aote.rights1 = 0x3F;
    test_subject.owner1.high = 0x1234;
    test_subject.owner1.low = 0x5678;

    call_dispatch(ATTR_TYPE_MERGE_ACL, &rec, &status);

    ASSERT_EQ(0x1234, test_aote.owner1.high);
    ASSERT_EQ(0x3F, test_aote.rights1);            /* bit 5 survives */
}

/*
 * 0xE05028: when the AOTE's owner2 is not the subject's owner2, the eight
 * supplementary groups (entries 1..8, not 0) are searched; a match keeps bit 5.
 */
TEST(merge_acl_searches_supplementary_groups)
{
    status_$t status;
    ast_$attr_rec_t rec;

    memset(&rec, 0, sizeof(rec));
    rec.owner2.high = 0xBEEF; rec.owner2.low = 0xF00D;
    rec.rights2 = 0x20;                            /* bit 5 set: not stored */

    setup_aote();
    test_aote.obj_type = 4;
    test_aote.rights2 = 0x2A;
    test_subject.owner2.high = 0x1111;             /* no direct match */
    test_subject.groups[0].high = 0xBEEF;          /* entry 0 is NOT searched */
    test_subject.groups[0].low = 0xF00D;
    test_subject.groups[1].high = 0x2222;
    test_subject.groups[1].low = 0x3333;
    test_subject.groups[2].high = 0xBEEF;          /* entry 2 matches */
    test_subject.groups[2].low = 0xF00D;

    call_dispatch(ATTR_TYPE_MERGE_ACL, &rec, &status);

    ASSERT_EQ(0x2A, test_aote.rights2);            /* bit 5 survives */
}

/* A nil group entry ends the search early and clears bit 5. */
TEST(merge_acl_nil_group_ends_search)
{
    status_$t status;
    ast_$attr_rec_t rec;

    memset(&rec, 0, sizeof(rec));
    rec.owner2.high = 0xBEEF; rec.owner2.low = 0xF00D;
    rec.rights2 = 0x20;

    setup_aote();
    test_aote.obj_type = 4;
    test_aote.rights2 = 0x2A;
    test_subject.owner2.high = 0x1111;
    test_subject.groups[1].high = 0;               /* nil: stops the scan */
    test_subject.groups[1].low = 0;
    test_subject.groups[2].high = 0xBEEF;          /* never reached */
    test_subject.groups[2].low = 0xF00D;

    call_dispatch(ATTR_TYPE_MERGE_ACL, &rec, &status);

    ASSERT_EQ(0x0A, test_aote.rights2);            /* bit 5 cleared */
}

/*
 * 0xE04DA6 tests obj_type (D0, still live from the prologue) and rounds the
 * length up when it is zero - but 0xE04B3A only admits attribute types in
 * the 0x3FFF set on an obj_type 0 object, and 0x17/0x18 are not in it.  The
 * round-up arm at 0xE04DBA is therefore unreachable in the shipped kernel;
 * what an obj_type 0 object actually gets is the invalid-type status.
 *
 * The C reproduces both, so this test pins the reachable behaviour and
 * documents the dead arm.
 */
TEST(len_rounded_on_basic_object_is_rejected)
{
    status_$t status;
    struct { uint32_t high; uint16_t low; } v;

    v.high = 0x100; v.low = 0x40;

    setup_aote();
    test_aote.obj_type = 0;
    test_aote.dtm_high = 0;
    test_aote.dtm_low = 0;

    call_dispatch(ATTR_TYPE_DTM_ROUNDED, &v, &status);

    ASSERT_EQ(status_$ast_incompatible_request, status);
    ASSERT_EQ(0, test_aote.dtm_high);           /* never reached the store */
    ASSERT_EQ(0, test_aote.dtm_low);
}

TEST(len_rounded_is_verbatim_on_other_objects)
{
    status_$t status;
    struct { uint32_t high; uint16_t low; } v;

    v.high = 0x100; v.low = 0x40;

    setup_aote();
    test_aote.obj_type = 4;

    call_dispatch(ATTR_TYPE_DTM_ROUNDED, &v, &status);

    ASSERT_EQ(0x100, test_aote.dtm_high);
    ASSERT_EQ(0x40, test_aote.dtm_low);
}

/*
 * 0xE04DEC stores the pair into DTM and then falls into 0xE04DF8, which
 * clears the touched bit.  As above the obj_type must be non-zero for the
 * case to be reachable at all, so the pair is stored verbatim.
 */
TEST(dtm_rounded_clears_touched_bit)
{
    status_$t status;
    struct { uint32_t high; uint16_t low; } v;

    v.high = 0x200; v.low = 0x40;

    setup_aote();
    test_aote.obj_type = 4;
    test_aote.flags = AOTE_FLAG_TOUCHED;

    call_dispatch(ATTR_TYPE_DTU_ROUNDED, &v, &status);

    ASSERT_EQ(0x200, test_aote.dtu_high);
    ASSERT_EQ(0x40, test_aote.dtu_low);
    ASSERT_TRUE((test_aote.flags & AOTE_FLAG_TOUCHED) == 0);
    ASSERT_TRUE((test_aote.flags & AOTE_FLAG_DIRTY) != 0);
}

/*
 * 0xE04E06: the FROM_CLOCK variants take the parent's clock local, and 0x1A
 * additionally copies the result into the length.
 */
TEST(len_from_clock_copies_dtm_into_len)
{
    status_$t status;

    setup_aote();
    test_aote.obj_type = 4;         /* no rounding */

    call_dispatch(ATTR_TYPE_DTM_FROM_CLOCK, NULL, &status);

    ASSERT_EQ(0x0A0B0C0D, test_aote.dtu_high);
    ASSERT_EQ(0x0E0F, test_aote.dtu_low);
    ASSERT_EQ(0x0A0B0C0D, test_aote.dtm_high);
    ASSERT_EQ(0x0E0F, test_aote.dtm_low);
}

TEST(dtm_from_clock_leaves_len_alone)
{
    status_$t status;

    setup_aote();
    test_aote.obj_type = 4;
    test_aote.dtm_high = 0x5555;

    call_dispatch(ATTR_TYPE_DTU_FROM_CLOCK, NULL, &status);

    ASSERT_EQ(0x0A0B0C0D, test_aote.dtu_high);
    ASSERT_EQ(0x5555, test_aote.dtm_high);
}

/* 0xE0511A: the timestamp mask selects which types refresh the DTU. */
TEST(timestamp_mask_gates_abs_clock)
{
    status_$t status;
    uint32_t v = 0x10;

    setup_aote();
    test_aote.obj_type = 4;
    AST_$ATTR_TIMESTAMP_MASK = 0;

    call_dispatch(ATTR_TYPE_DTM, &v, &status);
    ASSERT_EQ(0, mock_abs_clock_calls);

    setup_aote();
    test_aote.obj_type = 4;
    AST_$ATTR_TIMESTAMP_MASK = 1u << ATTR_TYPE_DTM;

    call_dispatch(ATTR_TYPE_DTM, &v, &status);
    ASSERT_EQ(1, mock_abs_clock_calls);
    ASSERT_TRUE(mock_abs_clock_arg == (void *)&test_aote.dtv_high);
    ASSERT_EQ(0x11112222, test_aote.dtv_high);
    ASSERT_EQ(0x3333, test_aote.dtv_low);

    AST_$ATTR_TIMESTAMP_MASK = 0;
}

/* 0xE0512A: a remote AOTE never refreshes the absolute clock. */
TEST(remote_aote_skips_abs_clock)
{
    status_$t status;
    uint32_t v = 0x10;

    setup_aote();
    test_aote.obj_type = 4;
    test_aote.remote_flag = -1;
    AST_$ATTR_TIMESTAMP_MASK = 1u << ATTR_TYPE_DTM;

    call_dispatch(ATTR_TYPE_DTM, &v, &status);

    ASSERT_EQ(0, mock_abs_clock_calls);
    AST_$ATTR_TIMESTAMP_MASK = 0;
}

/*
 * ast_$set_attribute_internal itself: 0xE0522E sends a nil UID to the
 * validator without ever taking the AST lock.
 */
TEST(parent_nil_uid_goes_to_validate)
{
    status_$t status = 0;
    uid_t nil = { 0, 0 };
    clock_t clk = { 0, 0 };

    ast_$set_attribute_internal(&nil, ATTR_TYPE_DTM, NULL, false, NULL,
                                &clk, &status);

    ASSERT_EQ(0x00030001, status);
    ASSERT_EQ(0, mock_lock_depth[AST_LOCK_ID]);
}

/* 0xE05294: a failed activation unlocks and returns the activation status. */
TEST(parent_activation_failure_unlocks)
{
    status_$t status = 0;
    uid_t uid = { 0x1234, 0x5678 };
    clock_t clk = { 0, 0 };

    ast_$set_attribute_internal(&uid, ATTR_TYPE_DTM, NULL, false, NULL,
                                &clk, &status);

    ASSERT_EQ(0x000F0003, status);
    ASSERT_EQ(0, mock_lock_depth[AST_LOCK_ID]);
    ASSERT_EQ(0, mock_rem_file_calls);
}

int main(void)
{
    printf("Running AST_$SET_ATTR_DISPATCH tests\n");

    RUN_TEST(blocks_change_does_not_purify);
    RUN_TEST(blocks_change_skips_attr_clock);
    RUN_TEST(blocks_unchanged_is_a_no_op);
    RUN_TEST(acl_change_purifies);
    RUN_TEST(acl_unchanged_does_not_purify);
    RUN_TEST(old_acl_nil_test_uses_top_byte_only);
    RUN_TEST(old_acl_with_top_byte_is_truncated);
    RUN_TEST(truncate_object_not_found_is_cleared);
    RUN_TEST(truncate_other_status_is_kept);
    RUN_TEST(new_acl_nil_test_uses_top_byte_only);
    RUN_TEST(remote_aote_skips_purify_epilogue);
    RUN_TEST(sub_refcount_to_zero_still_stamps_clock);
    RUN_TEST(sub_refcount_last_ref_on_sub_type_1_fails);
    RUN_TEST(basic_attr_set_rejects_high_types);
    RUN_TEST(non_basic_obj_type_allows_high_types);
    RUN_TEST(out_of_range_attr_type_is_invalid);
    RUN_TEST(special_object_rejects_other_attrs);
    RUN_TEST(special_object_accepts_blocks);
    RUN_TEST(owner_cases_use_the_right_record_slots);
    RUN_TEST(set_rights_mirrors_rights4_into_rights5);
    RUN_TEST(set_acl_image_preserves_mode_bits);
    RUN_TEST(merge_acl_skips_nil_owner_and_masks_rights);
    RUN_TEST(merge_acl_keeps_bit5_when_owner_matches_subject);
    RUN_TEST(merge_acl_searches_supplementary_groups);
    RUN_TEST(merge_acl_nil_group_ends_search);
    RUN_TEST(len_rounded_on_basic_object_is_rejected);
    RUN_TEST(len_rounded_is_verbatim_on_other_objects);
    RUN_TEST(dtm_rounded_clears_touched_bit);
    RUN_TEST(len_from_clock_copies_dtm_into_len);
    RUN_TEST(dtm_from_clock_leaves_len_alone);
    RUN_TEST(timestamp_mask_gates_abs_clock);
    RUN_TEST(remote_aote_skips_abs_clock);
    RUN_TEST(parent_nil_uid_goes_to_validate);
    RUN_TEST(parent_activation_failure_unlocks);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
