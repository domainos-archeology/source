/*
 * mmap/test/test_release_pages.c - Unit tests for MMAP_$RELEASE_PAGES
 * (0x00E0CFF8)
 *
 * The real mmap/release_pages.c is #included below and driven through a host
 * copy of the MMAP module data area; mmap_$remove_from_wsl and
 * mmap_$add_to_wsl are stubbed and recorded, so each test asserts exactly
 * the destination pool the function chose.
 *
 * Covered (this is the bead source-uxu3 fix): the dirty-page flush test.
 * A4 holds 0xEC5400 for the whole loop (0x00E0D034) and both arms index it
 * by seg * 0x14 and read the longword at -0x10, i.e.
 * MMAP_$SEG_ASTE_FOR(seg)->aote:
 *   0x00E0D0C4  tst.w (0x28,A0) / sne   -> high word of aote->dtm_high
 *   0x00E0D0E0  tst.b (0xb9,A0) / smi   -> aote->remote_flag < 0
 *
 * The seg-table arena is a plain host array: aste_t.aote is a real pointer
 * (movea.l (-0x10,A1),A0), so no field here holds a target VA and
 * ARCH_HOST_VA_BASE stays at its default of zero.
 */

#include <stdio.h>
#include <string.h>

#include "mmap/mmap_internal.h"
#include "mmu/mmu.h"
#include "arch/arch.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
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

/* ============================================================================
 * Module data
 * ============================================================================ */

#define TEST_PAGES      16
#define TEST_SEGMENTS   8

#define TEST_PID        2
#define TEST_WSL        7
#define TEST_VPN        0x203   /* MMAP_$MMAPE holds ppn 0x200..0xFFF */
#define TEST_SEG        1       /* the table is 1-based */

static uint32_t pft_store[0x1000];   /* the PFT is indexed by ppn, up to 0xFFF */

/*
 * The MMAP_ module data block (`D E23284 MMAP_ size = AA8').  The pid-to-WSL
 * words this test writes are MMAP_$WS_OWNER reached through the -2 bias, so
 * they are fields of this one object rather than a separate array.
 */
MODULE_DATA_DEFINE(mmap_globals_t, MMAP_$DATA, 0x00E23284);

MODULE_DATA_DEFINE(mmap_$mmape_table_t, MMAP_$MMAPE, 0x00EB4800);
uint32_t *mmu_pft_base    = pft_store;

/* The AST_ module blocks (ast/ast.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);
static aote_t aote_store[TEST_SEGMENTS];

/* ============================================================================
 * Mocks
 * ============================================================================ */

#define MAX_CALLS 8
static int      remove_calls;
static int      add_calls;
static uint32_t add_vpn[MAX_CALLS];
static uint16_t add_type[MAX_CALLS];
static int8_t   add_at_head[MAX_CALLS];

void mmap_$remove_from_wsl(mmape_t *page, uint32_t vpn)
{
    (void)page;
    (void)vpn;
    remove_calls++;
}

void mmap_$add_to_wsl(mmape_t *page, uint32_t vpn, uint16_t wsl_index,
                      int8_t insert_at_head)
{
    (void)page;
    if (add_calls < MAX_CALLS) {
        add_vpn[add_calls] = vpn;
        add_type[add_calls] = wsl_index;
        add_at_head[add_calls] = insert_at_head;
    }
    add_calls++;
}

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../release_pages.c"

/* ============================================================================
 * Helpers
 * ============================================================================ */

static mmape_t *page;
static aote_t  *aote;

/* One releasable page belonging to TEST_PID's WSL, owned by TEST_SEG. */
static void reset_module(uint8_t flags1, uint8_t flags2)
{
    memset(&MMAP_$MMAPE, 0, sizeof(MMAP_$MMAPE));
    memset(pft_store, 0, sizeof(pft_store));
    memset(&MMAP_GLOBALS, 0, sizeof(MMAP_GLOBALS));
    memset(AST_$AOT.aste, 0, sizeof(AST_$AOT.aste));
    memset(aote_store, 0, sizeof(aote_store));

    remove_calls = 0;
    add_calls = 0;

    MMAP_PID_TO_WSL[TEST_PID] = TEST_WSL;

    page = MMAPE_FOR_VPN(TEST_VPN);
    page->wsl_index = TEST_WSL;
    page->wire_count = 0;
    page->segment = TEST_SEG;
    page->flags1 = (uint8_t)(flags1 | MMAPE_FLAG1_IN_WSL);
    page->flags2 = flags2;

    aote = &aote_store[TEST_SEG - 1];
    MMAP_$SEG_ASTE_FOR(TEST_SEG)->aote = aote;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/* 0x00E0D0E0: not ON_DISK -> aote->remote_flag < 0 selects DIRTY_FL. */
TEST(dirty_remote_object_goes_to_dirty_fl)
{
    reset_module(0, MMAPE_FLAG2_MODIFIED);
    aote->remote_flag = -1;

    uint32_t vpns[1] = { TEST_VPN };
    MMAP_$RELEASE_PAGES(TEST_PID, vpns, 1);

    ASSERT_EQ(1, remove_calls);
    ASSERT_EQ(1, add_calls);
    ASSERT_EQ(TEST_VPN, add_vpn[0]);
    ASSERT_EQ(MMAP_PAGE_TYPE_DIRTY_FL, add_type[0]);
    ASSERT_EQ(-1, add_at_head[0]);      /* 0x00E0D0F8: st -(SP) */
}

/* Same page, a local object -> DIRTY_NF (0x00E0D0F2). */
TEST(dirty_local_object_goes_to_dirty_nf)
{
    reset_module(0, MMAPE_FLAG2_MODIFIED);
    aote->remote_flag = 0x7F;

    uint32_t vpns[1] = { TEST_VPN };
    MMAP_$RELEASE_PAGES(TEST_PID, vpns, 1);

    ASSERT_EQ(1, add_calls);
    ASSERT_EQ(MMAP_PAGE_TYPE_DIRTY_NF, add_type[0]);
}

/* 0x00E0D0C4: ON_DISK -> the high word of aote->dtm_high. */
TEST(dirty_on_disk_uses_aote_dtm_high)
{
    reset_module(0, (uint8_t)(MMAPE_FLAG2_ON_DISK | MMAPE_FLAG2_MODIFIED));
    aote->dtm_high = 0x00010000u;
    aote->remote_flag = 0;              /* not consulted on this arm */

    uint32_t vpns[1] = { TEST_VPN };
    MMAP_$RELEASE_PAGES(TEST_PID, vpns, 1);

    ASSERT_EQ(1, add_calls);
    ASSERT_EQ(MMAP_PAGE_TYPE_DIRTY_FL, add_type[0]);
}

/* Only the low half is set, so `tst.w (0x28,A0)` reads zero. */
TEST(dirty_on_disk_ignores_low_half_of_dtm_high)
{
    reset_module(0, (uint8_t)(MMAPE_FLAG2_ON_DISK | MMAPE_FLAG2_MODIFIED));
    aote->dtm_high = 0x0000FFFFu;
    aote->remote_flag = -1;             /* not consulted on this arm */

    uint32_t vpns[1] = { TEST_VPN };
    MMAP_$RELEASE_PAGES(TEST_PID, vpns, 1);

    ASSERT_EQ(1, add_calls);
    ASSERT_EQ(MMAP_PAGE_TYPE_DIRTY_NF, add_type[0]);
}

/*
 * A clean page never looks at the segment table at all: the AOTE link is
 * left nil here, so a stray dereference would fault.
 */
TEST(clean_impure_page_never_reads_the_segment_table)
{
    reset_module(MMAPE_FLAG1_IMPURE, 0);
    MMAP_$SEG_ASTE_FOR(TEST_SEG)->aote = (aote_t *)0;

    uint32_t vpns[1] = { TEST_VPN };
    MMAP_$RELEASE_PAGES(TEST_PID, vpns, 1);

    ASSERT_EQ(1, add_calls);
    ASSERT_EQ(MMAP_PAGE_TYPE_PURE, add_type[0]);
}

/* Wired pages are skipped entirely (0x00E0D062). */
TEST(wired_page_is_skipped)
{
    reset_module(0, MMAPE_FLAG2_MODIFIED);
    page->wire_count = 1;

    uint32_t vpns[1] = { TEST_VPN };
    MMAP_$RELEASE_PAGES(TEST_PID, vpns, 1);

    ASSERT_EQ(0, remove_calls);
    ASSERT_EQ(0, add_calls);
}

int main(void)
{
    printf("MMAP_$RELEASE_PAGES tests\n");
    RUN_TEST(dirty_remote_object_goes_to_dirty_fl);
    RUN_TEST(dirty_local_object_goes_to_dirty_nf);
    RUN_TEST(dirty_on_disk_uses_aote_dtm_high);
    RUN_TEST(dirty_on_disk_ignores_low_half_of_dtm_high);
    RUN_TEST(clean_impure_page_never_reads_the_segment_table);
    RUN_TEST(wired_page_is_skipped);
    printf("%d passed, %d failed\n", tests_passed - tests_failed, tests_failed);
    return tests_failed != 0;
}
