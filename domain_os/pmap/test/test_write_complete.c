/*
 * pmap/test/test_write_complete.c - unit tests for pmap_$write_complete
 * (0x00E12D84)
 *
 * The MMAPE table, segment map, ASTE table and PFT are host objects;
 * MMAP_$AVAIL and EC_$ADVANCE are mocked.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_state(void);

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

#include "pmap/pmap_internal.h"
#include "ast/ast.h"
#include "ec/ec.h"

static uint32_t pft_store[0x1000];
#define SAU2_PFT_BASE pft_store   /* the SAU2 PFT (arch/m68k/sau2/hw.h) */
MODULE_DATA_DEFINE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);
MODULE_DATA_DEFINE(mmap_$mmape_table_t, MMAP_$MMAPE, 0x00EB4800);
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);

static int avail_calls, advance_calls;
static uint32_t avail_vpn;
static ec_$eventcount_t *advance_ec;

void MMAP_$AVAIL(uint32_t vpn) { avail_vpn = vpn; avail_calls++; }
void EC_$ADVANCE(ec_$eventcount_t *ec) { advance_ec = ec; advance_calls++; }

#include "../write_complete.c"

#define VPN     0x345
#define SEG     7
#define PAGE    3

static mmape_t *pg;
static uint32_t *ent;

static void reset_state(void)
{
    memset(&PMAP_$SEGMAP, 0, sizeof(PMAP_$SEGMAP));
    memset(&MMAP_$MMAPE, 0, sizeof(MMAP_$MMAPE));
    memset(&AST_$AOT, 0, sizeof(AST_$AOT));
    memset(pft_store, 0, sizeof(pft_store));
    avail_calls = advance_calls = 0;
    avail_vpn = 0;
    advance_ec = NULL;
    pg = MMAPE_FOR_VPN(VPN);
    pg->segment = SEG;
    pg->seg_offset = PAGE;
    ent = (uint32_t *)&PMAP_SEGMAP_ROW(SEG)[PAGE];
    *ent = 0x80000000u | 0x40000000u | VPN;
}

static void test_success_clears_writing_and_state(void)
{
    status_$t st = 0;
    pg->wsl_index = 3;
    pg->disk_addr = PMAP_DADDR_ASTE_DIRTY | 0x1234;
    pmap_$write_complete(VPN, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x40000000u | VPN, *ent);
    ASSERT_EQ(1, pg->wsl_index);
    ASSERT_EQ(0x1234, pg->disk_addr);
    ASSERT_EQ(ASTE_FLAG_DIRTY, AST_ASTE_ENTRY(SEG)->flags);
    ASSERT_EQ(0, advance_calls);
    ASSERT_EQ(0, avail_calls);
}

static void test_write_protected_is_success(void)
{
    status_$t st = status_$disk_write_protected;
    pg->wsl_index = 7;
    pmap_$write_complete(VPN, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(7, pg->wsl_index);            /* >= 5 untouched */
    ASSERT_EQ(0, AST_ASTE_ENTRY(SEG)->flags);
}

static void test_on_disk_flag_skips_aste_dirty(void)
{
    status_$t st = 0;
    pg->flags2 = MMAPE_FLAG2_ON_DISK;
    pg->disk_addr = PMAP_DADDR_ASTE_DIRTY;
    pmap_$write_complete(VPN, &st);
    ASSERT_EQ(PMAP_DADDR_ASTE_DIRTY, pg->disk_addr);
    ASSERT_EQ(0, AST_ASTE_ENTRY(SEG)->flags);
}

static void test_error_marks_page_and_advances(void)
{
    status_$t st = 0x00080001;
    pmap_$write_complete(VPN, &st);
    ASSERT_EQ(0x80080001u, (uint32_t)st);
    ASSERT_EQ(MMAPE_FLAG2_MODIFIED, pg->flags2);
    ASSERT_EQ(0x2000, pft_store[VPN]);
    ASSERT_EQ(1, avail_calls);
    ASSERT_EQ(VPN, avail_vpn);
    ASSERT_EQ(0x40000000u | VPN, *ent);
    ASSERT_EQ(1, advance_calls);
    ASSERT_EQ((unsigned long)&AST_$PMAP_IN_TRANS_EC, (unsigned long)advance_ec);
}

int main(void)
{
    printf("pmap_$write_complete tests:\n");
    RUN_TEST(success_clears_writing_and_state);
    RUN_TEST(write_protected_is_success);
    RUN_TEST(on_disk_flag_skips_aste_dirty);
    RUN_TEST(error_marks_page_and_advances);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
