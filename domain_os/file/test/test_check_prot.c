/*
 * file/test/test_check_prot.c - unit tests for FILE_$CHECK_PROT (0x00E5D172)
 *
 * The real file/check_prot.c is #included at the bottom together with
 * file/file_data.c, so the lock tables the fast path reads are the real
 * globals.  ACL_$RIGHTS is mocked.
 *
 * These tests pin down bead source-xi4k: the lock-object table starts at
 * 0xE935CC (not 0xE935BC) and the entry fields the routine reads are the
 * rights byte at +0x1A and the UID at +0x0C.
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

#include "file/file_internal.h"
#include "acl/acl.h"

/* ------------------------------------------------------------------ */
/* Globals and mocks                                                    */
/* ------------------------------------------------------------------ */

uint16_t PROC1_$AS_ID = 3;
uint16_t PROC1_$CURRENT = 1;
uint32_t NODE_$ME = 0x00012345;

static int       m_acl_calls;
static uint32_t  m_acl_result;
static status_$t m_acl_status;
static boolean   m_acl_ignore_super_seen;
static uint32_t  m_acl_mask_seen;
static int16_t   m_acl_opts_seen;

uint32_t ACL_$RIGHTS(uid_t *uid, boolean *ignore_super, uint32_t *required_mask,
                     int16_t *option_flags, status_$t *status)
{
    (void)uid;
    m_acl_calls++;
    m_acl_ignore_super_seen = *ignore_super;
    m_acl_mask_seen = *required_mask;
    m_acl_opts_seen = *option_flags;
    *status = m_acl_status;
    return m_acl_result;
}

/* ------------------------------------------------------------------ */
/* Fixtures                                                             */
/* ------------------------------------------------------------------ */

#define TEST_ASID   3
#define TEST_SLOT   7
#define TEST_ENTRY  42

static uid_t     file_uid  = { 0x11112222u, 0x33334444u };
static uint16_t  rights_out;
static status_$t status;

static void reset_world(void)
{
    memset(FILE_$LOCK_ENTRIES, 0, sizeof(FILE_$LOCK_ENTRIES));
    memset(FILE_$LOCK_TABLE, 0,
           sizeof(file_lock_table_entry_t) * FILE_LOCK_TABLE_ENTRIES);

    PROC1_$AS_ID = TEST_ASID;
    rights_out = 0xEEEE;
    status = 0x7F7F7F7F;

    m_acl_calls = 0;
    m_acl_result = 0x0000000Fu;
    m_acl_status = 0;
}

/* Point slot TEST_SLOT at LOT entry TEST_ENTRY and fill that entry. */
static void seed_entry(uid_t uid, uint8_t rights)
{
    file_lock_entry_detail_t *e = FILE_$LOT_ENTRY(TEST_ENTRY);

    FILE_$PROC_LOT_SLOT(TEST_ASID, TEST_SLOT) = TEST_ENTRY;
    e->uid_high = uid.high;
    e->uid_low  = uid.low;
    e->rights   = rights;
}

static int16_t run(uint32_t slot, uint16_t mask)
{
    return FILE_$CHECK_PROT(&file_uid, mask, slot, false, 0,
                            &rights_out, &status);
}

/* ------------------------------------------------------------------ */

TEST(status_is_cleared_on_entry)
{
    reset_world();
    (void)run(0, 1);
    ASSERT_EQ(status_$ok, status);              /* 0x00E5D18A `clr.l (A2)` */
}

TEST(slot_zero_goes_straight_to_the_acl)
{
    reset_world();
    seed_entry(file_uid, 0x0F);
    (void)run(0, 1);
    ASSERT_EQ(1, m_acl_calls);                  /* 0x00E5D18E `beq` */
}

TEST(slot_0x96_goes_to_the_acl)
{
    reset_world();
    /* `cmpi.l #0x96,D0` + `bcc` at 0x00E5D192 - 0x96 itself is out of range. */
    (void)run(0x96, 1);
    ASSERT_EQ(1, m_acl_calls);
}

TEST(empty_slot_goes_to_the_acl)
{
    reset_world();
    (void)run(TEST_SLOT, 1);
    ASSERT_EQ(1, m_acl_calls);                  /* 0x00E5D1BA `beq` */
}

TEST(matching_entry_with_the_requested_rights_succeeds)
{
    reset_world();
    seed_entry(file_uid, 0x0F);
    ASSERT_EQ(1, run(TEST_SLOT, 0x05));
    ASSERT_EQ(0, m_acl_calls);
    ASSERT_EQ(0x0F, rights_out);                /* the +0x1A byte */
    ASSERT_EQ(status_$ok, status);
}

TEST(matching_entry_without_the_requested_rights_reports_0x230002)
{
    reset_world();
    seed_entry(file_uid, 0x05);
    ASSERT_EQ(1, run(TEST_SLOT, 0x0F));
    ASSERT_EQ(0x00230002u, status);             /* 0x00E5D20C */
    ASSERT_EQ(0, m_acl_calls);
}

TEST(matching_entry_with_no_rights_reports_0x230001)
{
    reset_world();
    seed_entry(file_uid, 0x00);
    ASSERT_EQ(1, run(TEST_SLOT, 0x01));
    ASSERT_EQ(0x00230001u, status);             /* 0x00E5D1FE */
    ASSERT_EQ(0, m_acl_calls);
}

TEST(the_ignore_bit_sends_the_request_to_the_acl)
{
    reset_world();
    seed_entry(file_uid, (uint8_t)(0x10 | 0x0F));
    (void)run(TEST_SLOT, 0x01);
    /* `btst.l #0x4,D4` at 0x00E5D1F4. */
    ASSERT_EQ(1, m_acl_calls);
}

TEST(a_foreign_uid_sends_the_request_to_the_acl)
{
    reset_world();
    uid_t other = { 0x99998888u, 0x77776666u };
    seed_entry(other, 0x0F);
    (void)run(TEST_SLOT, 0x01);
    ASSERT_EQ(1, m_acl_calls);
}

TEST(rights_out_is_written_before_the_uid_check)
{
    reset_world();
    uid_t other = { 0x99998888u, 0x77776666u };
    seed_entry(other, 0x0B);
    m_acl_result = 0x00000003u;
    (void)run(TEST_SLOT, 0x01);
    /* 0x00E5D1E2 stores the entry's rights unconditionally; the ACL call at
     * 0x00E5D232 then overwrites the same cell. */
    ASSERT_EQ(0x0003, rights_out);
}

TEST(uid_compare_uses_the_entry_field_at_0x0c)
{
    reset_world();
    file_lock_entry_detail_t *e = FILE_$LOT_ENTRY(TEST_ENTRY);
    FILE_$PROC_LOT_SLOT(TEST_ASID, TEST_SLOT) = TEST_ENTRY;
    /* Put the UID where the OLD (wrong) model expected it - at +0x00 - and
     * confirm the routine does not see it. */
    e->context  = file_uid.high;
    e->node_low = file_uid.low;
    e->rights   = 0x0F;
    (void)run(TEST_SLOT, 0x01);
    ASSERT_EQ(1, m_acl_calls);

    /* Now put it at +0x0C where the assembly reads it. */
    reset_world();
    seed_entry(file_uid, 0x0F);
    ASSERT_EQ(1, run(TEST_SLOT, 0x01));
    ASSERT_EQ(0, m_acl_calls);
}

TEST(per_process_table_is_indexed_by_asid_and_slot)
{
    reset_world();
    seed_entry(file_uid, 0x0F);
    /* The same slot in a DIFFERENT address space must miss. */
    PROC1_$AS_ID = TEST_ASID + 1;
    (void)run(TEST_SLOT, 0x01);
    ASSERT_EQ(1, m_acl_calls);
}

TEST(acl_path_passes_the_mask_and_the_two_by_reference_parameters)
{
    reset_world();
    m_acl_result = 0x00010007u;
    m_acl_status = 0x00230001u;
    rights_out = 0;
    int16_t r = FILE_$CHECK_PROT(&file_uid, 0x000B, 0, true, 1,
                                 &rights_out, &status);
    ASSERT_EQ(1, m_acl_calls);
    ASSERT_EQ(0x0000000Bu, m_acl_mask_seen);    /* zero-extended word */
    ASSERT_EQ(true, m_acl_ignore_super_seen);
    ASSERT_EQ(1, m_acl_opts_seen);
    /* `move.w D0w,(A1)` at 0x00E5D236 keeps only the low word. */
    ASSERT_EQ(0x0007, rights_out);
    ASSERT_EQ(0x0007, (uint16_t)r);
    ASSERT_EQ(0x00230001u, status);
}

int main(void)
{
    printf("FILE_$CHECK_PROT (0x00E5D172)\n");

    RUN_TEST(status_is_cleared_on_entry);
    RUN_TEST(slot_zero_goes_straight_to_the_acl);
    RUN_TEST(slot_0x96_goes_to_the_acl);
    RUN_TEST(empty_slot_goes_to_the_acl);
    RUN_TEST(matching_entry_with_the_requested_rights_succeeds);
    RUN_TEST(matching_entry_without_the_requested_rights_reports_0x230002);
    RUN_TEST(matching_entry_with_no_rights_reports_0x230001);
    RUN_TEST(the_ignore_bit_sends_the_request_to_the_acl);
    RUN_TEST(a_foreign_uid_sends_the_request_to_the_acl);
    RUN_TEST(rights_out_is_written_before_the_uid_check);
    RUN_TEST(uid_compare_uses_the_entry_field_at_0x0c);
    RUN_TEST(per_process_table_is_indexed_by_asid_and_slot);
    RUN_TEST(acl_path_passes_the_mask_and_the_two_by_reference_parameters);

    printf("\n%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed != 0;
}

#include "../check_prot.c"
#include "../file_data.c"
