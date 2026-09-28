/*
 * mst/test/test_wire.c - unit tests for MST_$WIRE (0x00E0E1BC)
 *
 * Under ML lock 0x14 MST_$WIRE asks MMU_$VTOP for the page; a present page
 * is wired with MMAP_$WIRE and returned, a missing one is handed to
 * MST_$TOUCH(va, &status, 1) after the lock is dropped.  The status left in
 * the frame is copied to *status_ret either way.
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

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

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
static status_$t mock_touch_status;

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
    *status_ret = mock_touch_status;
    return mock_touch_result;
}

#include "mst/wire.c"

/* ------------------------------------------------------------------ */

static void reset_state(void)
{
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
    mock_touch_status = status_$ok;
}

/* Present page: wired under the lock, its page returned, status_$ok
 * copied out (0x00E0E1C4..0x00E0E206, 0x00E0E228). */
static void test_present_page_is_wired_and_returned(void)
{
    status_$t status = 0x12345678;
    uint32_t r = MST_$WIRE(0x00ABC123, &status);

    ASSERT_EQ(0x1234, r);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x00ABC123, mock_vtop_va);
    ASSERT_EQ(1, mock_wire_calls);
    ASSERT_EQ(0x1234, mock_wire_vpn);
    ASSERT_EQ(1, mock_wire_lock_depth);
    ASSERT_EQ(0, mock_touch_calls);
    ASSERT_EQ(1, mock_lock_calls);
    ASSERT_EQ(1, mock_unlock_calls);
    ASSERT_EQ(MST_LOCK_MMU, mock_lock_id);
    ASSERT_EQ(0, mock_lock_depth);
}

/* Missing page: unlock first, then MST_$TOUCH(va, &status, 1); its result
 * and its status come back (0x00E0E208..0x00E0E228). */
static void test_missing_page_goes_to_touch_wired(void)
{
    status_$t status = 0x12345678;
    uint32_t r;

    mock_vtop_status = status_$reference_to_illegal_address;
    mock_touch_status = status_$mst_guard_fault;
    r = MST_$WIRE(0x00ABC123, &status);

    ASSERT_EQ(0x5678, r);
    ASSERT_EQ(status_$mst_guard_fault, status);
    ASSERT_EQ(1, mock_touch_calls);
    ASSERT_EQ(0x00ABC123, mock_touch_va);
    ASSERT_EQ(1, mock_touch_wire);
    ASSERT_EQ(0, mock_touch_lock_depth);
    ASSERT_EQ(0, mock_wire_calls);
    ASSERT_EQ(1, mock_unlock_calls);
}

int main(void)
{
    printf("MST_$WIRE tests:\n");
    RUN_TEST(present_page_is_wired_and_returned);
    RUN_TEST(missing_page_goes_to_touch_wired);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
