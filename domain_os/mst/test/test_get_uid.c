/*
 * mst/test/test_get_uid.c - unit tests for MST_$GET_VA_INFO (0x00E4404E),
 * MST_$GET_UID (0x00E43FD8) and MST_$GET_UID_ASID (0x00E44010)
 *
 * MST_$GET_VA_INFO rejects ASIDs above 0x39, otherwise copies the 16-byte
 * MST entry mst_$va_to_pte finds under lock 0x0C and reports its UID, the
 * segment base (area_id << 15) plus the low 15 bits of the VA, and flags
 * bits 15 (active) and 14 (modified) as Domain booleans.  The wrappers pass
 * &PROC1_$AS_ID / a copied ASID plus three throw-away locals.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "mst/mst_internal.h"

uint16_t PROC1_$AS_ID;

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

static int         mock_lock_depth;
static int         mock_lock_calls;
static int         mock_unlock_calls;
static int16_t     mock_lock_id;
static int         mock_pte_calls;
static uint16_t    mock_pte_asid;
static uint32_t    mock_pte_va;
static uint16_t   *mock_pte_prot_out;
static status_$t   mock_pte_status;
static int         mock_pte_lock_depth;
static mst_entry_t mock_entry;

void ML_$LOCK(int16_t resource_id)
{
    mock_lock_calls++;
    mock_lock_depth++;
    mock_lock_id = resource_id;
}

void ML_$UNLOCK(int16_t resource_id)
{
    mock_unlock_calls++;
    mock_lock_depth--;
    mock_lock_id = resource_id;
}

void mst_$va_to_pte(uint16_t asid, uint32_t va, uint16_t *prot_out,
                    void **entry_out, status_$t *status)
{
    mock_pte_calls++;
    mock_pte_asid = asid;
    mock_pte_va = va;
    mock_pte_prot_out = prot_out;
    mock_pte_lock_depth = mock_lock_depth;
    *prot_out = 0x1F;
    *entry_out = &mock_entry;
    *status = mock_pte_status;
}

#include "mst/get_uid.c"

/* ------------------------------------------------------------------ */

static void reset_state(void)
{
    mock_lock_depth = 0;
    mock_lock_calls = 0;
    mock_unlock_calls = 0;
    mock_lock_id = -1;
    mock_pte_calls = 0;
    mock_pte_asid = 0xFFFF;
    mock_pte_va = 0;
    mock_pte_prot_out = 0;
    mock_pte_status = status_$ok;
    mock_pte_lock_depth = -1;
    memset(&mock_entry, 0, sizeof(mock_entry));
    mock_entry.uid.high = 0x11223344u;
    mock_entry.uid.low = 0x55667788u;
    mock_entry.area_id = 0x0123;
    mock_entry.flags = 0xC001;
    PROC1_$AS_ID = 7;
}

static void test_va_info_reports_entry(void)
{
    uint16_t asid = 3;
    uint32_t va = 0x00A57FFF;       /* segment 0x14A, offset 0x7FFF */
    uid_t uid = { 0, 0 };
    uint32_t adjusted = 0;
    uint16_t prot = 0;
    boolean active = 0x55, modified = 0x55;
    status_$t status = 0x12345678;

    MST_$GET_VA_INFO(&asid, &va, &uid, &adjusted, &prot, &active, &modified, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, mock_pte_calls);
    ASSERT_EQ(3, mock_pte_asid);
    ASSERT_EQ(0x00A57FFF, mock_pte_va);
    ASSERT_EQ((unsigned long)&prot, (unsigned long)mock_pte_prot_out);
    ASSERT_EQ(0x1F, prot);
    ASSERT_EQ(1, mock_pte_lock_depth);              /* looked up under lock */
    ASSERT_EQ(MST_LOCK_ASID, mock_lock_id);
    ASSERT_EQ(1, mock_lock_calls);
    ASSERT_EQ(1, mock_unlock_calls);
    ASSERT_EQ(0x11223344u, uid.high);
    ASSERT_EQ(0x55667788u, uid.low);
    ASSERT_EQ((0x0123u << 15) + 0x7FFF, adjusted);   /* 0x00E440DE..F4 */
    ASSERT_EQ(0xFF, (uint8_t)active);                /* bit 15 */
    ASSERT_EQ(0xFF, (uint8_t)modified);              /* bit 14 */
}

static void test_va_info_flag_bits_separately(void)
{
    uint16_t asid = 0x39;           /* the highest accepted ASID */
    uint32_t va = 0;
    uid_t uid;
    uint32_t adjusted;
    uint16_t prot;
    boolean active = 0x55, modified = 0x55;
    status_$t status;

    mock_entry.flags = 0x8000;
    MST_$GET_VA_INFO(&asid, &va, &uid, &adjusted, &prot, &active, &modified, &status);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0xFF, (uint8_t)active);
    ASSERT_EQ(0x00, (uint8_t)modified);

    mock_entry.flags = 0x4000;
    MST_$GET_VA_INFO(&asid, &va, &uid, &adjusted, &prot, &active, &modified, &status);
    ASSERT_EQ(0x00, (uint8_t)active);
    ASSERT_EQ(0xFF, (uint8_t)modified);

    mock_entry.flags = 0x3FFF;
    MST_$GET_VA_INFO(&asid, &va, &uid, &adjusted, &prot, &active, &modified, &status);
    ASSERT_EQ(0x00, (uint8_t)active);
    ASSERT_EQ(0x00, (uint8_t)modified);
}

/* `cmpi.w #0x39` / `bls`: 0x3A is rejected before any lock is taken. */
static void test_va_info_rejects_asid_above_0x39(void)
{
    uint16_t asid = 0x3A;
    uint32_t va = 0;
    uid_t uid = { 1, 2 };
    uint32_t adjusted = 9;
    uint16_t prot = 9;
    boolean active = 0x55, modified = 0x55;
    status_$t status = 0;

    MST_$GET_VA_INFO(&asid, &va, &uid, &adjusted, &prot, &active, &modified, &status);

    ASSERT_EQ(status_$reference_to_illegal_address, status);
    ASSERT_EQ(0, mock_lock_calls);
    ASSERT_EQ(0, mock_pte_calls);
    ASSERT_EQ(1, uid.high);
    ASSERT_EQ(9, adjusted);
    ASSERT_EQ(0x55, (uint8_t)active);
}

/* A failing lookup is passed through after the unlock and nothing else is
 * written (0x00E440C8..0x00E440CE). */
static void test_va_info_passes_lookup_failure(void)
{
    uint16_t asid = 1;
    uint32_t va = 0;
    uid_t uid = { 1, 2 };
    uint32_t adjusted = 9;
    uint16_t prot;
    boolean active = 0x55, modified = 0x55;
    status_$t status = 0;

    mock_pte_status = status_$reference_to_illegal_address;
    MST_$GET_VA_INFO(&asid, &va, &uid, &adjusted, &prot, &active, &modified, &status);

    ASSERT_EQ(status_$reference_to_illegal_address, status);
    ASSERT_EQ(1, mock_lock_calls);
    ASSERT_EQ(1, mock_unlock_calls);
    ASSERT_EQ(1, uid.high);
    ASSERT_EQ(9, adjusted);
    ASSERT_EQ(0x55, (uint8_t)modified);
}

/* MST_$GET_UID uses PROC1_$AS_ID (0xE2060A) as the ASID. */
static void test_get_uid_uses_current_asid(void)
{
    uint32_t va = 0x00008010;
    uid_t uid = { 0, 0 };
    uint32_t adjusted = 0;
    status_$t status = 0x12345678;

    PROC1_$AS_ID = 0x21;
    MST_$GET_UID(&va, &uid, &adjusted, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x21, mock_pte_asid);
    ASSERT_EQ(0x00008010, mock_pte_va);
    ASSERT_EQ(0x11223344u, uid.high);
    ASSERT_EQ((0x0123u << 15) + 0x0010, adjusted);
}

/* MST_$GET_UID_ASID copies the caller's ASID word into its own frame. */
static void test_get_uid_asid_uses_given_asid(void)
{
    uint16_t asid = 0x2A;
    uint32_t va = 0x00008010;
    uid_t uid = { 0, 0 };
    uint32_t adjusted = 0;
    status_$t status = 0x12345678;

    PROC1_$AS_ID = 0x21;
    MST_$GET_UID_ASID(&asid, &va, &uid, &adjusted, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x2A, mock_pte_asid);
    ASSERT_EQ(0x55667788u, uid.low);
}

int main(void)
{
    printf("MST_$GET_VA_INFO / MST_$GET_UID / MST_$GET_UID_ASID tests:\n");
    RUN_TEST(va_info_reports_entry);
    RUN_TEST(va_info_flag_bits_separately);
    RUN_TEST(va_info_rejects_asid_above_0x39);
    RUN_TEST(va_info_passes_lookup_failure);
    RUN_TEST(get_uid_uses_current_asid);
    RUN_TEST(get_uid_asid_uses_given_asid);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
