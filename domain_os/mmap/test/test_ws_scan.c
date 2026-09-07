/*
 * mmap/test/test_ws_scan.c - Unit tests for MMAP_$WS_SCAN (0x00E0D364)
 *
 * The real mmap/ws_scan.c is #included below and driven through a host copy
 * of the MMAP module data area; mmap_$move_pages_to_wsl_type, MMU_$REMOVE
 * and CRASH_SYSTEM are stubbed and recorded.
 *
 * Covered (this is the bead source-uxu3 fix):
 *   - the aggressive-mode gate at 0x00E0D424-0x00E0D446: the segment table
 *     at 0xEC5400 is indexed by seg * 0x14 and read at -0x10, so it is
 *     MMAP_$SEG_ASTE_FOR(seg)->aote, and bit 12 of the aote's attribute
 *     flags word (aote+0x0E) decides whether the page may be taken
 *   - the dirty-page flush test, both arms:
 *       0x00E0D4DA-0x00E0D4F8  ON_DISK     -> high word of aote->len_high
 *       0x00E0D4FC-0x00E0D51A  not ON_DISK -> sign of the high word of
 *                                            aote->vol_uid
 *
 * The seg-table arena is a plain host array: aste_t.aote is a real pointer
 * (movea.l (-0x10,A1),A4 loads a 32-bit target VA into an address register),
 * so unlike disk/test/test_rtn_qblks_internal.c nothing here stores a VA in
 * a uint32_t field and ARCH_HOST_VA_BASE stays at its default of zero.
 */

#include <stdio.h>
#include <string.h>

#include "mmap/mmap_internal.h"
#include "mmu/mmu.h"
#include "misc/misc.h"
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

#define TEST_WSL_SLOTS  20
#define TEST_PAGES      16
#define TEST_SEGMENTS   8
#define TEST_PTES       64

#define TEST_WSL        5       /* WSL_INDEX_MIN_USER */
#define TEST_VPN        3
#define TEST_SEG        1       /* the table is 1-based */

static ws_hdr_t wsl_store[TEST_WSL_SLOTS];
static mmape_t  mmape_store[TEST_PAGES];
static uint32_t pft_store[TEST_PAGES];
static uint16_t pte_store[TEST_PTES];

ws_hdr_t *mmap_wsl        = wsl_store;
mmape_t  *mmap_mmape_base = mmape_store;
uint32_t *mmu_pft_base    = pft_store;
uint16_t *mmap_pte_base   = pte_store;
uint16_t  mmap_wsl_hi_mark;

/*
 * The 0xEC5400 table and the AOTEs it points at.  MMAP_$SEG_ASTE_FOR(seg)
 * is &MMAP_$SEG_ASTE[seg - 1], so segment 1 is slot 0.
 */
aste_t MMAP_$SEG_ASTE[TEST_SEGMENTS];
static aote_t aote_store[TEST_SEGMENTS];

uint32_t MMAP_$PAGEABLE_PAGES;
uint32_t MMAP_$WS_SCAN_CNT;
uint32_t MMAP_$WS_REMOVE;

/* ============================================================================
 * Mocks
 * ============================================================================ */

#define MAX_CALLS 8
static int      move_calls;
static uint32_t move_head[MAX_CALLS];
static uint16_t move_type[MAX_CALLS];
static int      mmu_removes;
static int      crashes;

void mmap_$move_pages_to_wsl_type(uint32_t vpn_head, uint16_t page_type,
                                  uint16_t scan_wsl_index, int16_t scan_mode)
{
    (void)scan_wsl_index;
    (void)scan_mode;
    if (move_calls < MAX_CALLS) {
        move_head[move_calls] = vpn_head;
        move_type[move_calls] = page_type;
    }
    move_calls++;
}

void MMU_$REMOVE(uint32_t vpn) { (void)vpn; mmu_removes++; }

void CRASH_SYSTEM(const status_$t *status_p) { (void)status_p; crashes++; }

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../ws_scan.c"

/* ============================================================================
 * Helpers
 * ============================================================================ */

static mmape_t *page;
static aote_t  *aote;

/*
 * One removable page, alone in its WSL, owned by segment TEST_SEG.  The PTE
 * is left invalid so the MMU_$REMOVE arm (0x00E0D4A6) is not taken.
 */
static void reset_module(uint8_t flags2)
{
    memset(wsl_store, 0, sizeof(wsl_store));
    memset(mmape_store, 0, sizeof(mmape_store));
    memset(pft_store, 0, sizeof(pft_store));
    memset(pte_store, 0, sizeof(pte_store));
    memset(MMAP_$SEG_ASTE, 0, sizeof(MMAP_$SEG_ASTE));
    memset(aote_store, 0, sizeof(aote_store));

    move_calls = 0;
    mmu_removes = 0;
    crashes = 0;
    MMAP_$PAGEABLE_PAGES = 0;
    MMAP_$WS_SCAN_CNT = 0;
    MMAP_$WS_REMOVE = 0;
    mmap_wsl_hi_mark = TEST_WSL_SLOTS - 1;

    wsl_store[TEST_WSL].page_count = 1;
    wsl_store[TEST_WSL].head_vpn = TEST_VPN;

    page = MMAPE_FOR_VPN(TEST_VPN);
    page->wire_count = 0;
    page->segment = TEST_SEG;
    page->seg_offset = 0;
    page->prev_vpn = TEST_VPN;
    page->next_vpn = TEST_VPN;
    page->flags2 = flags2;

    aote = &aote_store[TEST_SEG - 1];
    MMAP_$SEG_ASTE_FOR(TEST_SEG)->aote = aote;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/*
 * 0x00E0D43E/0x00E0D442: aote+0x0E bit 12 set, so the dirty page is taken.
 * flags2 has no ON_DISK bit, so the flush test reads aote->vol_uid, whose
 * high word is negative here -> the DIRTY_FL pool (0x00E0D51A).
 */
TEST(aggressive_mode_takes_page_when_aote_bit12_set)
{
    reset_module(MMAPE_FLAG2_MODIFIED);
    aote->attr_flags_hi = 0x10;         /* bit 12 of the flags word */
    aote->vol_uid = 0x80000000u;

    uint32_t scanned = MMAP_$WS_SCAN(TEST_WSL, -1, 4, 0);

    ASSERT_EQ(1u, scanned);
    ASSERT_EQ(0, crashes);
    ASSERT_EQ(1, move_calls);
    ASSERT_EQ(MMAP_PAGE_TYPE_DIRTY_FL, move_type[0]);
    ASSERT_EQ(TEST_VPN, move_head[0]);
    ASSERT_EQ(1u, MMAP_$WS_REMOVE);
    ASSERT_EQ(0u, wsl_store[TEST_WSL].page_count);
}

/* Same page, positive vol_uid high word -> the DIRTY_NF pool. */
TEST(aggressive_mode_local_object_goes_to_dirty_nf)
{
    reset_module(MMAPE_FLAG2_MODIFIED);
    aote->attr_flags_hi = 0x10;
    aote->vol_uid = 0x7FFFFFFFu;

    uint32_t scanned = MMAP_$WS_SCAN(TEST_WSL, -1, 4, 0);

    ASSERT_EQ(1u, scanned);
    ASSERT_EQ(1, move_calls);
    ASSERT_EQ(MMAP_PAGE_TYPE_DIRTY_NF, move_type[0]);
}

/*
 * 0x00E0D446: bit 12 clear -> beq past the removal, so the page stays in
 * the WSL and nothing is moved.
 */
TEST(aggressive_mode_leaves_page_when_aote_bit12_clear)
{
    reset_module(MMAPE_FLAG2_MODIFIED);
    aote->attr_flags_hi = 0x00;
    aote->vol_uid = 0x80000000u;

    uint32_t scanned = MMAP_$WS_SCAN(TEST_WSL, -1, 4, 0);

    ASSERT_EQ(1u, scanned);
    ASSERT_EQ(0, move_calls);
    ASSERT_EQ(0u, MMAP_$WS_REMOVE);
    ASSERT_EQ(1u, wsl_store[TEST_WSL].page_count);
}

/*
 * Normal mode with the referenced bit clear reaches the categorisation with
 * ON_DISK set, which takes the 0x00E0D4F4 arm: the high word of
 * aote->len_high.
 */
TEST(normal_mode_on_disk_uses_aote_len_high)
{
    reset_module((uint8_t)(MMAPE_FLAG2_ON_DISK | MMAPE_FLAG2_MODIFIED));
    aote->len_high = 0x00010000u;       /* non-zero high word */

    uint32_t scanned = MMAP_$WS_SCAN(TEST_WSL, 0, 4, 0);

    ASSERT_EQ(1u, scanned);
    ASSERT_EQ(1, move_calls);
    ASSERT_EQ(MMAP_PAGE_TYPE_DIRTY_FL, move_type[0]);
}

/* Only the LOW half of len_high is set, so `tst.w (0x28,A1)` reads zero. */
TEST(normal_mode_on_disk_ignores_low_half_of_len_high)
{
    reset_module((uint8_t)(MMAPE_FLAG2_ON_DISK | MMAPE_FLAG2_MODIFIED));
    aote->len_high = 0x0000FFFFu;

    uint32_t scanned = MMAP_$WS_SCAN(TEST_WSL, 0, 4, 0);

    ASSERT_EQ(1u, scanned);
    ASSERT_EQ(1, move_calls);
    ASSERT_EQ(MMAP_PAGE_TYPE_DIRTY_NF, move_type[0]);
}

int main(void)
{
    printf("MMAP_$WS_SCAN tests\n");
    RUN_TEST(aggressive_mode_takes_page_when_aote_bit12_set);
    RUN_TEST(aggressive_mode_local_object_goes_to_dirty_nf);
    RUN_TEST(aggressive_mode_leaves_page_when_aote_bit12_clear);
    RUN_TEST(normal_mode_on_disk_uses_aote_len_high);
    RUN_TEST(normal_mode_on_disk_ignores_low_half_of_len_high);
    printf("%d passed, %d failed\n", tests_passed - tests_failed, tests_failed);
    return tests_failed != 0;
}
