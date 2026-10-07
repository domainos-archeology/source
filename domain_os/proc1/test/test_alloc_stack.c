/*
 * Tests for PROC1_$ALLOC_STACK (0x00E1501A).
 *
 * Includes the real proc1/alloc_stack.c and drives it through mocked
 * ML_$LOCK / ML_$UNLOCK / WP_$CALLOC / MMU_$INSTALL.  The stack region
 * cells (STACK_LOW_WATER, STACK_HIGH_WATER, STACK_FREE_LIST) are defined
 * here; virtual addresses are mapped onto a host arena through
 * ARCH_HOST_VA_BASE so the free-list link can really be dereferenced.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "base/base.h"
#include "proc1/proc1.h"
#include "ml/ml.h"
#include "mmu/mmu.h"
#include "wp/wp.h"

int __host_intr_disable_count = 0;

/* ------------------------------------------------------------------ */
/* Module cells                                                        */
/* ------------------------------------------------------------------ */

MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);

/* ------------------------------------------------------------------ */
/* Mocks                                                               */
/* ------------------------------------------------------------------ */

static int n_lock, n_unlock;
static int16_t last_lock_id, last_unlock_id;

void ML_$LOCK(int16_t id)   { n_lock++;   last_lock_id = id; }
void ML_$UNLOCK(int16_t id) { n_unlock++; last_unlock_id = id; }

#define MAX_CALLS 16
static int n_calloc;
static int calloc_fail_on;              /* 1-based call number, 0 = never */
static status_$t calloc_fail_status;
static uint32_t calloc_page_base;

void WP_$CALLOC(uint32_t *ppn_out, status_$t *status)
{
    n_calloc++;
    if (calloc_fail_on != 0 && n_calloc == calloc_fail_on) {
        *status = calloc_fail_status;
        return;
    }
    *ppn_out = calloc_page_base + (uint32_t)n_calloc;
    *status = status_$ok;
}

static int n_install;
static uint32_t install_ppn[MAX_CALLS];
static uint32_t install_va[MAX_CALLS];
static uint32_t install_flags[MAX_CALLS];

void (MMU_$INSTALL)(uint32_t ppn, uint32_t va, uint32_t flags)
{
    if (n_install < MAX_CALLS) {
        install_ppn[n_install] = ppn;
        install_va[n_install] = va;
        install_flags[n_install] = flags;
    }
    n_install++;
}

/* ------------------------------------------------------------------ */
/* Code under test                                                     */
/* ------------------------------------------------------------------ */

#include "../alloc_stack.c"

/* ------------------------------------------------------------------ */

static int tests_run, tests_failed;

#define ASSERT_EQ(a, b) do {                                                  \
    unsigned long long _a = (unsigned long long)(a);                          \
    unsigned long long _b = (unsigned long long)(b);                          \
    if (_a != _b) {                                                           \
        printf("  FAIL %s:%d: %s == %s (0x%llx != 0x%llx)\n", __FILE__,       \
               __LINE__, #a, #b, _a, _b);                                     \
        tests_failed++;                                                       \
    }                                                                         \
} while (0)

#define RUN_TEST(fn) do { tests_run++; reset(); fn(); } while (0)

#define LOW0   0x00D00000u
#define HIGH0  0x00D50000u

static void reset(void)
{
    ARCH_HOST_VA_BASE = 0;
    PROC1_$DATA.stack_free_list = 0;
    PROC1_$DATA.stack_high_water = HIGH0;
    PROC1_$DATA.stack_low_water = LOW0;
    n_lock = n_unlock = 0;
    last_lock_id = last_unlock_id = -1;
    n_calloc = 0;
    calloc_fail_on = 0;
    calloc_fail_status = 0;
    calloc_page_base = 0x1000;
    n_install = 0;
    memset(install_ppn, 0, sizeof(install_ppn));
    memset(install_va, 0, sizeof(install_va));
    memset(install_flags, 0, sizeof(install_flags));
}

/*
 * 0x00E15040..0x00E15068: a 0x801-byte request rounds to 0xC00, is a
 * "small" stack, grows the low region and maps three pages just below
 * the returned top with flags 0x16.
 */
static void test_small_stack_rounds_and_maps(void)
{
    status_$t st = 0x12345678;
    void *top = PROC1_$ALLOC_STACK(0x801, &st);

    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(ARCH_PTR_TO_VA(top), LOW0 + 0xC00 + 0x400);
    ASSERT_EQ(n_calloc, 3);
    ASSERT_EQ(n_install, 3);
    ASSERT_EQ(install_va[0], LOW0 + 0xC00 + 0x400 - 0xC00);
    ASSERT_EQ(install_va[1], LOW0 + 0xC00 + 0x400 - 0x800);
    ASSERT_EQ(install_va[2], LOW0 + 0xC00 + 0x400 - 0x400);
    ASSERT_EQ(install_ppn[0], 0x1001);
    ASSERT_EQ(install_ppn[2], 0x1003);
    ASSERT_EQ(install_flags[0], 0x16);
    ASSERT_EQ(install_flags[2], 0x16);
    /* 0x00E150F4: the low-water mark becomes the new top */
    ASSERT_EQ(PROC1_$DATA.stack_low_water, LOW0 + 0x1000);
    ASSERT_EQ(PROC1_$DATA.stack_high_water, HIGH0);
    /* 0x00E15034 / 0x00E15104: lock id 0xB both ways */
    ASSERT_EQ(n_lock, 1);
    ASSERT_EQ(n_unlock, 1);
    ASSERT_EQ(last_lock_id, PROC1_CREATE_LOCK_ID);
    ASSERT_EQ(last_unlock_id, PROC1_CREATE_LOCK_ID);
}

/* 0x00E15040: a size that is already a multiple of 0x400 is not bumped */
static void test_exact_multiple_not_bumped(void)
{
    status_$t st;
    void *top = PROC1_$ALLOC_STACK(0x400, &st);

    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(ARCH_PTR_TO_VA(top), LOW0 + 0x400 + 0x400);
    ASSERT_EQ(n_install, 1);
}

/*
 * 0x00E15064 / 0x00E1506A: a small stack that would cross the high-water
 * mark fails before any page is allocated; the result cell was already
 * written at 0x00E15060 so the failed top is what comes back.
 */
static void test_small_stack_no_room(void)
{
    status_$t st;
    void *top;

    PROC1_$DATA.stack_low_water = HIGH0 - 0x800;
    top = PROC1_$ALLOC_STACK(0x800, &st);

    ASSERT_EQ(st, status_$no_stack_space_is_available);
    ASSERT_EQ(ARCH_PTR_TO_VA(top), HIGH0 - 0x800 + 0x800 + 0x400);
    ASSERT_EQ(n_calloc, 0);
    ASSERT_EQ(n_install, 0);
    ASSERT_EQ(PROC1_$DATA.stack_low_water, HIGH0 - 0x800);
    ASSERT_EQ(n_unlock, 1);
}

/* 0x00E15064: exactly reaching the high-water mark is allowed (bls) */
static void test_small_stack_exactly_fits(void)
{
    status_$t st;
    void *top;

    PROC1_$DATA.stack_low_water = HIGH0 - 0xC00;
    top = PROC1_$ALLOC_STACK(0x800, &st);

    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(ARCH_PTR_TO_VA(top), HIGH0);
    ASSERT_EQ(n_install, 2);
}

/*
 * 0x00E1506C..0x00E1508A: a 4KB request with a non-empty free list pops
 * the first free stack, returns the longword past its link and maps
 * nothing.
 */
static void test_large_stack_from_free_list(void)
{
    static uint8_t arena[0x400];
    status_$t st;
    void *top;
    uint32_t *first;

    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    PROC1_$DATA.stack_high_water = HIGH0;
    PROC1_$DATA.stack_low_water = LOW0;

    first = (uint32_t *)(arena + 0x100);
    *first = 0x200;                     /* link to the "next" free stack */
    PROC1_$DATA.stack_free_list = ARCH_PTR_TO_VA(first);

    top = PROC1_$ALLOC_STACK(0x1000, &st);

    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(ARCH_PTR_TO_VA(top), 0x104);
    ASSERT_EQ(PROC1_$DATA.stack_free_list, 0x200);
    ASSERT_EQ(n_calloc, 0);
    ASSERT_EQ(n_install, 0);
    ASSERT_EQ(PROC1_$DATA.stack_high_water, HIGH0);
    ASSERT_EQ(PROC1_$DATA.stack_low_water, LOW0);
    ASSERT_EQ(n_unlock, 1);
}

/* 0x00E15072: the free list is only consulted for exactly 0x1000 bytes */
static void test_large_stack_ignores_free_list_when_not_4k(void)
{
    static uint8_t arena[0x400];
    status_$t st;
    void *top;

    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    PROC1_$DATA.stack_high_water = HIGH0;
    PROC1_$DATA.stack_low_water = LOW0;
    PROC1_$DATA.stack_free_list = ARCH_PTR_TO_VA(arena + 0x100);

    top = PROC1_$ALLOC_STACK(0x2000, &st);

    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(ARCH_PTR_TO_VA(top), HIGH0);
    ASSERT_EQ(PROC1_$DATA.stack_free_list, 0x100);
    ASSERT_EQ(n_install, 8);
}

/*
 * 0x00E1508C..0x00E150A8: a large stack grows the high region downward;
 * the returned top is the old high-water mark and the new mark is
 * old - size - 0x400.
 */
static void test_large_stack_grows_down(void)
{
    status_$t st;
    void *top = PROC1_$ALLOC_STACK(0x1000, &st);

    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(ARCH_PTR_TO_VA(top), HIGH0);
    ASSERT_EQ(n_calloc, 4);
    ASSERT_EQ(n_install, 4);
    ASSERT_EQ(install_va[0], HIGH0 - 0x1000);
    ASSERT_EQ(install_va[3], HIGH0 - 0x400);
    /* 0x00E150FC */
    ASSERT_EQ(PROC1_$DATA.stack_high_water, HIGH0 - 0x1000 - 0x400);
    ASSERT_EQ(PROC1_$DATA.stack_low_water, LOW0);
}

/* 0x00E150A2 / 0x00E150A6: crossing the low-water mark fails (bcs) */
static void test_large_stack_no_room(void)
{
    status_$t st;

    PROC1_$DATA.stack_low_water = HIGH0 - 0x1000;
    (void)PROC1_$ALLOC_STACK(0x1000, &st);

    ASSERT_EQ(st, status_$no_stack_space_is_available);
    ASSERT_EQ(n_calloc, 0);
    ASSERT_EQ(PROC1_$DATA.stack_high_water, HIGH0);
    ASSERT_EQ(n_unlock, 1);
}

/* 0x00E150A2: landing exactly on the low-water mark is allowed */
static void test_large_stack_exactly_fits(void)
{
    status_$t st;

    PROC1_$DATA.stack_low_water = HIGH0 - 0x1400;
    (void)PROC1_$ALLOC_STACK(0x1000, &st);

    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(PROC1_$DATA.stack_high_water, HIGH0 - 0x1400);
}

/*
 * 0x00E150C0 / 0x00E150C4: a WP_$CALLOC failure part-way through stops
 * the loop, replaces WP's status with no_stack_space and leaves the
 * water marks alone; the pages already mapped stay mapped.
 */
static void test_calloc_failure_overwrites_status(void)
{
    status_$t st;
    void *top;

    calloc_fail_on = 2;
    calloc_fail_status = 0x00040001;
    top = PROC1_$ALLOC_STACK(0xC00, &st);

    ASSERT_EQ(st, status_$no_stack_space_is_available);
    ASSERT_EQ(ARCH_PTR_TO_VA(top), LOW0 + 0xC00 + 0x400);
    ASSERT_EQ(n_calloc, 2);
    ASSERT_EQ(n_install, 1);
    ASSERT_EQ(PROC1_$DATA.stack_low_water, LOW0);
    ASSERT_EQ(PROC1_$DATA.stack_high_water, HIGH0);
    ASSERT_EQ(n_unlock, 1);
}

/* 0x00E150AE: a zero-length request maps nothing but still moves the mark */
static void test_zero_size(void)
{
    status_$t st;
    void *top = PROC1_$ALLOC_STACK(0, &st);

    ASSERT_EQ(st, status_$ok);
    ASSERT_EQ(ARCH_PTR_TO_VA(top), LOW0 + 0x400);
    ASSERT_EQ(n_calloc, 0);
    ASSERT_EQ(PROC1_$DATA.stack_low_water, LOW0 + 0x400);
}

int main(void)
{
    RUN_TEST(test_small_stack_rounds_and_maps);
    RUN_TEST(test_exact_multiple_not_bumped);
    RUN_TEST(test_small_stack_no_room);
    RUN_TEST(test_small_stack_exactly_fits);
    RUN_TEST(test_large_stack_from_free_list);
    RUN_TEST(test_large_stack_ignores_free_list_when_not_4k);
    RUN_TEST(test_large_stack_grows_down);
    RUN_TEST(test_large_stack_no_room);
    RUN_TEST(test_large_stack_exactly_fits);
    RUN_TEST(test_calloc_failure_overwrites_status);
    RUN_TEST(test_zero_size);

    printf("test_alloc_stack: %d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
