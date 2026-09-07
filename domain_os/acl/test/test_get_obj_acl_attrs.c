/*
 * acl/test/test_get_obj_acl_attrs.c - unit tests for acl_$get_obj_acl_attrs
 * (0x00E45F78)
 *
 * The real acl/get_obj_acl_attrs.c is #included at the bottom and driven
 * through mocks of HINT_$LOOKUP_CACHE, AST_$GET_ACL_ATTRIBUTES and
 * ACL_$CONVERT_FUNKY_ACL.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  %-50s ", #name);                  \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "acl/acl_internal.h"
#include "hint/hint.h"

/* ------------------------------------------------------------------ */
/* Globals                                                              */
/* ------------------------------------------------------------------ */

uid_t UID_$NIL      = { 0, 0 };
/* 0x00E17384: raw bytes 00 00 01 00 00 00 00 00. */
uid_t ACL_$NIL      = { 0x00000100u, 0 };
/* 0x00E174DC: raw bytes 00 02 a0 7f 00 00 00 00. */
uid_t ACL_$DNDCAL   = { 0x0002A07Fu, 0 };

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static uint32_t mock_hint_key_seen;
static uint8_t  mock_hint_result;
static int      mock_hint_calls;

void HINT_$LOOKUP_CACHE(uint32_t *uid_low_masked_ptr, uint8_t *result)
{
    mock_hint_key_seen = *uid_low_masked_ptr;
    mock_hint_calls++;
    *result = mock_hint_result;
}

#define MAX_AST_CALLS 4
static int             mock_ast_calls;
static uint16_t        mock_ast_mode[MAX_AST_CALLS];
static status_$t       mock_ast_status[MAX_AST_CALLS];
static ast_$acl_attr_t mock_ast_out[MAX_AST_CALLS];
static file_$obj_loc_t mock_ast_loc_seen;

void AST_$GET_ACL_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags,
                             ast_$acl_attr_t *acl, status_$t *status)
{
    int i = mock_ast_calls < MAX_AST_CALLS ? mock_ast_calls : MAX_AST_CALLS - 1;

    mock_ast_loc_seen = *loc_rec;
    mock_ast_mode[i] = flags;
    *acl = mock_ast_out[i];
    *status = mock_ast_status[i];
    mock_ast_calls++;
}

static int   mock_funky_calls;
static uid_t mock_funky_uid_seen;
static uid_t mock_funky_norm_out;
static int   mock_funky_set_norm;

void ACL_$CONVERT_FUNKY_ACL(void *acl_uid, void *acl_data_out,
                            void *prot_info_out, void *target_uid_out,
                            status_$t *status_ret)
{
    (void)acl_data_out; (void)target_uid_out;
    mock_funky_calls++;
    mock_funky_uid_seen = *(uid_t *)acl_uid;
    if (mock_funky_set_norm) {
        *(uid_t *)prot_info_out = mock_funky_norm_out;
    }
    *status_ret = status_$ok;
}

/* ------------------------------------------------------------------ */
/* Fixtures                                                             */
/* ------------------------------------------------------------------ */

static uid_t           in_uid;
static file_$obj_loc_t loc;
static ast_$acl_attr_t attrs;
static status_$t       status;

static void reset_world(void)
{
    memset(&loc, 0, sizeof(loc));
    memset(&attrs, 0, sizeof(attrs));
    memset(mock_ast_out, 0, sizeof(mock_ast_out));
    memset(mock_ast_status, 0, sizeof(mock_ast_status));
    memset(mock_ast_mode, 0, sizeof(mock_ast_mode));

    status = 0x7F7F7F7F;
    loc.flags = (int8_t)0xFF;               /* every flag bit set */

    mock_hint_calls = 0;
    mock_hint_result = 0;
    mock_hint_key_seen = 0;
    mock_ast_calls = 0;
    mock_funky_calls = 0;
    mock_funky_set_norm = 0;

    in_uid.high = 0x0BAD0000u;
    in_uid.low  = 0x000ABCDEu;
}

static void run(void)
{
    acl_$get_obj_acl_attrs(&in_uid, &loc, &attrs, &status);
}

/* ------------------------------------------------------------------ */

TEST(loc_gets_the_normalised_uid_with_bit_24_cleared)
{
    reset_world();
    in_uid.low = 0x0102ABCDu;               /* bit 24 set */
    run();
    /* `bclr.b #0x0,(-0x14,A6)` at 0x00E45FA8 clears bit 24 of the LOW half. */
    ASSERT_EQ(0x0BAD0000u, loc.uid.high);
    ASSERT_EQ(0x0002ABCDu, loc.uid.low);
    /* `bclr.b #0x6,(0x1d,A3)` at 0x00E45FEA. */
    ASSERT_EQ(0, loc.flags & FILE_OBJ_LOC_SCRATCH);
    /* Bit 7 survives on the AST path. */
    ASSERT_EQ(FILE_OBJ_LOC_REMOTE, loc.flags & FILE_OBJ_LOC_REMOTE);
}

TEST(hint_key_is_the_low_20_bits_of_the_normalised_uid)
{
    reset_world();
    in_uid.low = 0x01FFFFFFu;
    run();
    ASSERT_EQ(1, mock_hint_calls);
    /* 0x00E45FF4: `move.l #0xfffff,D0` / `and.l (-0x14,A6),D0`. */
    ASSERT_EQ(0x000FFFFFu, mock_hint_key_seen);
}

TEST(hint_flag_selects_ast_mode_1)
{
    reset_world();
    mock_hint_result = 0x7F;                /* >= 0 -> `bpl` */
    run();
    ASSERT_EQ(1, mock_ast_calls);
    ASSERT_EQ(0x0001, mock_ast_mode[0]);
}

TEST(negative_hint_flag_selects_ast_mode_21)
{
    reset_world();
    mock_hint_result = 0xFF;                /* < 0 */
    run();
    ASSERT_EQ(1, mock_ast_calls);
    ASSERT_EQ(0x0021, mock_ast_mode[0]);
}

TEST(funky_bits_invoke_the_converter)
{
    reset_world();
    /* bits 4..11 of the low half's high word, >>4 & 0xE0 == 0x80 */
    in_uid.low = 0x08000000u;
    mock_funky_set_norm = 1;
    mock_funky_norm_out.high = 0x77770000u;
    mock_funky_norm_out.low  = 0x01000042u; /* the converter re-sets bit 24 */
    run();
    ASSERT_EQ(1, mock_funky_calls);
    ASSERT_EQ(0x0BAD0000u, mock_funky_uid_seen.high);
    ASSERT_EQ(0x08000000u, mock_funky_uid_seen.low);
    /* 0x00E45FD8 clears bit 24 again after the call. */
    ASSERT_EQ(0x77770000u, loc.uid.high);
    ASSERT_EQ(0x00000042u, loc.uid.low);
}

TEST(non_funky_bits_do_not_invoke_the_converter)
{
    reset_world();
    in_uid.low = 0x00100000u;               /* >>4 & 0xE0 == 0x10 & 0xE0 == 0 */
    run();
    ASSERT_EQ(0, mock_funky_calls);
}

/* ---- 0x00E46022: the well-known short circuits ---- */

TEST(nil_uid_is_well_known_and_reports_object_not_found)
{
    reset_world();
    in_uid = UID_$NIL;
    attrs.obj_flags[ACL_ATTR_FLAGS] = 0xFF;
    loc.flags = (int8_t)0xFF;
    run();
    ASSERT_EQ(0, mock_ast_calls);
    ASSERT_EQ(1, attrs.obj_flags[ACL_ATTR_PRESENT]);
    ASSERT_EQ(3, attrs.obj_flags[ACL_ATTR_OBJ_TYPE]);
    ASSERT_EQ(0, attrs.obj_flags[ACL_ATTR_FLAGS] & ACL_ATTR_FLAG_LOCAL);
    ASSERT_EQ(0, loc.flags & FILE_OBJ_LOC_REMOTE);      /* 0x00E4607E */
    ASSERT_EQ(0x000F0001u, status);                     /* 0x00E460A4 */
}

TEST(acl_nil_is_well_known_and_leaves_status_ok)
{
    reset_world();
    in_uid = ACL_$NIL;
    run();
    ASSERT_EQ(0, mock_ast_calls);
    ASSERT_EQ(1, attrs.obj_flags[ACL_ATTR_PRESENT]);
    ASSERT_EQ(ACL_$NIL.high, attrs.default_acl.high);
    ASSERT_EQ(status_$ok, status);
}

TEST(volume_uid_with_top_word_1_is_well_known)
{
    reset_world();
    in_uid.high = 0x00010000u;
    in_uid.low  = 0x00000005u;
    run();
    ASSERT_EQ(0, mock_ast_calls);
    ASSERT_EQ(3, attrs.obj_flags[ACL_ATTR_OBJ_TYPE]);
    ASSERT_EQ(status_$ok, status);
}

TEST(volume_uid_with_top_word_2_is_well_known)
{
    reset_world();
    in_uid.high = 0x00020000u;
    run();
    ASSERT_EQ(0, mock_ast_calls);
    ASSERT_EQ(1, attrs.obj_flags[ACL_ATTR_PRESENT]);
}

TEST(a_non_zero_top_byte_defeats_the_well_known_test)
{
    reset_world();
    /* `move.b (-0x18,A6),D0b` / `tst.w` at 0x00E46024 tests the TOP BYTE. */
    in_uid.high = 0x01010000u;
    run();
    ASSERT_EQ(1, mock_ast_calls);
}

/* ---- 0x00E460AE: the AST path ---- */

TEST(ast_failure_is_reported_from_the_low_status_word)
{
    reset_world();
    mock_ast_status[0] = 0x00030007u;
    run();
    ASSERT_EQ(1, mock_ast_calls);
    ASSERT_EQ(0x00030007u, status);
}

TEST(ast_status_with_only_a_high_half_is_not_a_failure)
{
    reset_world();
    /* `tst.w (-0x22,A6)` at 0x00E460C6 is the LOW word. */
    mock_ast_status[0] = 0x12340000u;
    run();
    ASSERT_EQ(1, mock_ast_calls);
    ASSERT_EQ(status_$ok, status);
}

TEST(local_flag_with_a_positive_hint_forces_a_second_remote_query)
{
    reset_world();
    mock_hint_result = 0x00;                            /* >= 0 */
    mock_ast_out[0].obj_flags[ACL_ATTR_FLAGS] = ACL_ATTR_FLAG_LOCAL;
    run();
    ASSERT_EQ(2, mock_ast_calls);                       /* 0x00E460D8 */
    ASSERT_EQ(0x0001, mock_ast_mode[0]);
    ASSERT_EQ(0x0021, mock_ast_mode[1]);
}

TEST(local_flag_with_a_negative_hint_does_not_repeat_the_query)
{
    reset_world();
    mock_hint_result = 0xFF;                            /* `bmi` at 0x00E460D6 */
    mock_ast_out[0].obj_flags[ACL_ATTR_FLAGS] = ACL_ATTR_FLAG_LOCAL;
    run();
    ASSERT_EQ(1, mock_ast_calls);
}

TEST(second_ast_query_failure_is_reported)
{
    reset_world();
    mock_ast_out[0].obj_flags[ACL_ATTR_FLAGS] = ACL_ATTR_FLAG_LOCAL;
    mock_ast_status[1] = 0x000F0002u;
    run();
    ASSERT_EQ(2, mock_ast_calls);
    ASSERT_EQ(0x000F0002u, status);
}

/* ---- 0x00E460FE: the default-ACL fixups ---- */

TEST(directory_with_acl_nil_gets_the_canned_dndcal)
{
    reset_world();
    mock_ast_out[0].obj_flags[ACL_ATTR_OBJ_TYPE] = 1;
    mock_ast_out[0].default_acl = ACL_$NIL;
    run();
    ASSERT_EQ(ACL_$DNDCAL.high, attrs.default_acl.high);
    ASSERT_EQ(ACL_$DNDCAL.low, attrs.default_acl.low);
}

TEST(directory_acl_with_top_word_1_is_bumped_to_2)
{
    reset_world();
    mock_ast_out[0].obj_flags[ACL_ATTR_OBJ_TYPE] = 2;
    mock_ast_out[0].default_acl.high = 0x00010007u;
    mock_ast_out[0].default_acl.low  = 0x00000009u;
    run();
    /* `cmpi.w #0x1,(A0)` / `move.w #0x2,(A0)` at 0x00E4613E - the TOP WORD. */
    ASSERT_EQ(0x00020007u, attrs.default_acl.high);
    ASSERT_EQ(0x00000009u, attrs.default_acl.low);
}

TEST(obj_type_3_with_a_zero_acl_high_takes_the_input_uid)
{
    reset_world();
    in_uid.high = 0x0BAD0000u;
    in_uid.low  = 0x0100BEEFu;
    mock_ast_out[0].obj_flags[ACL_ATTR_OBJ_TYPE] = 3;
    mock_ast_out[0].default_acl.high = 0;
    run();
    /* 0x00E4615A copies the ORIGINAL uid, and the trailing
     * `bclr.b #0x0,(0x8,A2)` then clears bit 24 of its low half. */
    ASSERT_EQ(0x0BAD0000u, attrs.default_acl.high);
    ASSERT_EQ(0x0000BEEFu, attrs.default_acl.low);
}

TEST(the_trailing_bclr_clears_bit_24_of_the_default_acl)
{
    reset_world();
    mock_ast_out[0].obj_flags[ACL_ATTR_OBJ_TYPE] = 7;   /* no fixup applies */
    mock_ast_out[0].default_acl.high = 0x11112222u;
    mock_ast_out[0].default_acl.low  = 0x01003333u;
    run();
    ASSERT_EQ(0x11112222u, attrs.default_acl.high);
    ASSERT_EQ(0x00003333u, attrs.default_acl.low);      /* 0x00E46162 */
}

int main(void)
{
    printf("acl_$get_obj_acl_attrs (0x00E45F78)\n");

    RUN_TEST(loc_gets_the_normalised_uid_with_bit_24_cleared);
    RUN_TEST(hint_key_is_the_low_20_bits_of_the_normalised_uid);
    RUN_TEST(hint_flag_selects_ast_mode_1);
    RUN_TEST(negative_hint_flag_selects_ast_mode_21);
    RUN_TEST(funky_bits_invoke_the_converter);
    RUN_TEST(non_funky_bits_do_not_invoke_the_converter);

    RUN_TEST(nil_uid_is_well_known_and_reports_object_not_found);
    RUN_TEST(acl_nil_is_well_known_and_leaves_status_ok);
    RUN_TEST(volume_uid_with_top_word_1_is_well_known);
    RUN_TEST(volume_uid_with_top_word_2_is_well_known);
    RUN_TEST(a_non_zero_top_byte_defeats_the_well_known_test);

    RUN_TEST(ast_failure_is_reported_from_the_low_status_word);
    RUN_TEST(ast_status_with_only_a_high_half_is_not_a_failure);
    RUN_TEST(local_flag_with_a_positive_hint_forces_a_second_remote_query);
    RUN_TEST(local_flag_with_a_negative_hint_does_not_repeat_the_query);
    RUN_TEST(second_ast_query_failure_is_reported);

    RUN_TEST(directory_with_acl_nil_gets_the_canned_dndcal);
    RUN_TEST(directory_acl_with_top_word_1_is_bumped_to_2);
    RUN_TEST(obj_type_3_with_a_zero_acl_high_takes_the_input_uid);
    RUN_TEST(the_trailing_bclr_clears_bit_24_of_the_default_acl);

    printf("\n%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed != 0;
}

#include "../get_obj_acl_attrs.c"
