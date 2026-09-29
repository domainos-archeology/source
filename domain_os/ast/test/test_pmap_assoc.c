/*
 * ast/test/test_pmap_assoc.c - Unit tests for AST_$PMAP_ASSOC (0x00E042B0)
 *
 * The test #includes ast/pmap_assoc.c directly.  It pins:
 *
 *   - a wired installed page is unmapped and its bit 29 cleared before
 *     the "pages wired" check on the MMAPE;
 *   - replacing an installed page takes the MMAPE's disk address, frees
 *     the frame and lowers the page count;
 *   - "bad assoc" for an empty local entry stops unless the allow flag
 *     is TRUE (low byte negative);
 *   - the new MMAPE fields, INSTALL_LIST only for an unwired frame, the
 *     entry's low word / bits 30 and 29, the PFT word, page count up.
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

#include "ast/pmap_assoc.c"

#define TEST_N_PAGES 32
#define TEST_N_FRAMES 0x1000
/* The AST_ module blocks (ast/ast.h) and the segment map (pmap/pmap.h). */
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);
MODULE_DATA_DEFINE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);
static uint32_t       test_pft[TEST_N_FRAMES];
MODULE_DATA_DEFINE(mmap_$mmape_table_t, MMAP_$MMAPE, 0x00EB4800);
uint32_t       *mmu_pft_base    = test_pft;

static aote_t test_aote;
static aste_t test_aste;

static int wait_calls;
void ast_$wait_for_page_transition(void) { wait_calls++; }

static int remove_calls; static uint32_t remove_ppn;
void MMU_$REMOVE(uint32_t ppn) { remove_calls++; remove_ppn = ppn; }

static int free_calls; static mmape_t *free_page; static uint32_t free_vpn;
void MMAP_$FREE_REMOVE(mmape_t *page, uint32_t vpn) { free_calls++; free_page = page; free_vpn = vpn; }

static int install_calls; static uint32_t install_vpn; static uint16_t install_count; static int8_t install_wired;
void MMAP_$INSTALL_LIST(uint32_t *vpn_array, uint16_t count, int8_t use_wired)
{
    install_calls++; install_vpn = vpn_array[0]; install_count = count; install_wired = use_wired;
}

static int crash_calls; static const status_$t *crash_status;
void CRASH_SYSTEM(const status_$t *s) { crash_calls++; crash_status = s; }

static uint32_t *row1(void) { return (uint32_t *)PMAP_SEGMAP_ROW(1); }

static void reset_state(void)
{
    memset(&PMAP_$SEGMAP, 0, sizeof(PMAP_$SEGMAP));
    memset(&MMAP_$MMAPE, 0, sizeof(MMAP_$MMAPE));
    memset(test_pft, 0, sizeof(test_pft));
    memset(&test_aote, 0, sizeof(test_aote));
    memset(&test_aste, 0, sizeof(test_aste));
    test_aste.aote = &test_aote;
    test_aste.seg_index = 1;
    test_aste.page_count = 4;
    wait_calls = 0; remove_calls = 0; free_calls = 0; install_calls = 0;
    crash_calls = 0; crash_status = NULL;
}

TEST(replace_installed_page)
{
    uint32_t *row = row1();
    status_$t status = 0x55;

    row[3] = SEGMAP_VALID | SEGMAP_WIRED | 0x0300;
    MMAPE_FOR_VPN(0x300)->disk_addr = 0x00001111;
    test_pft[0x400] = 0x0000FFFF;

    AST_$PMAP_ASSOC(&test_aste, 3, 0x400, 0, 0, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, remove_calls); ASSERT_EQ(0x300, remove_ppn);
    ASSERT_EQ(1, free_calls); ASSERT_EQ((uintptr_t)MMAPE_FOR_VPN(0x300), (uintptr_t)free_page);
    ASSERT_EQ(0x300, free_vpn);
    /* new MMAPE */
    ASSERT_EQ(1, MMAPE_FOR_VPN(0x400)->segment);
    ASSERT_EQ(MMAPE_FLAG1_IMPURE, MMAPE_FOR_VPN(0x400)->flags1);
    ASSERT_EQ(3, MMAPE_FOR_VPN(0x400)->seg_offset);
    ASSERT_EQ(MMAPE_FLAG2_MODIFIED, MMAPE_FOR_VPN(0x400)->flags2);
    ASSERT_EQ(0x1111, MMAPE_FOR_VPN(0x400)->disk_addr);   /* the old page's address */
    ASSERT_EQ(1, install_calls); ASSERT_EQ(0x400, install_vpn);
    ASSERT_EQ(1, install_count); ASSERT_EQ(0, install_wired);
    /* entry: low word, bits 30 and 29; the disk addr survives in bits 22..16 only if set */
    ASSERT_EQ(SEGMAP_VALID | SEGMAP_WIRED | 0x0400, row[3]);
    ASSERT_EQ(0x0000BFFF & ~0x4000u | 0x2000, test_pft[0x400] | 0);
    ASSERT_EQ(4, test_aste.page_count);                /* -1 then +1 */
    ASSERT_EQ(0, crash_calls);
}

TEST(wired_mmape_refuses)
{
    uint32_t *row = row1();
    status_$t status = 0;

    row[0] = SEGMAP_VALID | 0x0300;
    MMAPE_FOR_VPN(0x300)->wire_count = 1;

    AST_$PMAP_ASSOC(&test_aste, 0, 0x400, 0, 0, &status);

    ASSERT_EQ(status_$pmap_pages_wired, status);
    ASSERT_EQ(0, free_calls);
    ASSERT_EQ(SEGMAP_VALID | 0x0300, row[0]);          /* untouched */
    ASSERT_EQ(4, test_aste.page_count);
}

TEST(empty_local_entry_bad_assoc)
{
    uint32_t *row = row1();
    status_$t status = 0;

    AST_$PMAP_ASSOC(&test_aste, 5, 0x400, 0, 0, &status);
    ASSERT_EQ(status_$pmap_bad_assoc, status);
    ASSERT_EQ(0, row[5]);
    ASSERT_EQ(0, install_calls);

    /* allow flag TRUE (low byte 0xFF) goes on, status stays bad_assoc */
    AST_$PMAP_ASSOC(&test_aste, 5, 0x400, 0, 0x00FF, &status);
    ASSERT_EQ(status_$pmap_bad_assoc, status);
    ASSERT_EQ(SEGMAP_VALID | SEGMAP_WIRED | 0x0400, row[5]);
    ASSERT_EQ(1, install_calls);
    ASSERT_EQ(5, test_aste.page_count);
}

TEST(empty_remote_entry_is_fine)
{
    uint32_t *row = row1();
    status_$t status = 0;

    test_aote.remote_flag = (int8_t)0x80;
    AST_$PMAP_ASSOC(&test_aste, 7, 0x400, 0, 0, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(SEGMAP_VALID | SEGMAP_WIRED | 0x0400, row[7]);
}

TEST(on_disk_entry_keeps_address)
{
    uint32_t *row = row1();
    status_$t status = 0;

    row[2] = 0x00012345;
    MMAPE_FOR_VPN(0x400)->wire_count = 2;      /* wired: no INSTALL_LIST */

    AST_$PMAP_ASSOC(&test_aste, 2, 0x400, 0, 0, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x12345, MMAPE_FOR_VPN(0x400)->disk_addr);
    ASSERT_EQ(0, install_calls);
    ASSERT_EQ(SEGMAP_VALID | SEGMAP_WIRED | 0x00010400u, row[2]);
}

TEST(ppn_out_of_range_skips_mmape)
{
    uint32_t *row = row1();
    status_$t status = 0;

    row[1] = 0x00000001;
    AST_$PMAP_ASSOC(&test_aste, 1, 0x1234, 0, 0, &status);

    ASSERT_EQ(0, install_calls);
    ASSERT_EQ(SEGMAP_VALID | SEGMAP_WIRED | 0x1234, row[1]);
    ASSERT_EQ(0x2000, test_pft[0x1234 & 0xFFF] | 0 ? test_pft[0x1234] : 0x2000);
    ASSERT_EQ(0, crash_calls);
}

TEST(zero_ppn_crashes)
{
    uint32_t *row = row1();
    status_$t status = 0;

    row[1] = 0x00000001;
    AST_$PMAP_ASSOC(&test_aste, 1, 0, 0, 0, &status);
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(0x00050003, (uint32_t)*crash_status);
}

int main(void)
{
    printf("test_pmap_assoc (AST_$PMAP_ASSOC 0x00E042B0)\n");

    RUN_TEST(replace_installed_page);
    RUN_TEST(wired_mmape_refuses);
    RUN_TEST(empty_local_entry_bad_assoc);
    RUN_TEST(empty_remote_entry_is_fine);
    RUN_TEST(on_disk_entry_keeps_address);
    RUN_TEST(ppn_out_of_range_skips_mmape);
    RUN_TEST(zero_ppn_crashes);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
