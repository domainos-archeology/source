/*
 * peb/test/test_proc_cleanup.c - PEB_$PROC_CLEANUP (0x00E752BC) and
 * peb_$cleanup_internal (0x00E70954)
 *
 * Pins the gate (~installed | mmu_installed, bit 7), the owner check, the
 * PEB_CTL busy poll that ends in CRASH_SYSTEM, the MMU_$REMOVE(0x2D) and
 * owner clear, and the 7-longword zero of the current AS's FP record.
 */

#include <stdio.h>
#include <string.h>

int __host_intr_disable_count = 0;

#include "peb/peb_internal.h"

MODULE_DATA_DEFINE(peb_globals_t, PEB_$INFO, 0x00E24C78);
uint16_t          PROC1_$CURRENT;
uint16_t          PROC1_$AS_ID;
peb_fp_state_t    PEB_$WIRED_DATA_START[PEB_MAX_PROCESSES];
status_$t         PEB_FPU_Is_Hung_Err = status_$peb_fpu_is_hung | 0x80000000;

static uint16_t   host_peb_ctl;
/* The PEB control page stands in for SAU2_PEB_CTL (arch/m68k/sau2/hw.h). */
#define SAU2_PEB_CTL peb_ctl_reg
static volatile uint16_t *peb_ctl_reg = &host_peb_ctl;

static int remove_calls; static uint32_t remove_ppn;
static int crash_calls; static const status_$t *crash_arg;
static int busy_polls_left;   /* -1: busy forever */

void MMU_$REMOVE(uint32_t ppn) { remove_calls++; remove_ppn = ppn; }
void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_calls++; crash_arg = status_p;
    host_peb_ctl = 0;            /* the mock returns; make the board idle */
}

#include "../proc_cleanup.c"

static int tests_passed = 0;
static int tests_failed = 0;
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { printf("  Running %-44s ", #name); test_##name(); \
    tests_passed++; printf("PASSED\n"); } while (0)
#define ASSERT_EQ(expected, actual) do { \
    unsigned long long _e = (unsigned long long)(expected); \
    unsigned long long _a = (unsigned long long)(actual); \
    if (_e != _a) { printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n", \
        _e, _a, __LINE__); tests_failed++; return; } } while (0)

static void reset(void)
{
    memset(&PEB_$INFO, 0, sizeof PEB_$INFO);
    memset(PEB_$WIRED_DATA_START, 0xAB, sizeof PEB_$WIRED_DATA_START);
    host_peb_ctl = 0;
    remove_calls = crash_calls = 0;
    busy_polls_left = 0;
    PROC1_$CURRENT = 7;
    PROC1_$AS_ID = 3;
}

static int record_is_zero(int asid)
{
    static const peb_fp_state_t z;
    return memcmp(&PEB_$WIRED_DATA_START[asid], &z, sizeof z) == 0;
}

TEST(not_installed_does_nothing)
{
    reset();
    PEB_$INSTALLED = 0;
    PEB_$OWNER_PID = 7;
    PEB_$PROC_CLEANUP();
    ASSERT_EQ(0, remove_calls);
    ASSERT_EQ(0, record_is_zero(3));
}

TEST(mmu_installed_does_nothing)
{
    reset();
    PEB_$INSTALLED = -1;
    PEB_$MMU_INSTALLED = -1;
    PEB_$OWNER_PID = 7;
    PEB_$PROC_CLEANUP();
    ASSERT_EQ(0, remove_calls);
    ASSERT_EQ(0, record_is_zero(3));
}

TEST(owner_idle_removes_page_and_zeroes)
{
    reset();
    PEB_$INSTALLED = -1;
    PEB_$OWNER_PID = 7;
    PEB_$PROC_CLEANUP();
    ASSERT_EQ(1, remove_calls);
    ASSERT_EQ(0x2D, remove_ppn);
    ASSERT_EQ(0, PEB_$OWNER_PID);
    ASSERT_EQ(0, crash_calls);
    ASSERT_EQ(1, record_is_zero(3));
    ASSERT_EQ(0, record_is_zero(2));   /* neighbours untouched */
    ASSERT_EQ(0, record_is_zero(4));
    ASSERT_EQ(0, __host_intr_disable_count);
}

TEST(not_owner_only_zeroes)
{
    reset();
    PEB_$INSTALLED = -1;
    PEB_$OWNER_PID = 9;
    host_peb_ctl = 0x8000;
    PEB_$PROC_CLEANUP();
    ASSERT_EQ(0, remove_calls);
    ASSERT_EQ(9, PEB_$OWNER_PID);
    ASSERT_EQ(1, record_is_zero(3));
}

TEST(owner_busy_forever_crashes_then_removes)
{
    reset();
    PEB_$INSTALLED = -1;
    PEB_$OWNER_PID = 7;
    host_peb_ctl = 0x8000;
    PEB_$PROC_CLEANUP();
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ((unsigned long long)(uintptr_t)&PEB_FPU_Is_Hung_Err,
              (unsigned long long)(uintptr_t)crash_arg);
    ASSERT_EQ(1, remove_calls);
    ASSERT_EQ(0, PEB_$OWNER_PID);
    ASSERT_EQ(1, record_is_zero(3));
}

int main(void)
{
    printf("PEB_$PROC_CLEANUP tests\n");
    RUN_TEST(not_installed_does_nothing);
    RUN_TEST(mmu_installed_does_nothing);
    RUN_TEST(owner_idle_removes_page_and_zeroes);
    RUN_TEST(not_owner_only_zeroes);
    RUN_TEST(owner_busy_forever_crashes_then_removes);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
