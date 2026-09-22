/*
 * disk/test/test_sort.c - Unit tests for DISK_$SORT (0x00E3C3E4) and its
 * nested swap procedure (0x00E3C370)
 *
 * Requests live in an arena that ARCH_HOST_VA_BASE points at; the +0x00
 * links are arena offsets.
 */

#include <stdio.h>
#include <string.h>

#include "disk/disk_internal.h"

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

#include "../sort.c"

#define REQ_VA(i) (0x100u + (i) * 0x40u)
static uint8_t arena[0x800];
static uint8_t dev[0x10];
static disk_$volume_t vol;

static disk_io_req_t *req(int i) { return (disk_io_req_t *)(arena + REQ_VA(i)); }

/* build a chain of n requests in index order */
static void *chain(int n)
{
    int i;
    for (i = 0; i < n; i++) {
        req(i)->next = (i + 1 < n) ? REQ_VA(i + 1) : 0;
    }
    return ARCH_VA_TO_PTR(REQ_VA(0));
}

static void set(int i, uint32_t lba, uint16_t cyl, uint8_t head, uint8_t sector)
{
    memset(req(i), 0, sizeof *req(i));
    req(i)->header[7] = lba;
    req(i)->daddr = ((uint32_t)cyl << 16) | ((uint32_t)head << 8) | sector;
}

static void reset(uint16_t dev_flags, uint16_t bat_step)
{
    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    memset(arena, 0, sizeof arena);
    memset(dev, 0, sizeof dev);
    *(uint16_t *)(dev + 0x08) = dev_flags;
    memset(&vol, 0, sizeof vol);
    vol.dev_info = dev;
    vol.bat_step = bat_step;
}

/* fill out[] with the request indexes in chain order, return the count */
static int order(void *head, int out[8])
{
    int n = 0;
    uint32_t p = ARCH_PTR_TO_VA(head);
    while (p != 0 && n < 8) {
        out[n++] = (int)((p - 0x100u) / 0x40u);
        p = ((disk_io_req_t *)ARCH_VA_TO_PTR(p))->next;
    }
    return n;
}

TEST(sorts_by_lba)
{
    void *q;
    int o[8];
    reset(0, 1);
    set(0, 30, 0, 0, 0); set(1, 10, 0, 0, 0); set(2, 20, 0, 0, 0); set(3, 5, 0, 0, 0);
    q = chain(4);
    DISK_$SORT(&vol, &q);
    ASSERT_EQ(4, order(q, o));
    ASSERT_EQ(3, o[0]); ASSERT_EQ(1, o[1]); ASSERT_EQ(2, o[2]); ASSERT_EQ(0, o[3]);
    ASSERT_EQ(0, req(0)->next);
}

TEST(sorts_by_daddr_for_bit9_drivers)
{
    void *q;
    int o[8];
    reset(0x0200, 1);
    set(0, 1, 5, 0, 0); set(1, 2, 3, 0, 0); set(2, 3, 4, 0, 0);
    q = chain(3);
    DISK_$SORT(&vol, &q);
    ASSERT_EQ(3, order(q, o));
    ASSERT_EQ(1, o[0]); ASSERT_EQ(2, o[1]); ASSERT_EQ(0, o[2]);
}

TEST(adjacent_swap_keeps_chain_intact)
{
    void *q;
    int o[8];
    reset(0, 1);
    set(0, 2, 0, 0, 0); set(1, 1, 0, 0, 0);
    q = chain(2);
    DISK_$SORT(&vol, &q);
    ASSERT_EQ(2, order(q, o));
    ASSERT_EQ(1, o[0]); ASSERT_EQ(0, o[1]);
}

/* bat_step 3: request 1 is only 1 sector after request 0 on the same
 * track, so the first later request 3 or more sectors after request 0
 * (request 3, sector 4) is swapped into its place. */
TEST(second_pass_pulls_far_sector_forward)
{
    void *q;
    int o[8];
    reset(0, 3);
    set(0, 10, 7, 1, 1);
    set(1, 11, 7, 1, 2);
    set(2, 12, 7, 1, 3);
    set(3, 13, 7, 1, 4);
    q = chain(4);
    DISK_$SORT(&vol, &q);
    ASSERT_EQ(4, order(q, o));
    ASSERT_EQ(0, o[0]); ASSERT_EQ(3, o[1]); ASSERT_EQ(2, o[2]); ASSERT_EQ(1, o[3]);
}

TEST(second_pass_stops_at_head_change)
{
    void *q;
    int o[8];
    reset(0, 3);
    set(0, 10, 7, 1, 1);
    set(1, 11, 7, 1, 2);
    set(2, 12, 7, 2, 9);        /* different head ends the scan */
    set(3, 13, 7, 1, 8);
    q = chain(4);
    DISK_$SORT(&vol, &q);
    ASSERT_EQ(4, order(q, o));
    ASSERT_EQ(0, o[0]); ASSERT_EQ(1, o[1]); ASSERT_EQ(2, o[2]); ASSERT_EQ(3, o[3]);
}

TEST(bat_step_one_skips_second_pass)
{
    void *q;
    int o[8];
    reset(0, 1);
    set(0, 10, 7, 1, 1);
    set(1, 11, 7, 1, 2);
    set(2, 12, 7, 1, 4);
    q = chain(3);
    DISK_$SORT(&vol, &q);
    ASSERT_EQ(3, order(q, o));
    ASSERT_EQ(0, o[0]); ASSERT_EQ(1, o[1]); ASSERT_EQ(2, o[2]);
}

TEST(single_request)
{
    void *q;
    int o[8];
    reset(0, 3);
    set(0, 10, 7, 1, 1);
    q = chain(1);
    DISK_$SORT(&vol, &q);
    ASSERT_EQ(1, order(q, o));
    ASSERT_EQ(0, o[0]);
}

int main(void)
{
    printf("test_sort:\n");
    RUN_TEST(sorts_by_lba);
    RUN_TEST(sorts_by_daddr_for_bit9_drivers);
    RUN_TEST(adjacent_swap_keeps_chain_intact);
    RUN_TEST(second_pass_pulls_far_sector_forward);
    RUN_TEST(second_pass_stops_at_head_change);
    RUN_TEST(bat_step_one_skips_second_pass);
    RUN_TEST(single_request);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
