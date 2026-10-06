/*
 * disk/test/test_grow_qblk_pool.c - unit tests for disk_$grow_qblk_pool
 * (0x00E3BC40).  DISK_BLK_POOL lives in a host arena reached through
 * ARCH_HOST_VA_BASE.
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

#include "disk/disk_internal.h"
#include "math/math.h"
#include "wp/wp.h"
#include "mmu/mmu.h"
#include "misc/crash_system.h"

uint8_t DISK_$DATA[DISK_$DATA_SIZE];

static uint8_t arena[DISK_QBLK_MAX_PAGES * 0x400];
static char log_buf[256];
static void note(const char *s) { strcat(log_buf, s); }
static uint32_t next_ppn;
static status_$t calloc_status;
static int crashes;
static uint32_t inst_va[20], inh_va[20];
static int n_inst, n_inh;

short M$OIS$WLW(long dividend, short divisor) { return (short)(dividend % divisor); }
void ML_$EXCLUSION_START(ml_$exclusion_t *e)
{
    if ((uint8_t *)e != DISK_$DATA + DMOD_EXCLUSION) note("!");
    note("<");
}
void ML_$EXCLUSION_STOP(ml_$exclusion_t *e)
{
    if ((uint8_t *)e != DISK_$DATA + DMOD_EXCLUSION) note("!");
    note(">");
}
void WP_$CALLOC(uint32_t *ppn_out, status_$t *status)
{
    note("W");
    *ppn_out = next_ppn++;
    *status = calloc_status;
}
void MMU_$INSTALL(uint32_t ppn, uint32_t va, uint32_t flags)
{
    (void)ppn;
    note("I");
    if (flags != 0x16) note("!");
    inst_va[n_inst++] = va;
}
void MMU_$CACHE_INHIBIT_VA(uint32_t va) { note("C"); inh_va[n_inh++] = va; }
void CRASH_SYSTEM(const status_$t *status_p) { (void)status_p; crashes++; note("X"); }

#include "../grow_qblk_pool.c"

#define U16(off) (*(uint16_t *)(DISK_$DATA + (off)))
#define U32(off) (*(uint32_t *)(DISK_$DATA + (off)))
#define BLK(va, off) (*(uint32_t *)(arena + ((va) - DISK_BLK_POOL_VA) + (off)))

static void reset_state(void)
{
    memset(DISK_$DATA, 0, sizeof(DISK_$DATA));
    memset(arena, 0xEE, sizeof(arena));
    ARCH_HOST_VA_BASE = (uintptr_t)arena - DISK_BLK_POOL_VA;
    log_buf[0] = 0;
    next_ppn = 0x200;
    calloc_status = 0;
    crashes = 0;
    n_inst = n_inh = 0;
}

static void test_first_growth_three_pages_and_reserve(void)
{
    int8_t r = disk_$grow_qblk_pool(5);         /* want raised to 0x21 */
    ASSERT_EQ(0, strcmp(log_buf, ">WICWICWIC<"));
    ASSERT_EQ(3, U16(DMOD_PAGES_ALLOC));
    ASSERT_EQ(DISK_BLK_POOL_VA, inst_va[0]);
    ASSERT_EQ(DISK_BLK_POOL_VA + 0x800, inst_va[2]);
    ASSERT_EQ(DISK_BLK_POOL_VA + 0x800, inh_va[2]);
    /* reserve = block 0, head = block 1 */
    ASSERT_EQ(DISK_BLK_POOL_VA, U32(DMOD_RESERVE_BLOCK));
    ASSERT_EQ(0xFF, DISK_$DATA[DMOD_RESERVE_AVAIL]);
    ASSERT_EQ(DISK_BLK_POOL_VA + 0x40, U32(DMOD_FREE_HEAD));
    ASSERT_EQ(0, BLK(DISK_BLK_POOL_VA, DISK_QBLK_FREE_NEXT));
    ASSERT_EQ(0, BLK(DISK_BLK_POOL_VA, DISK_QBLK_FORWARD));
    ASSERT_EQ(47, U16(DMOD_AVAIL_COUNT));
    /* block 1: next = block 2, phys = 0x200 << 10 + 0x60 */
    ASSERT_EQ(DISK_BLK_POOL_VA + 0x80, BLK(DISK_BLK_POOL_VA + 0x40, DISK_QBLK_FREE_NEXT));
    ASSERT_EQ((0x200u << 10) + 0x60, BLK(DISK_BLK_POOL_VA + 0x40, DISK_QBLK_PHYS));
    /* page 2's first block uses ppn 0x201 */
    ASSERT_EQ((0x201u << 10) + 0x20, BLK(DISK_BLK_POOL_VA + 0x400, DISK_QBLK_PHYS));
    /* page 1's last block links page 2; the last page's links the old head (0) */
    ASSERT_EQ(DISK_BLK_POOL_VA + 0x400, BLK(DISK_BLK_POOL_VA + 0x3C0, DISK_QBLK_FREE_NEXT));
    ASSERT_EQ(0, BLK(DISK_BLK_POOL_VA + 0xBC0, DISK_QBLK_FREE_NEXT));
    ASSERT_EQ(0xFF, (uint8_t)r);
}

static void test_later_growth_chains_old_head(void)
{
    U32(DMOD_RESERVE_BLOCK) = 0x1234;
    U16(DMOD_PAGES_ALLOC) = 2;
    U16(DMOD_AVAIL_COUNT) = 3;
    U32(DMOD_FREE_HEAD) = 0xABCD0;
    ASSERT_EQ(0xFF, (uint8_t)disk_$grow_qblk_pool(20)); /* (20-3)/16 -> 2 pages */
    ASSERT_EQ(4, U16(DMOD_PAGES_ALLOC));
    ASSERT_EQ(DISK_BLK_POOL_VA + 0x800, U32(DMOD_FREE_HEAD));
    ASSERT_EQ(35, U16(DMOD_AVAIL_COUNT));
    ASSERT_EQ(0xABCD0, BLK(DISK_BLK_POOL_VA + 0xFC0, DISK_QBLK_FREE_NEXT));
    ASSERT_EQ(0x1234, U32(DMOD_RESERVE_BLOCK));
}

static void test_clamped_to_sixteen_pages(void)
{
    U32(DMOD_RESERVE_BLOCK) = 0x1234;
    U16(DMOD_PAGES_ALLOC) = 15;
    ASSERT_EQ(0, disk_$grow_qblk_pool(100));
    ASSERT_EQ(16, U16(DMOD_PAGES_ALLOC));
    ASSERT_EQ(16, U16(DMOD_AVAIL_COUNT));
}

static void test_full_pool_disables_growth(void)
{
    U32(DMOD_RESERVE_BLOCK) = 0x1234;
    U16(DMOD_PAGES_ALLOC) = 16;
    ASSERT_EQ(0, disk_$grow_qblk_pool(100));
    ASSERT_EQ(0xFF, DISK_$DATA[DMOD_ALLOC_DISABLED]);
    ASSERT_EQ(0, strlen(log_buf));
}

static void test_calloc_error_crashes(void)
{
    U32(DMOD_RESERVE_BLOCK) = 0x1234;
    calloc_status = 0x00050001;
    disk_$grow_qblk_pool(1);
    ASSERT_EQ(1, crashes);
}

int main(void)
{
    printf("disk_$grow_qblk_pool tests\n");
    RUN_TEST(first_growth_three_pages_and_reserve);
    RUN_TEST(later_growth_chains_old_head);
    RUN_TEST(clamped_to_sixteen_pages);
    RUN_TEST(full_pool_disables_growth);
    RUN_TEST(calloc_error_crashes);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
