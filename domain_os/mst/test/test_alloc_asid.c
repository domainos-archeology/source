/*
 * mst/test/test_alloc_asid.c - unit tests for MST_$ALLOC_ASID (0x00E42D3A)
 *
 * The routine scans MST_$ASID_LIST for a clear bit (ASID N is bit N & 7 of
 * byte (0x3f - N) >> 3), marks it with MST_$SET, asks MST_$ALLOC_TABLE_PAGE
 * for the ASID's first MST page and then checks / initialises the first
 * segment-table entry of that page.  The entry address is
 * MSTE_PAGES + (page - 1) * 0x400: the image adds page * 0x400 to 0xEF6400
 * and then addresses the fields as (-0x400,A0) .. (-0x3f6,A0).
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

/* Module data the test supplies instead of mst_data.c / uid_data.c. */
uint8_t  MST_$ASID_LIST[8];
uint16_t MST_ASID_BASE[MST_MAX_ASIDS];
uint16_t MST[MST_TABLE_ENTRIES];
uid_t    OS_WIRED_$UID = { 0x00000200u, 0x00000000u };

/* Three one-based MSTE pages: page 1 is arena[0], page 2 is arena[0x400]. */
static uint8_t page_arena[0x400 * 3];

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

static int      mock_lock_calls;
static int      mock_unlock_calls;
static int16_t  mock_lock_id;
static int      mock_set_calls;
static uint16_t mock_set_size;
static uint16_t mock_set_bit;
static int      mock_alloc_calls;
static uint16_t mock_alloc_asid;
static uint16_t mock_alloc_flags;
static uint16_t *mock_alloc_table_ptr;
static status_$t mock_alloc_status;
static uint16_t mock_alloc_page;   /* written into *table_ptr on success */

void ML_$LOCK(int16_t resource_id)
{
    mock_lock_calls++;
    mock_lock_id = resource_id;
}

void ML_$UNLOCK(int16_t resource_id)
{
    mock_unlock_calls++;
    mock_lock_id = resource_id;
}

void MST_$SET(void *bitmap, uint16_t size, uint16_t bit_index)
{
    uint8_t *bytes = (uint8_t *)bitmap;
    mock_set_calls++;
    mock_set_size = size;
    mock_set_bit = bit_index;
    bytes[(uint16_t)(((size - 1) | 0x0f) - bit_index) >> 3] |= (uint8_t)(1u << (bit_index & 7));
}

status_$t MST_$ALLOC_TABLE_PAGE(uint16_t asid, uint16_t flags, uint16_t *table_ptr)
{
    mock_alloc_calls++;
    mock_alloc_asid = asid;
    mock_alloc_flags = flags;
    mock_alloc_table_ptr = table_ptr;
    if (mock_alloc_status == status_$ok && *table_ptr == 0) {
        *table_ptr = mock_alloc_page;
    }
    return mock_alloc_status;
}

#include "mst/alloc_asid.c"

/* ------------------------------------------------------------------ */

static void reset_state(void)
{
    int i;

    memset(page_arena, 0, sizeof(page_arena));
    memset(MST_$ASID_LIST, 0, sizeof(MST_$ASID_LIST));
    memset(MST, 0, sizeof(MST));
    for (i = 0; i < MST_MAX_ASIDS; i++) {
        MST_ASID_BASE[i] = (uint16_t)(i * 0x20);
    }
    mock_lock_calls = 0;
    mock_unlock_calls = 0;
    mock_lock_id = -1;
    mock_set_calls = 0;
    mock_set_size = 0;
    mock_set_bit = 0xFFFF;
    mock_alloc_calls = 0;
    mock_alloc_asid = 0xFFFF;
    mock_alloc_flags = 0xFFFF;
    mock_alloc_table_ptr = 0;
    mock_alloc_status = status_$ok;
    mock_alloc_page = 2;

    /* VA MST_PAGE_TABLE_BASE (0xEF6400) is the arena, so page 1 starts at
     * arena[0] and page 2 at arena[0x400]. */
    ARCH_HOST_VA_BASE = (uintptr_t)page_arena - MST_PAGE_TABLE_BASE;
}

/* ASID 0 taken (bit 0 of byte 7): the scan returns ASID 1 and MST_$SET is
 * asked for bit 1 of a 58-bit set (0x00E42D7C..0x00E42D8A). */
static void test_first_free_asid_is_returned(void)
{
    status_$t status = 0x12345678;
    uint16_t asid;

    MST_$ASID_LIST[7] = 0x01;           /* ASID 0 allocated */

    asid = MST_$ALLOC_ASID(&status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, asid);
    ASSERT_EQ(1, mock_lock_calls);
    ASSERT_EQ(1, mock_unlock_calls);
    ASSERT_EQ(MST_LOCK_ASID, mock_lock_id);
    ASSERT_EQ(1, mock_set_calls);
    ASSERT_EQ(MST_MAX_ASIDS, mock_set_size);
    ASSERT_EQ(1, mock_set_bit);
    ASSERT_EQ(0x03, MST_$ASID_LIST[7]);

    /* MST_$ALLOC_TABLE_PAGE(asid, 0, &MST[MST_ASID_BASE[asid]]) */
    ASSERT_EQ(1, mock_alloc_calls);
    ASSERT_EQ(1, mock_alloc_asid);
    ASSERT_EQ(0, mock_alloc_flags);
    ASSERT_EQ((unsigned long)&MST[0x20], (unsigned long)mock_alloc_table_ptr);
}

/* An empty entry is initialised with OS_WIRED_$UID, area_id 0 and the flags
 * word masked with 0x3e00 (0x00E42DF6..0x00E42E08); the entry for the
 * one-based page 2 is arena[0x400], not arena[0x800]. */
static void test_empty_entry_is_initialised_one_page_down(void)
{
    status_$t status = 0x12345678;
    mst_entry_t *entry = (mst_entry_t *)&page_arena[0x400];
    uint16_t asid;

    MST_$ASID_LIST[7] = 0x01;
    entry->flags = 0xFFFF;
    entry->page_info = 0x55;

    asid = MST_$ALLOC_ASID(&status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, asid);
    ASSERT_EQ(2, MST[0x20]);
    ASSERT_EQ(OS_WIRED_$UID.high, entry->uid.high);
    ASSERT_EQ(OS_WIRED_$UID.low, entry->uid.low);
    ASSERT_EQ(0, entry->area_id);
    ASSERT_EQ(0x3e00, entry->flags);
    ASSERT_EQ(0x55, entry->page_info);            /* untouched */
    /* page 3's entry (arena[0x800]) was never written */
    ASSERT_EQ(0, ((mst_entry_t *)&page_arena[0x800])->uid.high);
}

/* An entry already holding OS_WIRED_$UID is left exactly as it is
 * (0x00E42DDE..0x00E42DE4 -> 0x00E42E0E). */
static void test_entry_already_wired_is_left_alone(void)
{
    status_$t status = 0x12345678;
    mst_entry_t *entry = (mst_entry_t *)&page_arena[0x400];
    uint16_t asid;

    MST_$ASID_LIST[7] = 0x01;
    entry->uid = OS_WIRED_$UID;
    entry->area_id = 0x1234;
    entry->flags = 0xFFFF;

    asid = MST_$ALLOC_ASID(&status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, asid);
    ASSERT_EQ(0x1234, entry->area_id);
    ASSERT_EQ(0xFFFF, entry->flags);
}

/* Any other non-zero UID high word is status_$no_space_available and the
 * result ASID is 0 (0x00E42DE6..0x00E42DF4, 0x00E42E30). */
static void test_foreign_uid_is_no_space(void)
{
    status_$t status = 0x12345678;
    mst_entry_t *entry = (mst_entry_t *)&page_arena[0x400];
    uint16_t asid;

    MST_$ASID_LIST[7] = 0x01;
    entry->uid.high = 0xDEADBEEFu;
    entry->uid.low = 0;

    asid = MST_$ALLOC_ASID(&status);

    ASSERT_EQ(status_$no_space_available, status);
    ASSERT_EQ(0, asid);
    ASSERT_EQ(1, mock_unlock_calls);
    ASSERT_EQ(0xDEADBEEFu, entry->uid.high);      /* not overwritten */
}

/* A failing MST_$ALLOC_TABLE_PAGE is passed straight through and the
 * entry is never touched (0x00E42DB2..0x00E42DB6). */
static void test_table_page_failure_is_passed_through(void)
{
    status_$t status = 0x12345678;
    uint16_t asid;

    MST_$ASID_LIST[7] = 0x01;
    mock_alloc_status = status_$pmap_vm_resources_exhausted;

    asid = MST_$ALLOC_ASID(&status);

    ASSERT_EQ(status_$pmap_vm_resources_exhausted, status);
    ASSERT_EQ(0, asid);
    ASSERT_EQ(1, mock_set_calls);                 /* the bit was already set */
    ASSERT_EQ(0, ((mst_entry_t *)&page_arena[0x400])->uid.high);
}

/* All 58 ASIDs allocated: 58 probes (dbf #0x39), no MST_$SET, status
 * status_$no_asid_available (0x00E42D70). */
static void test_full_set_is_no_asid_available(void)
{
    status_$t status = 0x12345678;
    uint16_t asid;

    /* bits 0..57: bytes 7..1 all set, byte 0 holds bits 56,57 */
    memset(&MST_$ASID_LIST[1], 0xFF, 7);
    MST_$ASID_LIST[0] = 0x03;

    asid = MST_$ALLOC_ASID(&status);

    ASSERT_EQ(status_$no_asid_available, status);
    ASSERT_EQ(0, asid);
    ASSERT_EQ(0, mock_set_calls);
    ASSERT_EQ(0, mock_alloc_calls);
    ASSERT_EQ(1, mock_lock_calls);
    ASSERT_EQ(1, mock_unlock_calls);
}

/* Bit 58 (byte 0, bit 2) is outside the 58-probe scan: with everything
 * below it taken the scan fails even though that bit is clear. */
static void test_scan_stops_at_asid_57(void)
{
    status_$t status = 0x12345678;
    uint16_t asid;

    memset(&MST_$ASID_LIST[1], 0xFF, 7);
    MST_$ASID_LIST[0] = 0x01;                     /* ASID 56 taken, 57 free */

    asid = MST_$ALLOC_ASID(&status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(57, asid);
    ASSERT_EQ(57, mock_set_bit);
    ASSERT_EQ(0x03, MST_$ASID_LIST[0]);
}

int main(void)
{
    printf("MST_$ALLOC_ASID tests:\n");
    RUN_TEST(first_free_asid_is_returned);
    RUN_TEST(empty_entry_is_initialised_one_page_down);
    RUN_TEST(entry_already_wired_is_left_alone);
    RUN_TEST(foreign_uid_is_no_space);
    RUN_TEST(table_page_failure_is_passed_through);
    RUN_TEST(full_set_is_no_asid_available);
    RUN_TEST(scan_stops_at_asid_57);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
