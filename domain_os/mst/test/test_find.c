/*
 * mst/test/test_find.c - unit tests for MST_$FIND (0x00E0E11E)
 *
 * Under ML lock 0x14 MST_$FIND asks MMU_$VTOP for the page; a present page
 * is optionally wired (flag bit 1) and returned, a missing one is handed to
 * MST_$TOUCH after the lock is dropped.  Flag bits 0 and 2 crash the system
 * with the cell at 0x00E0E1B8 (00 04 00 05) and then carry on.
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
#include "misc/misc.h"

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

static int       mock_crash_calls;
static status_$t mock_crash_status;
static int       mock_lock_depth;      /* +1 on lock, -1 on unlock */
static int       mock_lock_calls;
static int       mock_unlock_calls;
static int16_t   mock_lock_id;
static uint32_t  mock_vtop_va;
static uint32_t  mock_vtop_ppn;
static status_$t mock_vtop_status;
static int       mock_wire_calls;
static uint32_t  mock_wire_vpn;
static int       mock_wire_lock_depth; /* lock depth when MMAP_$WIRE ran */
static int       mock_touch_calls;
static uint32_t  mock_touch_va;
static int16_t   mock_touch_wire;
static int       mock_touch_lock_depth;
static uint32_t  mock_touch_result;

void CRASH_SYSTEM(const status_$t *status_p)
{
    mock_crash_calls++;
    mock_crash_status = *status_p;
}

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

uint32_t MMU_$VTOP(uint32_t va, status_$t *status)
{
    mock_vtop_va = va;
    *status = mock_vtop_status;
    return mock_vtop_ppn;
}

void MMAP_$WIRE(uint32_t vpn)
{
    mock_wire_calls++;
    mock_wire_vpn = vpn;
    mock_wire_lock_depth = mock_lock_depth;
}

uint32_t MST_$TOUCH(uint32_t virtual_addr, status_$t *status_ret, int16_t wire_flag)
{
    mock_touch_calls++;
    mock_touch_va = virtual_addr;
    mock_touch_wire = wire_flag;
    mock_touch_lock_depth = mock_lock_depth;
    *status_ret = status_$ok;
    return mock_touch_result;
}

#include "mst/find.c"

/* ------------------------------------------------------------------ */

static void reset_state(void)
{
    mock_crash_calls = 0;
    mock_crash_status = 0;
    mock_lock_depth = 0;
    mock_lock_calls = 0;
    mock_unlock_calls = 0;
    mock_lock_id = -1;
    mock_vtop_va = 0;
    mock_vtop_ppn = 0x1234;
    mock_vtop_status = status_$ok;
    mock_wire_calls = 0;
    mock_wire_vpn = 0;
    mock_wire_lock_depth = -1;
    mock_touch_calls = 0;
    mock_touch_va = 0;
    mock_touch_wire = -1;
    mock_touch_lock_depth = -1;
    mock_touch_result = 0x5678;
}

/* Present page, no wire bit: MMU_$VTOP's page comes back, nothing wired,
 * lock 0x14 taken and released (0x00E0E13C..0x00E0E180). */
static void test_present_page_is_returned(void)
{
    uint32_t r = MST_$FIND(0x00ABC123, 0);

    ASSERT_EQ(0x1234, r);
    ASSERT_EQ(0x00ABC123, mock_vtop_va);
    ASSERT_EQ(0, mock_wire_calls);
    ASSERT_EQ(0, mock_touch_calls);
    ASSERT_EQ(0, mock_crash_calls);
    ASSERT_EQ(1, mock_lock_calls);
    ASSERT_EQ(1, mock_unlock_calls);
    ASSERT_EQ(MST_LOCK_MMU, mock_lock_id);
    ASSERT_EQ(0, mock_lock_depth);
}

/* Present page with bit 1 set: MMAP_$WIRE(ppn) runs while the lock is
 * still held (0x00E0E162..0x00E0E170). */
static void test_present_page_is_wired_under_lock(void)
{
    uint32_t r = MST_$FIND(0x00ABC123, 2);

    ASSERT_EQ(0x1234, r);
    ASSERT_EQ(1, mock_wire_calls);
    ASSERT_EQ(0x1234, mock_wire_vpn);
    ASSERT_EQ(1, mock_wire_lock_depth);
    ASSERT_EQ(0, mock_touch_calls);
}

/* Missing page: the lock is dropped first, then MST_$TOUCH(va, &status, 0)
 * and its result is returned (0x00E0E182..0x00E0E1AA). */
static void test_missing_page_goes_to_touch(void)
{
    uint32_t r;

    mock_vtop_status = status_$reference_to_illegal_address;
    r = MST_$FIND(0x00ABC123, 0);

    ASSERT_EQ(0x5678, r);
    ASSERT_EQ(1, mock_touch_calls);
    ASSERT_EQ(0x00ABC123, mock_touch_va);
    ASSERT_EQ(0, mock_touch_wire);
    ASSERT_EQ(0, mock_touch_lock_depth);
    ASSERT_EQ(0, mock_wire_calls);
    ASSERT_EQ(1, mock_unlock_calls);
}

/* Missing page with bit 1: MST_$TOUCH gets the word 1 (0x00E0E196). */
static void test_missing_page_touch_wire_flag(void)
{
    mock_vtop_status = status_$reference_to_illegal_address;
    (void)MST_$FIND(0x00ABC123, 2);

    ASSERT_EQ(1, mock_touch_calls);
    ASSERT_EQ(1, mock_touch_wire);
}

/* Bits 0 and 2 crash with 0x00040005 and the lookup then proceeds
 * anyway (0x00E0E12A..0x00E0E13A falls into 0x00E0E13C). */
static void test_bad_flag_bits_crash_then_continue(void)
{
    uint32_t r = MST_$FIND(0x00ABC123, 1);

    ASSERT_EQ(1, mock_crash_calls);
    ASSERT_EQ(0x00040005, mock_crash_status);
    ASSERT_EQ(0x1234, r);
    ASSERT_EQ(1, mock_lock_calls);

    reset_state();
    r = MST_$FIND(0x00ABC123, 4);
    ASSERT_EQ(1, mock_crash_calls);
    ASSERT_EQ(0x1234, r);

    reset_state();
    r = MST_$FIND(0x00ABC123, 0xFFFA);       /* bits 0 and 2 clear */
    ASSERT_EQ(0, mock_crash_calls);
    ASSERT_EQ(1, mock_wire_calls);
}

int main(void)
{
    printf("MST_$FIND tests:\n");
    RUN_TEST(present_page_is_returned);
    RUN_TEST(present_page_is_wired_under_lock);
    RUN_TEST(missing_page_goes_to_touch);
    RUN_TEST(missing_page_touch_wire_flag);
    RUN_TEST(bad_flag_bits_crash_then_continue);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
