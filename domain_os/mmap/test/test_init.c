/*
 * mmap/test/test_init.c - Unit tests for MMAP_$INIT (0x00E3193E)
 *
 * Drives the real mmap/init.c over a host arena of 0x1000 mmape_t entries
 * whose first word carries a fake memory probe.  Covers the re-emission
 * fixes: the probe is read from mmape_t word 0 (not the PFT), field_14 is
 * left alone, DUMP_$ADDRS starts are masked with 0xFFF80000, the block
 * table is translated from 0xEB4800 + block*0x400, and the in-use bits go
 * to MMAP_$WSL[5] (0x80) and MMAP_$WSL[6] (0xA0, max_pages 100).
 */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "mmap/mmap_internal.h"
#include "mmu/mmu.h"
#include "misc/misc.h"
#include "arch/arch.h"

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

#define TEST_PAGES 0x1000
static mmape_t  mmape_store[TEST_PAGES];
static uint32_t pft_store[TEST_PAGES];
mmap_globals_t MMAP_GLOBALS_STORAGE;
mmape_t  *mmap_mmape_base = mmape_store;
uint32_t *mmu_pft_base    = pft_store;
mem_range_t DUMP_$ADDRS[DUMP_ADDRS_RANGES];

static int      vtop_calls;
static uint32_t vtop_va[64];
static int      crash_calls;
static status_$t crash_status;
static jmp_buf  crash_jmp;

uint32_t mmu_$vtop_or_crash(uint32_t va)
{
    if (vtop_calls < 64) vtop_va[vtop_calls] = va;
    vtop_calls++;
    return va >> 10;
}

void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_calls++;
    crash_status = *status_p;
    longjmp(crash_jmp, 1);
}

#include "../init.c"

static uint32_t table[MMAP_INIT_BLOCKS];

static void probe(uint32_t vpn, uint16_t word0)
{
    mmape_store[vpn].wire_count = (uint8_t)(word0 >> 8);
    mmape_store[vpn].seg_offset = (uint8_t)word0;
}

static void reset_module(void)
{
    uint32_t v;
    int i;

    memset(mmape_store, 0, sizeof(mmape_store));
    memset(&MMAP_GLOBALS, 0, sizeof(MMAP_GLOBALS));
    memset(DUMP_$ADDRS, 0, sizeof(DUMP_$ADDRS));
    memset(table, 0xAA, sizeof(table));
    vtop_calls = 0;
    crash_calls = 0;

    /* the image seeds MMAP_$INIT narrows */
    MMAP_$HPPN = 1;
    MMAP_$LPPN = 0xFFF;

    for (i = 0; i < MMAP_WSL_SLOTS; i++) {
        MMAP_$WSL[i].flags = 0xFF;
        MMAP_$WSL[i].field_14 = 0xDEAD;
    }
    for (i = 0; i < MMAP_WS_OWNER_SLOTS; i++) MMAP_$WS_OWNER[i] = 0x55;

    /* make ARCH_PTR_TO_VA(&mmape_store[vpn]) == 0xEB2800 + vpn*0x10 */
    ARCH_HOST_VA_BASE = (uintptr_t)mmape_store - 0xEB2800u;

    /* two ranges: 0x200..0x37F (first 0x100 pageable) and 0x400..0x43F */
    for (v = 0x200; v < 0x300; v++) probe(v, 0xC000);
    for (v = 0x300; v < 0x380; v++) probe(v, 0x8000);
    for (v = 0x400; v < 0x440; v++) probe(v, 0x4000);
}

TEST(pools_ranges_and_table)
{
    reset_module();
    if (setjmp(crash_jmp)) { ASSERT_EQ(0, 1); }

    MMAP_$INIT(table);

    ASSERT_EQ(0, crash_calls);
    ASSERT_EQ(0x1C0, MMAP_$REAL_PAGES);
    ASSERT_EQ(0x140, MMAP_$PAGEABLE_PAGES);
    ASSERT_EQ(0x200, MMAP_$LPPN);
    ASSERT_EQ(0x43F, MMAP_$HPPN);
    ASSERT_EQ(0x140, MMAP_$WSL[0].page_count);
    ASSERT_EQ(0x200, MMAP_$WSL[0].head_vpn);

    /* free list: head 0x200, spliced between head and its next each time */
    ASSERT_EQ(0x43F, MMAPE_FOR_VPN(0x200)->next_vpn);
    ASSERT_EQ(0x200, MMAPE_FOR_VPN(0x43F)->prev_vpn);
    ASSERT_EQ(0x201, MMAPE_FOR_VPN(0x200)->prev_vpn);
    ASSERT_EQ(0, MMAPE_FOR_VPN(0x200)->wire_count);
    ASSERT_EQ(0, MMAPE_FOR_VPN(0x200)->wsl_index);
    ASSERT_EQ(MMAPE_FLAG1_IN_WSL, MMAPE_FOR_VPN(0x200)->flags1);
    ASSERT_EQ(1, MMAPE_FOR_VPN(0x300)->wire_count);
    ASSERT_EQ(5, MMAPE_FOR_VPN(0x300)->wsl_index);
    ASSERT_EQ(1, MMAPE_FOR_VPN(0x380)->wire_count);
    ASSERT_EQ(5, MMAPE_FOR_VPN(0x380)->wsl_index);

    /* DUMP_$ADDRS, starts masked with 0xFFF80000 */
    ASSERT_EQ(0x80000, DUMP_$ADDRS[0].start);
    ASSERT_EQ(0x37Fu << 10, DUMP_$ADDRS[0].end);
    ASSERT_EQ(0x100000, DUMP_$ADDRS[1].start);
    ASSERT_EQ(0x43Fu << 10, DUMP_$ADDRS[1].end);

    /*
     * Blocks 0..5 and 8 hold real pages and are zeroed; the other 49
     * entries still hold the 0xFFF seed and are the ones translated
     * (`tst.l / beq' at 0x00E31B10 skips zero entries).
     */
    ASSERT_EQ(49, vtop_calls);
    ASSERT_EQ(0xEB4800 + 6 * 0x400, vtop_va[0]);
    ASSERT_EQ(0xEB4800 + 7 * 0x400, vtop_va[1]);
    ASSERT_EQ(0xEB4800 + 9 * 0x400, vtop_va[2]);
    ASSERT_EQ(0xEB4800 + 55 * 0x400, vtop_va[48]);
    ASSERT_EQ(0, table[0]);
    ASSERT_EQ(0, table[5]);
    ASSERT_EQ(0, table[8]);
    ASSERT_EQ((0xEB4800 + 6 * 0x400) >> 10, table[6]);
    ASSERT_EQ((0xEB4800 + 55 * 0x400) >> 10, table[55]);
}

TEST(wsl_headers_and_owner_table)
{
    reset_module();
    if (setjmp(crash_jmp)) { ASSERT_EQ(0, 1); }

    MMAP_$INIT(table);

    ASSERT_EQ(7, MMAP_$WS_OWNER[0]);
    ASSERT_EQ(0, MMAP_$WS_OWNER[1]);
    ASSERT_EQ(0, MMAP_$WS_OWNER[63]);

    ASSERT_EQ(0x07, MMAP_$WSL[0].flags);
    ASSERT_EQ(0x87, MMAP_$WSL[5].flags);
    ASSERT_EQ(0xA7, MMAP_$WSL[6].flags);
    ASSERT_EQ(0x07, MMAP_$WSL[7].flags);
    ASSERT_EQ(0x1000, MMAP_$WSL[5].max_pages);
    ASSERT_EQ(100, MMAP_$WSL[6].max_pages);
    ASSERT_EQ(0x1000, MMAP_$WSL[69].max_pages);
    ASSERT_EQ(0xDEAD, MMAP_$WSL[6].field_14);   /* not touched */
    ASSERT_EQ(0, MMAP_$WSL[6].ws_floor);
    ASSERT_EQ(0, MMAP_$WSL[6].owner);
}

TEST(third_range_crashes)
{
    uint32_t v;

    reset_module();
    for (v = 0x500; v < 0x510; v++) probe(v, 0x8000);

    if (setjmp(crash_jmp) == 0) {
        MMAP_$INIT(table);
        ASSERT_EQ(0, 1);
    }

    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(status_$mmap_examined_max, crash_status);
}

int main(void)
{
    printf("MMAP_$INIT tests\n");
    RUN_TEST(pools_ranges_and_table);
    RUN_TEST(wsl_headers_and_owner_table);
    RUN_TEST(third_range_crashes);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
