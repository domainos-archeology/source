/*
 * ast/test/test_invalidate_page.c - Unit tests for AST_$INVALIDATE_PAGE
 *                                    (0x00E00F16)
 *
 * The test #includes ast/invalidate_page.c directly.  It pins:
 *
 *   - a wired entry (bit 29) has its mapping removed and the bit cleared;
 *   - bit 30 is cleared, the top nine bits kept, the low 23 replaced by
 *     the MMAPE's disk address;
 *   - MMAP_$FREE_REMOVE gets the MMAPE and the ppn; the ASTE page count
 *     goes down by one (a byte).
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define uid_t ast_uid_t

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

static void reset_state(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %-48s", #name);                                         \
    current_failed = 0;                                                       \
    reset_state();                                                            \
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

#include "ast/invalidate_page.c"

#define TEST_N_FRAMES 0x400
static mmape_t test_mmapes[TEST_N_FRAMES];
mmape_t *mmap_mmape_base = test_mmapes;

static int remove_calls;
static uint32_t remove_ppn;
void MMU_$REMOVE(uint32_t ppn) { remove_calls++; remove_ppn = ppn; }

static int free_calls;
static mmape_t *free_page;
static uint32_t free_vpn;
void MMAP_$FREE_REMOVE(mmape_t *page, uint32_t vpn)
{
    free_calls++; free_page = page; free_vpn = vpn;
}

static void reset_state(void)
{
    memset(test_mmapes, 0, sizeof(test_mmapes));
    remove_calls = 0; remove_ppn = 0;
    free_calls = 0; free_page = NULL; free_vpn = 0;
}

TEST(wired_page)
{
    aste_t aste;
    uint32_t entry = SEGMAP_VALID | SEGMAP_WIRED | 0x00400000u | 0x0321;

    memset(&aste, 0, sizeof(aste));
    aste.page_count = 3;
    test_mmapes[0x321].disk_addr = 0x00123456;

    AST_$INVALIDATE_PAGE(&aste, &entry, 0x321);

    ASSERT_EQ(1, remove_calls);
    ASSERT_EQ(0x321, remove_ppn);
    /* bits 31..23 kept (here: none set), bit 22 dropped by the mask */
    ASSERT_EQ(0x00123456u, entry);
    ASSERT_EQ(1, free_calls);
    ASSERT_EQ((uintptr_t)&test_mmapes[0x321], (uintptr_t)free_page);
    ASSERT_EQ(0x321, free_vpn);
    ASSERT_EQ(2, aste.page_count);
}

TEST(unwired_page_keeps_top_bits)
{
    aste_t aste;
    uint32_t entry = SEGMAP_IN_TRANS | SEGMAP_VALID | 0x0210;

    memset(&aste, 0, sizeof(aste));
    aste.page_count = 0;                 /* wraps to 0xFF */
    test_mmapes[0x210].disk_addr = 0x00ABCDEF;

    AST_$INVALIDATE_PAGE(&aste, &entry, 0x210);

    ASSERT_EQ(0, remove_calls);
    ASSERT_EQ(SEGMAP_IN_TRANS | 0x00ABCDEFu, entry);
    ASSERT_EQ(0xFF, aste.page_count);
}

int main(void)
{
    printf("test_invalidate_page (AST_$INVALIDATE_PAGE 0x00E00F16)\n");

    RUN_TEST(wired_page);
    RUN_TEST(unwired_page_keeps_top_bits);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
