/*
 * disk/test/test_chksum_page.c - unit tests for disk_$chksum_page (0x00e0a290)
 *
 * The real disk/chksum_page.c is #included below.  The four MMU entry points
 * and CHKSUM_$GET_CHKSUM are mocked so the order of the calls, the scratch
 * virtual address and the mapping restore can all be observed.
 */

#include "disk/disk_internal.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
 * Test harness
 * ================================================================ */
static int tests_failed = 0;
static int tests_run = 0;

#define RUN_TEST(name) do {                     \
    tests_run++;                                \
    printf("  Running %s... ", #name);          \
    fflush(stdout);                             \
    if (test_##name() == 0) printf("PASSED\n"); \
} while (0)

#define CHECK_EQ(expected, actual) do {                             \
    unsigned long e_ = (unsigned long)(expected);                   \
    unsigned long a_ = (unsigned long)(actual);                     \
    if (e_ != a_) {                                                 \
        printf("FAILED\n    %s: expected 0x%lx, got 0x%lx at %s:%d\n", \
               #actual, e_, a_, __FILE__, __LINE__);                \
        tests_failed++;                                             \
        return 1;                                                   \
    }                                                               \
} while (0)

/* ================================================================
 * Globals and mocks
 * ================================================================ */
ml_$exclusion_t ml_$exclusion_t_00e7a274;
ml_$exclusion_t MOUNT_LOCK;

#define MAX_EVENTS 8

typedef enum {
    EV_PTOV,
    EV_INSTALL,
    EV_SUM,
    EV_REMOVE,
    EV_CLR_USED
} event_kind_t;

static event_kind_t events[MAX_EVENTS];
static uint32_t ev_ppn[MAX_EVENTS];
static uint32_t ev_va[MAX_EVENTS];
static uint32_t ev_flags[MAX_EVENTS];
static int nevents;

static uint32_t mock_ptov_result;
static uint16_t mock_sum;

static void record(event_kind_t k, uint32_t ppn, uint32_t va, uint32_t flags)
{
    if (nevents < MAX_EVENTS) {
        events[nevents] = k;
        ev_ppn[nevents] = ppn;
        ev_va[nevents] = va;
        ev_flags[nevents] = flags;
    }
    nevents++;
}

uint32_t MMU_$PTOV(uint32_t ppn)
{
    record(EV_PTOV, ppn, 0, 0);
    return mock_ptov_result;
}

void MMU_$INSTALL(uint32_t ppn, uint32_t va, uint32_t flags)
{
    record(EV_INSTALL, ppn, va, flags);
}

void MMU_$REMOVE(uint32_t ppn) { record(EV_REMOVE, ppn, 0, 0); }

void MMU_$CLR_USED(uint32_t ppn) { record(EV_CLR_USED, ppn, 0, 0); }

uint16_t CHKSUM_$GET_CHKSUM(const void *va)
{
    record(EV_SUM, 0, (uint32_t)(uintptr_t)va, 0);
    return mock_sum;
}

#include "../chksum_page.c"

/* DISK_$DIAG, DISK_$DO_CHKSUM and the module exclusion lock are cells of the
 * DISK_ module block (disk/disk.h), so the host build provides the block. */
uint8_t DISK_$DATA[DISK_$DATA_SIZE];

static void reset_state(void)
{
    nevents = 0;
    memset(events, 0, sizeof(events));
    memset(ev_ppn, 0, sizeof(ev_ppn));
    memset(ev_va, 0, sizeof(ev_va));
    memset(ev_flags, 0, sizeof(ev_flags));
    mock_ptov_result = 0;
    mock_sum = 0;
}

/* ================================================================
 * Tests
 * ================================================================ */

/*
 * 0x00E0A2D8: a page that was already mapped somewhere is put back at that
 * virtual address afterwards.
 */
static int test_mapped_page_is_restored(void)
{
    uint32_t ppn = 0x1234;
    uint16_t sum;

    reset_state();
    mock_ptov_result = 0x00A00000;
    mock_sum = 0xBEEF;

    sum = disk_$chksum_page(&ppn);

    CHECK_EQ(0xBEEF, sum);
    CHECK_EQ(5, nevents);

    CHECK_EQ(EV_PTOV, events[0]);
    CHECK_EQ(0x1234, ev_ppn[0]);

    CHECK_EQ(EV_INSTALL, events[1]);
    CHECK_EQ(0x1234, ev_ppn[1]);
    CHECK_EQ(DISK_CHKSUM_SCRATCH_VA, ev_va[1]);
    CHECK_EQ(DISK_CHKSUM_MMU_FLAGS, ev_flags[1]);

    CHECK_EQ(EV_SUM, events[2]);
    CHECK_EQ(DISK_CHKSUM_SCRATCH_VA, ev_va[2]);

    CHECK_EQ(EV_INSTALL, events[3]);
    CHECK_EQ(0x1234, ev_ppn[3]);
    CHECK_EQ(0x00A00000, ev_va[3]);
    CHECK_EQ(DISK_CHKSUM_MMU_FLAGS, ev_flags[3]);

    CHECK_EQ(EV_CLR_USED, events[4]);
    CHECK_EQ(0x1234, ev_ppn[4]);
    return 0;
}

/*
 * 0x00E0A2F0: a page that was not mapped anywhere has the scratch mapping
 * removed instead of restored.
 */
static int test_unmapped_page_is_removed(void)
{
    uint32_t ppn = 0x99;
    uint16_t sum;

    reset_state();
    mock_ptov_result = 0;
    mock_sum = 0x0001;

    sum = disk_$chksum_page(&ppn);

    CHECK_EQ(0x0001, sum);
    CHECK_EQ(5, nevents);
    CHECK_EQ(EV_INSTALL, events[1]);
    CHECK_EQ(EV_SUM, events[2]);
    CHECK_EQ(EV_REMOVE, events[3]);
    CHECK_EQ(0x99, ev_ppn[3]);
    CHECK_EQ(EV_CLR_USED, events[4]);
    return 0;
}

/* 0x00E0A298: the page number is read through the pointer, once */
static int test_ppn_is_read_through_the_pointer(void)
{
    uint32_t ppn = 0x00FFFFFF;

    reset_state();
    mock_ptov_result = 0;
    mock_sum = 0;

    (void)disk_$chksum_page(&ppn);

    CHECK_EQ(0x00FFFFFF, ev_ppn[0]);
    CHECK_EQ(0x00FFFFFF, ev_ppn[1]);
    CHECK_EQ(0x00FFFFFF, ev_ppn[3]);
    CHECK_EQ(0x00FFFFFF, ev_ppn[4]);
    return 0;
}

int main(void)
{
    printf("disk_$chksum_page (0x00e0a290) tests\n");

    RUN_TEST(mapped_page_is_restored);
    RUN_TEST(unmapped_page_is_removed);
    RUN_TEST(ppn_is_read_through_the_pointer);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
