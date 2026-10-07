/*
 * mst/test/test_init_table_page.c - Unit tests for mst_$init_table_page
 *
 * Bead source-ebo.  Exercises the real function (#included at the bottom)
 * with mocked WP_$CALLOC and MMU_$INSTALL and checks:
 *   - the page-alignment mask is the WORD mask "andi.w #-0x400" applied to
 *     the low half only, i.e. va & 0xFFFFFC00;
 *   - MMU_$INSTALL is called with (ppn, aligned_va, 0x16);
 *   - exactly 256 longwords (0x400 bytes) are zeroed starting at the
 *     aligned address ("move.w #0xff,D0w / clr.l (A2)+ / dbf");
 *   - the PPN from WP_$CALLOC is returned;
 *   - a failing WP_$CALLOC status is NOT checked (the original never tests
 *     it), so the install and the zero-fill still happen.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_mocks(); \
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

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    Assertion failed at line %d: %s\n", __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "mst/mst_internal.h"
#include "mmu/mmu.h"
#include "wp/wp.h"

/*
 * Simulated page-table region.  The real base is 0xEF6000; the test hands
 * the function addresses inside this buffer instead.
 */
static uint32_t page_store[0x400] __attribute__((aligned(0x400)));
                                       /* 4 KB = four 0x400-byte pages */

static uint32_t mock_calloc_ppn;
static status_$t mock_calloc_status;
static int mock_calloc_calls;

static int mock_install_calls;
static uint32_t mock_install_ppn;
static uint32_t mock_install_va;
static uint32_t mock_install_flags;

void WP_$CALLOC(uint32_t *ppn_out, status_$t *status)
{
    mock_calloc_calls++;
    *ppn_out = mock_calloc_ppn;
    *status = mock_calloc_status;
}

void (MMU_$INSTALL)(uint32_t ppn, uint32_t va, uint32_t flags)
{
    mock_install_calls++;
    mock_install_ppn = ppn;
    mock_install_va = va;
    mock_install_flags = flags;
}

static void reset_mocks(void)
{
    memset(page_store, 0xA5, sizeof(page_store));
    mock_calloc_ppn = 0x00000123;
    mock_calloc_status = 0;
    mock_calloc_calls = 0;
    mock_install_calls = 0;
    mock_install_ppn = 0;
    mock_install_va = 0;
    mock_install_flags = 0;
}

/* Number of 0xA5-filled longwords still untouched, starting at index i. */
static int untouched_from(int i)
{
    int n = 0;
    for (; i < (int)(sizeof(page_store) / sizeof(page_store[0])); i++) {
        if (page_store[i] == 0xA5A5A5A5u) {
            n++;
        }
    }
    return n;
}

static void test_zeroes_exactly_one_page(void)
{
    /* Start at the second 0x400-byte page inside the buffer. */
    uintptr_t base = (uintptr_t)&page_store[256];

    ASSERT_EQ(0x00000123, mst_$init_table_page(base));
    ASSERT_EQ(1, mock_calloc_calls);
    ASSERT_EQ(1, mock_install_calls);

    /* Page before is untouched, the page itself is zero, the page after is
     * untouched. */
    for (int i = 0; i < 256; i++) {
        ASSERT_EQ(0xA5A5A5A5u, page_store[i]);
    }
    for (int i = 256; i < 512; i++) {
        ASSERT_EQ(0u, page_store[i]);
    }
    ASSERT_EQ(512, untouched_from(512));
}

static void test_install_arguments(void)
{
    uintptr_t base = (uintptr_t)&page_store[256];

    mock_calloc_ppn = 0x0000ABCD;
    (void)mst_$init_table_page(base);

    ASSERT_EQ(0x0000ABCD, mock_install_ppn);
    /* MMU_$INSTALL still takes a 32-bit VA, so compare the low half. */
    ASSERT_EQ((uint32_t)base, mock_install_va);
    /* 0xE42D12: "pea (0x16).w" -> ASID 0, protection 0x16 */
    ASSERT_EQ(0x00000016, mock_install_flags);
}

/*
 * "andi.w #-0x400" masks the LOW word only.  An unaligned address inside a
 * page must be rounded down to that page, and the high half kept intact.
 */
static void test_word_alignment_mask(void)
{
    uintptr_t base = (uintptr_t)&page_store[256];
    uintptr_t unaligned = base + 0x2AC;     /* somewhere inside the page */

    /* Precondition: page_store[256] really is 0x400-byte aligned. */
    ASSERT_EQ(base, base & ~(uintptr_t)0x3FF);

    (void)mst_$init_table_page(unaligned);
    ASSERT_EQ((uint32_t)base, mock_install_va);
    for (int i = 256; i < 512; i++) {
        ASSERT_EQ(0u, page_store[i]);
    }
}

/*
 * WP_$CALLOC's status is written but never tested (there is no "tst" of
 * (-0x8,A6) anywhere in the function), so a failure still installs and
 * zeroes the page.
 */
static void test_calloc_status_is_ignored(void)
{
    uintptr_t base = (uintptr_t)&page_store[256];

    mock_calloc_status = 0x00070001;        /* status_$mmu_miss */
    mock_calloc_ppn = 0;

    ASSERT_EQ(0, mst_$init_table_page(base));
    ASSERT_EQ(1, mock_install_calls);
    for (int i = 256; i < 512; i++) {
        ASSERT_EQ(0u, page_store[i]);
    }
}

int main(void)
{
    printf("Running mst_$init_table_page tests...\n\n");

    RUN_TEST(zeroes_exactly_one_page);
    RUN_TEST(install_arguments);
    RUN_TEST(word_alignment_mask);
    RUN_TEST(calloc_status_is_ignored);

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}

/* The real implementation under test. */
#include "../init_table_page.c"
