/*
 * misc/test/test_zero_page.c - ZERO_PAGE (0x00E00EB0): install at the zero
 * window, clear exactly 256 longwords, remove.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "mmu/mmu.h"
#include "ast/ast.h"

/* the window and one guard longword after it */
static uint32_t window[257];
uint32_t AST_$ZERO_BUFF[256];

static char trace[8];
static int ntrace;
static uint32_t inst_ppn, inst_va, inst_flags, rem_ppn;
static int filled_at_install;

void (MMU_$INSTALL)(uint32_t ppn, uint32_t va, uint32_t flags)
{
    trace[ntrace++] = 'I';
    inst_ppn = ppn; inst_va = va; inst_flags = flags;
    filled_at_install = (AST_$ZERO_BUFF[0] == 0xDEADBEEF);
}
void MMU_$REMOVE(uint32_t ppn)
{
    trace[ntrace++] = 'R';
    rem_ppn = ppn;
}

#include "../zero_page.c"

TEST(clears_window_between_install_and_remove)
{
    int i, nonzero = 0;
    (void)window;
    for (i = 0; i < 256; i++) AST_$ZERO_BUFF[i] = 0xDEADBEEF;
    ZERO_PAGE(0x1234);
    ASSERT_EQ(0, strcmp(trace, "IR"));
    ASSERT_EQ(1, filled_at_install);
    ASSERT_EQ(0x1234, inst_ppn);
    ASSERT_EQ(ARCH_PTR_TO_VA(AST_$ZERO_BUFF), inst_va);
    ASSERT_EQ(0x16, inst_flags);
    ASSERT_EQ(0x1234, rem_ppn);
    for (i = 0; i < 256; i++) nonzero |= (AST_$ZERO_BUFF[i] != 0);
    ASSERT_EQ(0, nonzero);
}

int main(void)
{
    printf("ZERO_PAGE\n");
    RUN_TEST(clears_window_between_install_and_remove);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
