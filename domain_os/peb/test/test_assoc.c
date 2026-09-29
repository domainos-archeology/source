/*
 * peb/test/test_assoc.c - PEB_$ASSOC (0x00E5AD38) / PEB_$DISSOC (0x00E5ADA4)
 */

#include <stdio.h>
#include <string.h>

int __host_intr_disable_count = 0;

#include "peb/peb_internal.h"

MODULE_DATA_DEFINE(peb_globals_t, PEB_$INFO, 0x00E24C78);
uint16_t      PROC1_$CURRENT;
uint16_t      PROC1_$AS_ID;

static int install_calls; static uint32_t inst_ppn[4], inst_va[4], inst_flags[4];
static int remove_calls; static uint32_t remove_ppn;

void MMU_$INSTALL(uint32_t ppn, uint32_t va, uint32_t flags)
{
    if (install_calls < 4) { inst_ppn[install_calls] = ppn; inst_va[install_calls] = va;
                             inst_flags[install_calls] = flags; }
    install_calls++;
}
void MMU_$REMOVE(uint32_t ppn) { remove_calls++; remove_ppn = ppn; }

#include "../assoc.c"

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

TEST(first_assoc_installs_three_pages)
{
    memset(&PEB_$INFO, 0, sizeof PEB_$INFO);
    install_calls = 0;
    PROC1_$CURRENT = 0x1234; PROC1_$AS_ID = 0x0009;
    PEB_$ASSOC();
    ASSERT_EQ(0x1234, PEB_$OWNER_PID);
    ASSERT_EQ(0x0009, PEB_$OWNER_ASID);
    ASSERT_EQ(0xFF, (uint8_t)PEB_$MMU_INSTALLED);
    ASSERT_EQ(3, install_calls);
    ASSERT_EQ(0x2E, inst_ppn[0]); ASSERT_EQ(0xFF7800, inst_va[0]); ASSERT_EQ(6, inst_flags[0]);
    ASSERT_EQ(0x2C, inst_ppn[1]); ASSERT_EQ(0xFF7000, inst_va[1]); ASSERT_EQ(6, inst_flags[1]);
    ASSERT_EQ(0x2D, inst_ppn[2]); ASSERT_EQ(0xFF7400, inst_va[2]); ASSERT_EQ(6, inst_flags[2]);
}

TEST(second_assoc_only_records_owner)
{
    memset(&PEB_$INFO, 0, sizeof PEB_$INFO);
    PEB_$MMU_INSTALLED = -1;
    install_calls = 0;
    PROC1_$CURRENT = 5; PROC1_$AS_ID = 6;
    PEB_$ASSOC();
    ASSERT_EQ(5, PEB_$OWNER_PID);
    ASSERT_EQ(6, PEB_$OWNER_ASID);
    ASSERT_EQ(0, install_calls);
}

TEST(dissoc_removes_mirror_and_clears)
{
    memset(&PEB_$INFO, 0, sizeof PEB_$INFO);
    PEB_$MMU_INSTALLED = -1; PEB_$OWNER_PID = 5; PEB_$OWNER_ASID = 6;
    remove_calls = 0;
    PEB_$DISSOC();
    ASSERT_EQ(1, remove_calls);
    ASSERT_EQ(0x2D, remove_ppn);
    ASSERT_EQ(0, PEB_$OWNER_PID);
    ASSERT_EQ(6, PEB_$OWNER_ASID);     /* not touched by DISSOC */
    ASSERT_EQ(0, (uint8_t)PEB_$MMU_INSTALLED);
}

int main(void)
{
    printf("PEB_$ASSOC / PEB_$DISSOC tests\n");
    RUN_TEST(first_assoc_installs_three_pages);
    RUN_TEST(second_assoc_only_records_owner);
    RUN_TEST(dissoc_removes_mirror_and_clears);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
