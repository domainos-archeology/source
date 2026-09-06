/*
 * area/test/test_create.c - Unit tests for the AREA_$CREATE family
 *
 * These tests #include area/create.c directly and drive the real
 * area_$internal_create / AREA_$CREATE / AREA_$CREATE_FROM through mocked
 * callees.  They pin down the behaviours the 2026-09-06 audit found wrong:
 *
 *   - the backing-store overhead arithmetic at 0x00E078F8-0x00E07910, where
 *     the sign test is on the signed 32-bit (virt_size-1) BEFORE the shift,
 *     so virt_size == 0 asks the partner for 0x400 bytes (not 0x1000000);
 *   - AREA_$PARTNER being passed BY ADDRESS (0x00E0792E);
 *   - the "no free entry" path returning 0 with status_$area_none_free;
 *   - the original AREA_$CREATE_FROM bug at 0x00E07AC4, where the exhausted
 *     hash pool returns the low word of the remote UID instead of the area
 *     id because D2 is never overwritten.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/*
 * Suppress area_internal.h (and with it as/ast/cal/ml/mmu/network/proc1/
 * rem_file/wp) so the test only pulls in the real area.h.
 */
#define AREA_INTERNAL_H
#define MISC_CRASH_SYSTEM_H
#define MATH_H

#include "area/area.h"

/* ==========================================================================
 * Test infrastructure
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %s... ", #name);                                        \
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

/* ==========================================================================
 * Mock area table
 *
 * AREA_TABLE_BASE is the fixed image address 0xD94C00 on the target; rebind
 * it to a host array so AREA_ID_TO_ENTRY / AREA_ENTRY_TO_ID work here.
 * ========================================================================== */

static area_$entry_t mock_area_table[AREA_MAX_ENTRIES];

#undef AREA_TABLE_BASE
#define AREA_TABLE_BASE ((uintptr_t)mock_area_table)

/* ==========================================================================
 * Module globals normally defined in area/area_data.c
 * ========================================================================== */

area_$entry_t   *AREA_$FREE_LIST = NULL;
int16_t          AREA_$N_FREE = 0;
int16_t          AREA_$N_AREAS = 0;
int16_t          AREA_$PARTNER_PKT_SIZE = 0;
int16_t          AREA_$CR_DUP = 0;
int16_t          AREA_$DEL_DUP = 0;
uid_t            AREA_$PARTNER = { 0, 0 };
uint32_t         AREA_$NEXT_CALLER_ID = 0;
area_$uid_hash_t *AREA_$UID_HASH_FREE = NULL;
area_$uid_hash_t *AREA_$UID_HASH[AREA_UID_HASH_BUCKETS];
area_$uid_hash_t  AREA_$UID_HASH_POOL[AREA_UID_HASH_BUCKETS];
area_$entry_t    *AREA_$ASID_LIST[AREA_MAX_ENTRIES];
ec_$eventcount_t  AREA_$IN_TRANS_EC;

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

int16_t  CAL_$BOOT_VOLX = 7;
uint16_t PROC1_$AS_ID = 3;

static int      lock_depth;
static int      lock_calls;
static int      unlock_calls;

/* area_$alloc_resources */
static boolean  mock_alloc_result;
static int      alloc_calls;
static int16_t  alloc_last_count;
static area_$entry_t *mock_alloc_supplies;   /* pushed onto the free list */

/* REM_FILE_$CREATE_AREA */
static int       rfca_calls;
static void     *rfca_addr_info;
static uint32_t  rfca_area_size;
static uint32_t  rfca_area_offset;
static uint32_t  rfca_caller_id;
static uint8_t   rfca_flags;
static uint16_t  rfca_volx_out;
static uint16_t  rfca_result;
static status_$t rfca_status;

/* NETWORK_$GET_PKT_SIZE */
static int       gps_calls;
static uint32_t *gps_addr;
static uint16_t  gps_max_size;
static uint16_t  gps_result;

/* area_$resize */
static int       resize_calls;
static int16_t   resize_area_id;
static area_$entry_t *resize_entry;
static uint32_t  resize_virt;
static uint32_t  resize_commit;
static int16_t   resize_is_grow;
static status_$t resize_status;

/* area_$internal_delete / AREA_$DELETE */
static int       internal_delete_calls;
static area_$entry_t *internal_delete_entry;
static int16_t   internal_delete_area_id;
static boolean   internal_delete_unlink;
static int       area_delete_calls;
static area_$handle_t area_delete_handle;

void ML_$LOCK(int16_t resource_id)
{
    (void)resource_id;
    lock_calls++;
    lock_depth++;
}

void ML_$UNLOCK(int16_t resource_id)
{
    (void)resource_id;
    unlock_calls++;
    lock_depth--;
}

boolean area_$alloc_resources(int16_t count)
{
    alloc_calls++;
    alloc_last_count = count;
    if (mock_alloc_result < 0 && mock_alloc_supplies != NULL) {
        AREA_$FREE_LIST = mock_alloc_supplies;
        mock_alloc_supplies = NULL;
    }
    return mock_alloc_result;
}

uint16_t REM_FILE_$CREATE_AREA(void *addr_info, uint32_t area_type,
                               uint32_t area_size, uint32_t area_offset,
                               uint8_t flags, uint16_t *pkt_size_out,
                               status_$t *status)
{
    rfca_calls++;
    rfca_addr_info = addr_info;
    rfca_area_size = area_type;      /* 2nd arg: total_size (D4) */
    rfca_area_offset = area_size;    /* 3rd arg: total_commit (D1) */
    rfca_caller_id = area_offset;    /* 4th arg: caller_id */
    rfca_flags = flags;
    *pkt_size_out = rfca_volx_out;
    *status = rfca_status;
    return rfca_result;
}

uint16_t NETWORK_$GET_PKT_SIZE(uint32_t *dest_addr, uint16_t max_size)
{
    gps_calls++;
    gps_addr = dest_addr;
    gps_max_size = max_size;
    return gps_result;
}

void area_$resize(int16_t area_id, area_$entry_t *entry,
                  uint32_t virt_size, uint32_t commit_size,
                  int16_t is_grow, status_$t *status_p)
{
    resize_calls++;
    resize_area_id = area_id;
    resize_entry = entry;
    resize_virt = virt_size;
    resize_commit = commit_size;
    resize_is_grow = is_grow;
    *status_p = resize_status;
}

void area_$internal_delete(area_$entry_t *entry, int16_t area_id,
                           status_$t *status_p, boolean do_unlink)
{
    internal_delete_calls++;
    internal_delete_entry = entry;
    internal_delete_area_id = area_id;
    internal_delete_unlink = do_unlink;
    *status_p = status_$ok;
}

void AREA_$DELETE(area_$handle_t handle, status_$t *status_ret)
{
    area_delete_calls++;
    area_delete_handle = handle;
    *status_ret = status_$ok;
}

short M$OIU$WLW(long dividend, short divisor)
{
    return (short)((unsigned long)dividend % (unsigned short)divisor);
}

void CRASH_SYSTEM(const status_$t *status_p)
{
    printf("\n    CRASH_SYSTEM(0x%08x) unexpectedly called\n",
           (unsigned)*status_p);
    tests_failed++;
    current_failed = 1;
}

status_$t Area_Internal_Error = 0x00320000;

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../create.c"

/* ==========================================================================
 * Fixtures
 * ========================================================================== */

static void reset_mocks(void)
{
    memset(mock_area_table, 0, sizeof(mock_area_table));
    memset(AREA_$UID_HASH, 0, sizeof(AREA_$UID_HASH));
    memset(AREA_$UID_HASH_POOL, 0, sizeof(AREA_$UID_HASH_POOL));
    memset(AREA_$ASID_LIST, 0, sizeof(AREA_$ASID_LIST));

    AREA_$FREE_LIST = NULL;
    AREA_$N_FREE = 0;
    AREA_$N_AREAS = AREA_MAX_ENTRIES;
    AREA_$PARTNER_PKT_SIZE = 0;
    AREA_$CR_DUP = 0;
    AREA_$DEL_DUP = 0;
    AREA_$PARTNER.high = 0;
    AREA_$PARTNER.low = 0;
    AREA_$NEXT_CALLER_ID = 100;
    AREA_$UID_HASH_FREE = NULL;

    CAL_$BOOT_VOLX = 7;
    PROC1_$AS_ID = 3;

    lock_depth = lock_calls = unlock_calls = 0;

    mock_alloc_result = false;
    mock_alloc_supplies = NULL;
    alloc_calls = 0;
    alloc_last_count = 0;

    rfca_calls = 0;
    rfca_addr_info = NULL;
    rfca_area_size = rfca_area_offset = rfca_caller_id = 0;
    rfca_flags = 0;
    rfca_volx_out = 0x11;
    rfca_result = 0x22;
    rfca_status = status_$ok;

    gps_calls = 0;
    gps_addr = NULL;
    gps_max_size = 0;
    gps_result = 0x400;

    resize_calls = 0;
    resize_area_id = 0;
    resize_entry = NULL;
    resize_virt = resize_commit = 0;
    resize_is_grow = -1;
    resize_status = status_$ok;

    internal_delete_calls = 0;
    internal_delete_entry = NULL;
    internal_delete_area_id = 0;
    internal_delete_unlink = false;
    area_delete_calls = 0;
    area_delete_handle = 0;
}

/* Put `count` entries, starting at table index `first`, on the free list. */
static void seed_free_list(int first, int count)
{
    int i;

    AREA_$FREE_LIST = &mock_area_table[first];
    for (i = 0; i < count; i++) {
        mock_area_table[first + i].next =
            (i + 1 < count) ? &mock_area_table[first + i + 1] : NULL;
    }
    AREA_$N_FREE = (int16_t)count;
}

/* ==========================================================================
 * area_$internal_create
 * ========================================================================== */

/*
 * The regression the audit flagged: with virt_size == 0 and a diskless
 * partner, the overhead arithmetic must ask REM_FILE_$CREATE_AREA for
 * exactly 0x400 bytes.  A logical shift on an unsigned virt_size makes the
 * `bpl` at 0x00E078FC dead and produces 0x1000000 instead.
 */
TEST(virt_size_zero_asks_partner_for_0x400)
{
    status_$t status = 0xDEADBEEF;
    uint32_t handle;

    seed_free_list(0, 4);
    AREA_$PARTNER.low = 0x00012345;     /* diskless: we have a partner */

    handle = area_$internal_create(0, 0, 0, 3, 1, false, &status);

    ASSERT_EQ(1, rfca_calls);
    ASSERT_EQ(0x400, rfca_area_size);
    /* total_commit = commit_size + (total_size - virt_size) */
    ASSERT_EQ(0x400, rfca_area_offset);
    ASSERT_EQ(status_$ok, status);
    /* virt_size == 0 skips area_$resize entirely (0x00E07970/0x00E0798C). */
    ASSERT_EQ(0, resize_calls);
    ASSERT_TRUE(handle != 0);
}

/* A non-zero size takes the `bpl` branch: overhead is one 1KB unit per
 * 4 * 64KB of virtual size, rounded up. */
TEST(overhead_for_nonzero_sizes)
{
    status_$t status = 0xDEADBEEF;

    seed_free_list(0, 4);
    AREA_$PARTNER.low = 0x00012345;

    /* 0x8000 rounds to 0x8000; (0x7FFF >> 16) = 0 -> (0 >> 2) + 1 = 1 unit */
    (void)area_$internal_create(0x8000, 0x1000, 0, 3, 1, false, &status);
    ASSERT_EQ(0x8000 + 0x400, rfca_area_size);
    ASSERT_EQ(0x1000 + 0x400, rfca_area_offset);

    reset_mocks();
    seed_free_list(0, 4);
    AREA_$PARTNER.low = 0x00012345;

    /* 0x100000: (0xFFFFF >> 16) = 0x0F -> (0x0F >> 2) + 1 = 4 units */
    (void)area_$internal_create(0x100000, 0, 0, 3, 1, false, &status);
    ASSERT_EQ(0x100000 + 4 * 0x400, rfca_area_size);
}

/* AREA_$PARTNER is passed by address to both network callees
 * (0x00E0792E and 0x00E0795C). */
TEST(partner_is_passed_by_address)
{
    status_$t status = 0xDEADBEEF;

    seed_free_list(0, 4);
    AREA_$PARTNER.low = 0x00012345;
    AREA_$PARTNER_PKT_SIZE = 0;
    rfca_volx_out = 0x0400;
    gps_result = 0x380;

    (void)area_$internal_create(0x8000, 0, 0, 3, 1, false, &status);

    ASSERT_EQ((uintptr_t)&AREA_$PARTNER, (uintptr_t)rfca_addr_info);
    ASSERT_EQ(1, gps_calls);
    ASSERT_EQ((uintptr_t)&AREA_$PARTNER, (uintptr_t)gps_addr);
    ASSERT_EQ(0x0400, gps_max_size);
    ASSERT_EQ(0x380, AREA_$PARTNER_PKT_SIZE);
}

/* With no partner the area is backed on the local boot volume and no
 * network traffic happens at all (0x00E078E2). */
TEST(no_partner_uses_boot_volx)
{
    status_$t status = 0xDEADBEEF;
    uint32_t handle;

    seed_free_list(0, 4);
    AREA_$PARTNER.low = 0;
    CAL_$BOOT_VOLX = 9;

    handle = area_$internal_create(0x8000, 0x400, 0, 3, 1, false, &status);

    ASSERT_EQ(0, rfca_calls);
    ASSERT_EQ(0, gps_calls);
    ASSERT_EQ(9, mock_area_table[0].volx);
    ASSERT_EQ(0, mock_area_table[0].remote_volx);
    ASSERT_EQ(1, AREA_HANDLE_TO_ID(handle));
}

/* The free list is empty and area_$alloc_resources reports false (0):
 * status_$area_none_free, handle 0, and the local path unlocks. */
TEST(no_free_entry_fails_and_unlocks)
{
    status_$t status = 0xDEADBEEF;
    uint32_t handle;

    AREA_$FREE_LIST = NULL;
    mock_alloc_result = false;

    handle = area_$internal_create(0x8000, 0, 0, 3, 1, false, &status);

    ASSERT_EQ(0, handle);
    ASSERT_EQ(status_$area_none_free, status);
    ASSERT_EQ(1, alloc_calls);
    ASSERT_EQ(0x60, alloc_last_count);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0, lock_depth);
}

/* Same failure on a remote create (remote_uid != 0): no lock is taken and
 * none is released (0x00E07838). */
TEST(no_free_entry_remote_does_not_unlock)
{
    status_$t status = 0xDEADBEEF;
    uint32_t handle;

    AREA_$FREE_LIST = NULL;
    mock_alloc_result = false;

    handle = area_$internal_create(0x8000, 0, 0xAABBCCDD, 0, 0, false, &status);

    ASSERT_EQ(0, handle);
    ASSERT_EQ(status_$area_none_free, status);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(0, unlock_calls);
}

/* area_$alloc_resources returning true (0xFF) lets the create proceed. */
TEST(alloc_resources_true_continues)
{
    status_$t status = 0xDEADBEEF;
    uint32_t handle;

    AREA_$FREE_LIST = NULL;
    mock_area_table[0].next = NULL;
    mock_alloc_supplies = &mock_area_table[0];
    mock_alloc_result = true;               /* 0xFF, i.e. < 0 */

    handle = area_$internal_create(0x8000, 0, 0, 3, 1, false, &status);

    ASSERT_EQ(1, alloc_calls);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, AREA_HANDLE_TO_ID(handle));
}

/* Entry initialisation: flags word, generation, caller id, ASID threading. */
TEST(entry_initialisation_and_asid_list)
{
    status_$t status = 0xDEADBEEF;
    uint32_t handle;

    seed_free_list(1, 3);                   /* start at table index 1 */
    mock_area_table[1].generation = 5;
    AREA_$NEXT_CALLER_ID = 0x1234;
    AREA_$N_FREE = 3;

    handle = area_$internal_create(0x1, 0x2, 0, 4, 0, false, &status);

    /* virt_size rounds up to 0x8000 */
    ASSERT_EQ(1, resize_calls);
    ASSERT_EQ(0x8000, resize_virt);
    ASSERT_EQ(0x2, resize_commit);
    ASSERT_EQ(0, resize_is_grow);
    ASSERT_EQ(2, resize_area_id);           /* 1-based */

    ASSERT_EQ(AREA_FLAG_ACTIVE | AREA_FLAG_SHARED, mock_area_table[1].flags);
    ASSERT_EQ(6, mock_area_table[1].generation);
    ASSERT_EQ(0x1234, mock_area_table[1].caller_id);
    ASSERT_EQ(0x1235, AREA_$NEXT_CALLER_ID);
    ASSERT_EQ(-1, mock_area_table[1].first_bste);
    ASSERT_EQ(4, mock_area_table[1].owner_asid);
    ASSERT_EQ(2, AREA_$N_FREE);
    ASSERT_EQ(AREA_MAKE_HANDLE(6, 2), handle);

    /* Threaded onto ASID 4's list, with the old head linked back. */
    ASSERT_EQ((uintptr_t)&mock_area_table[1], (uintptr_t)AREA_$ASID_LIST[4]);
    ASSERT_TRUE(mock_area_table[1].prev == NULL);
    ASSERT_EQ(0, lock_depth);
}

/* shared is a Domain boolean: only true (0xFF, tested with bmi) sets
 * AREA_FLAG_REVERSED (bit 1 of the flags word, 0x00E0789A). */
TEST(shared_true_sets_reversed_flag)
{
    status_$t status = 0xDEADBEEF;

    seed_free_list(0, 2);
    (void)area_$internal_create(0x8000, 0, 0, 3, 0, true, &status);
    ASSERT_EQ(AREA_FLAG_ACTIVE | AREA_FLAG_SHARED | AREA_FLAG_REVERSED,
              mock_area_table[0].flags);

    reset_mocks();
    seed_free_list(0, 2);
    (void)area_$internal_create(0x8000, 0, 0, 3, 0, false, &status);
    ASSERT_EQ(AREA_FLAG_ACTIVE | AREA_FLAG_SHARED, mock_area_table[0].flags);
}

/* A remote create is not threaded onto any ASID list and takes no lock. */
TEST(remote_create_skips_lock_and_asid_list)
{
    status_$t status = 0xDEADBEEF;

    seed_free_list(0, 2);
    (void)area_$internal_create(0x8000, 0, 0xAABBCCDD, 0, 0, false, &status);

    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(0, unlock_calls);
    ASSERT_TRUE(AREA_$ASID_LIST[0] == NULL);
    ASSERT_EQ(0xAABBCCDD, mock_area_table[0].remote_uid);
}

/* REM_FILE_$CREATE_AREA failing deletes the half-built area via
 * AREA_$DELETE (0x00E07942) and returns 0. */
TEST(remote_create_failure_deletes_area)
{
    status_$t status = 0xDEADBEEF;
    uint32_t handle;

    seed_free_list(0, 2);
    AREA_$PARTNER.low = 0x00012345;
    rfca_status = 0x000E0004;

    handle = area_$internal_create(0x8000, 0, 0, 3, 1, false, &status);

    ASSERT_EQ(0, handle);
    ASSERT_EQ(0x000E0004, status);
    ASSERT_EQ(1, area_delete_calls);
    ASSERT_EQ(AREA_MAKE_HANDLE(1, 1), area_delete_handle);
    ASSERT_EQ(0, gps_calls);
    ASSERT_EQ(0, internal_delete_calls);
}

/* area_$resize failing calls area_$internal_delete with do_unlink true for
 * a local create and false for a remote one (0x00E0799A). */
TEST(resize_failure_unlink_flag)
{
    status_$t status = 0xDEADBEEF;
    uint32_t handle;

    seed_free_list(0, 2);
    resize_status = 0x00320003;

    handle = area_$internal_create(0x8000, 0, 0, 3, 0, false, &status);
    ASSERT_EQ(0, handle);
    ASSERT_EQ(1, internal_delete_calls);
    ASSERT_TRUE(internal_delete_unlink < 0);
    ASSERT_EQ(1, internal_delete_area_id);

    reset_mocks();
    seed_free_list(0, 2);
    resize_status = 0x00320003;

    handle = area_$internal_create(0x8000, 0, 0xAABBCCDD, 0, 0, false, &status);
    ASSERT_EQ(0, handle);
    ASSERT_EQ(1, internal_delete_calls);
    ASSERT_EQ(0, internal_delete_unlink);
}

/* ==========================================================================
 * AREA_$CREATE
 * ========================================================================== */

/* AREA_$CREATE is a procedure: it reports status only, always creates for
 * PROC1_$AS_ID with remote_uid 0 and alloc_remote 1. */
TEST(area_create_uses_current_asid)
{
    status_$t status = 0xDEADBEEF;

    seed_free_list(0, 2);
    PROC1_$AS_ID = 11;

    AREA_$CREATE(0x8000, 0x400, false, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(11, mock_area_table[0].owner_asid);
    ASSERT_EQ((uintptr_t)&mock_area_table[0], (uintptr_t)AREA_$ASID_LIST[11]);
}

/* ==========================================================================
 * AREA_$CREATE_FROM
 * ========================================================================== */

/* An existing chain entry with a matching UID and caller id is reused. */
TEST(create_from_dedups_on_caller_id)
{
    status_$t status = 0xDEADBEEF;
    uint16_t id;
    uint16_t bucket = (uint16_t)(0xAABBCCDDu % AREA_UID_HASH_BUCKETS);

    mock_area_table[2].remote_uid = 0xAABBCCDD;
    mock_area_table[2].caller_id = 0x555;
    mock_area_table[2].next = NULL;
    AREA_$UID_HASH_POOL[0].first_entry = &mock_area_table[2];
    AREA_$UID_HASH_POOL[0].next = NULL;
    AREA_$UID_HASH[bucket] = &AREA_$UID_HASH_POOL[0];

    id = AREA_$CREATE_FROM(0xAABBCCDD, 0x8000, 0, 0x555, &status);

    ASSERT_EQ(3, id);                        /* table index 2 -> id 3 */
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, AREA_$CR_DUP);
    ASSERT_EQ(0, lock_depth);
}

/* No match: a new area is created and threaded onto a pool record. */
TEST(create_from_allocates_hash_record)
{
    status_$t status = 0xDEADBEEF;
    uint16_t id;
    uint16_t bucket = (uint16_t)(0xAABBCCDDu % AREA_UID_HASH_BUCKETS);

    seed_free_list(0, 2);
    AREA_$UID_HASH_FREE = &AREA_$UID_HASH_POOL[0];
    AREA_$UID_HASH_POOL[0].next = NULL;

    id = AREA_$CREATE_FROM(0xAABBCCDD, 0x8000, 0x200, 0x777, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, id);
    ASSERT_EQ(0, AREA_$CR_DUP);
    ASSERT_TRUE(AREA_$UID_HASH_FREE == NULL);
    ASSERT_EQ((uintptr_t)&AREA_$UID_HASH_POOL[0],
              (uintptr_t)AREA_$UID_HASH[bucket]);
    ASSERT_EQ((uintptr_t)&mock_area_table[0],
              (uintptr_t)AREA_$UID_HASH_POOL[0].first_entry);
    ASSERT_EQ(0x777, mock_area_table[0].caller_id);
    ASSERT_TRUE(mock_area_table[0].prev == NULL);
    ASSERT_EQ(0, lock_depth);
}

/* An existing chain for this UID but a different caller id: the new entry is
 * pushed on the head of that chain and no pool record is consumed. */
TEST(create_from_reuses_existing_chain)
{
    status_$t status = 0xDEADBEEF;
    uint16_t id;
    uint16_t bucket = (uint16_t)(0xAABBCCDDu % AREA_UID_HASH_BUCKETS);

    seed_free_list(0, 2);
    mock_area_table[5].remote_uid = 0xAABBCCDD;
    mock_area_table[5].caller_id = 0x111;
    mock_area_table[5].next = NULL;
    AREA_$UID_HASH_POOL[0].first_entry = &mock_area_table[5];
    AREA_$UID_HASH_POOL[0].next = NULL;
    AREA_$UID_HASH[bucket] = &AREA_$UID_HASH_POOL[0];
    AREA_$UID_HASH_FREE = &AREA_$UID_HASH_POOL[1];

    id = AREA_$CREATE_FROM(0xAABBCCDD, 0x8000, 0, 0x222, &status);

    ASSERT_EQ(1, id);
    ASSERT_EQ(status_$ok, status);
    /* pool untouched */
    ASSERT_EQ((uintptr_t)&AREA_$UID_HASH_POOL[1], (uintptr_t)AREA_$UID_HASH_FREE);
    /* new entry at the head, old entry linked back */
    ASSERT_EQ((uintptr_t)&mock_area_table[0],
              (uintptr_t)AREA_$UID_HASH_POOL[0].first_entry);
    ASSERT_EQ((uintptr_t)&mock_area_table[5],
              (uintptr_t)mock_area_table[0].next);
    ASSERT_EQ((uintptr_t)&mock_area_table[0],
              (uintptr_t)mock_area_table[5].prev);
}

/*
 * ORIGINAL BUG, reproduced deliberately (0x00E07AC4 .. 0x00E07B44).
 *
 * When the hash-record pool is exhausted the area just created is deleted,
 * status_$area_no_uid is reported, and the function returns the LOW WORD OF
 * THE REMOTE UID because D2 was never overwritten with the area id.
 */
TEST(create_from_pool_exhausted_returns_remote_uid_low_word)
{
    status_$t status = 0xDEADBEEF;
    uint16_t id;

    seed_free_list(0, 2);
    AREA_$UID_HASH_FREE = NULL;             /* pool exhausted */

    id = AREA_$CREATE_FROM(0xAABBCCDD, 0x8000, 0, 0x777, &status);

    ASSERT_EQ(status_$area_no_uid, status);
    /* The faithful, buggy result: 0xCCDD, not the area id 1. */
    ASSERT_EQ(0xCCDD, id);
    ASSERT_EQ(1, internal_delete_calls);
    ASSERT_EQ((uintptr_t)&mock_area_table[0], (uintptr_t)internal_delete_entry);
    ASSERT_EQ(1, internal_delete_area_id);
    ASSERT_EQ(0, internal_delete_unlink);   /* clr.w -(SP) at 0x00E07AC4 */
    ASSERT_EQ(0, lock_depth);
}

/* If area_$internal_create fails, the hash table is left alone. */
TEST(create_from_propagates_create_failure)
{
    status_$t status = 0xDEADBEEF;
    uint16_t id;

    AREA_$FREE_LIST = NULL;
    mock_alloc_result = false;
    AREA_$UID_HASH_FREE = &AREA_$UID_HASH_POOL[0];

    id = AREA_$CREATE_FROM(0xAABBCCDD, 0x8000, 0, 0x777, &status);

    ASSERT_EQ(status_$area_none_free, status);
    ASSERT_EQ(0, id);                       /* D0w of a zero handle */
    ASSERT_EQ((uintptr_t)&AREA_$UID_HASH_POOL[0],
              (uintptr_t)AREA_$UID_HASH_FREE);
    ASSERT_EQ(0, lock_depth);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("AREA_$CREATE family tests:\n");

    RUN_TEST(virt_size_zero_asks_partner_for_0x400);
    RUN_TEST(overhead_for_nonzero_sizes);
    RUN_TEST(partner_is_passed_by_address);
    RUN_TEST(no_partner_uses_boot_volx);
    RUN_TEST(no_free_entry_fails_and_unlocks);
    RUN_TEST(no_free_entry_remote_does_not_unlock);
    RUN_TEST(alloc_resources_true_continues);
    RUN_TEST(entry_initialisation_and_asid_list);
    RUN_TEST(shared_true_sets_reversed_flag);
    RUN_TEST(remote_create_skips_lock_and_asid_list);
    RUN_TEST(remote_create_failure_deletes_area);
    RUN_TEST(resize_failure_unlink_flag);
    RUN_TEST(area_create_uses_current_asid);
    RUN_TEST(create_from_dedups_on_caller_id);
    RUN_TEST(create_from_allocates_hash_record);
    RUN_TEST(create_from_reuses_existing_chain);
    RUN_TEST(create_from_pool_exhausted_returns_remote_uid_low_word);
    RUN_TEST(create_from_propagates_create_failure);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
