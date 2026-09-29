/*
 * netbuf/test/test_add_pages.c - Unit tests for NETBUF_$ADD_PAGES
 * (0x00E0E928).
 *
 * The test compiles the real netbuf/add_pages.c against scripted versions of
 * the spin lock, WP_$CALLOC_LIST, NETBUF_$GETVA, NETBUF_$DEL_PAGES and
 * CRASH_SYSTEM, plus a real MMAPE array and a real 1KB-per-buffer arena, so
 * the free-list threading and the header-buffer initialisation can be checked
 * against the addresses the original writes.
 */

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_failed = 0;
static int tests_run = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  Running %s... ", #name);                                    \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("done\n");                                                     \
    } while (0)

#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long _e = (long long)(expected);                                 \
        long long _a = (long long)(actual);                                   \
        if (_e != _a) {                                                       \
            printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",  \
                   (unsigned long long)_e, (unsigned long long)_a, __LINE__); \
            tests_failed++;                                                   \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ==========================================================================
 * Globals and mocks the code under test links against
 * ========================================================================== */

#include "netbuf/netbuf_internal.h"

/* The netbuf globals. */
MODULE_DATA_DEFINE(netbuf_globals_t, NETBUF_$DATA, 0x00E245A8);
#define globals_storage NETBUF_$DATA

/* The MMAPE table; only the word at entry offset 0x06 is used here. */
#define TEST_MMAPE_ENTRIES 512
static mmape_t mmape_storage[TEST_MMAPE_ENTRIES];
mmape_t *mmap_mmape_base = mmape_storage;

status_$t netbuf_err = 0x00110001;
uint16_t NETBUF_$DELAY_TYPE = 0;

/*
 * The header-buffer arena.  ARCH_HOST_VA_BASE (arch/host/arch.h) makes
 * ARCH_VA_TO_PTR resolve a target VA into it, so the code under test can write
 * through the 1KB buffers by VA exactly as it does on the target.
 */
#define TEST_ARENA_PAGES 16
static uint8_t arena[(TEST_ARENA_PAGES + 1) * NETBUF_HDR_SIZE];

/* Scripted WP_$CALLOC_LIST: hands out page numbers from calloc_pages[]. */
static int      calloc_calls;
static int16_t  calloc_count_seen;
static uint32_t calloc_pages[NETBUF_MAX_ALLOC];

void WP_$CALLOC_LIST(int16_t count, uint32_t *ppn_arr)
{
    int i;

    calloc_calls++;
    calloc_count_seen = count;
    for (i = 0; i < count; i++) {
        ppn_arr[i] = calloc_pages[i];
    }
}

/* NETBUF_$GETVA maps ppn<<10 to the arena slot for that page. */
static int       getva_calls;
static status_$t getva_status;
static uint32_t  getva_pa_seen[NETBUF_MAX_ALLOC];

void NETBUF_$GETVA(uint32_t ppn_shifted, uint32_t *va_out, status_$t *status)
{
    if (getva_calls < NETBUF_MAX_ALLOC) {
        getva_pa_seen[getva_calls] = ppn_shifted;
    }
    getva_calls++;
    /*
     * Slot n of the arena for the n-th request, counting from ONE: virtual
     * address zero is nil on the target, and ARCH_VA_TO_PTR maps it to NULL.
     */
    *va_out = (uint32_t)(getva_calls * NETBUF_HDR_SIZE);
    *status = getva_status;
}

static int      del_pages_calls;
static int16_t  del_hdr_seen;
static int16_t  del_dat_seen;

void NETBUF_$DEL_PAGES(int16_t hdr_count, int16_t dat_count)
{
    del_pages_calls++;
    del_hdr_seen = hdr_count;
    del_dat_seen = dat_count;
}

/*
 * CRASH_SYSTEM halts the machine, so the mock must not return into the caller
 * the way a plain stub would - NETBUF_$ADD_PAGES would then run
 * WP_$CALLOC_LIST past the end of its 0x80-entry page array.
 */
static int      crash_calls;
static jmp_buf  crash_jmp;
static status_$t crash_status_seen;

void CRASH_SYSTEM(const status_$t *status)
{
    crash_calls++;
    crash_status_seen = *status;
    longjmp(crash_jmp, 1);
}

#define ADD_PAGES(h, d)                                                       \
    do {                                                                      \
        if (setjmp(crash_jmp) == 0) {                                         \
            NETBUF_$ADD_PAGES((h), (d));                              \
        }                                                                     \
    } while (0)

static int              lock_calls;
static int              unlock_calls;
static ml_$spin_token_t next_token;

ml_$spin_token_t ML_$SPIN_LOCK(void *lockp)
{
    (void)lockp;
    lock_calls++;
    return next_token;
}

void ML_$SPIN_UNLOCK(void *lockp, ml_$spin_token_t token)
{
    (void)lockp;
    (void)token;
    unlock_calls++;
}

#include "../add_pages.c"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static void reset_state(void)
{
    int i;

    memset(&globals_storage, 0, sizeof(globals_storage));
    memset(mmape_storage, 0, sizeof(mmape_storage));
    memset(arena, 0xA5, sizeof(arena));

    for (i = 0; i < NETBUF_MAX_ALLOC; i++) {
        calloc_pages[i] = (uint32_t)(0x100 + i);
    }

    calloc_calls = 0;
    calloc_count_seen = -1;
    getva_calls = 0;
    getva_status = status_$ok;
    del_pages_calls = 0;
    crash_calls = 0;
    lock_calls = 0;
    unlock_calls = 0;
    next_token = 0x1234;

    ARCH_HOST_VA_BASE = (uintptr_t)arena;

    globals_storage.dat_lim = 1000;
    globals_storage.dat_cnt = 0;
    globals_storage.hdr_alloc = 0;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * 0x00E0E94E / 0x00E0E972: both counts are clamped, and 0x00E0E99A charges
 * hdr_alloc the REQUESTED count rather than the clamped one.
 */
TEST(counts_are_clamped_but_hdr_alloc_takes_the_request)
{
    reset_state();
    globals_storage.hdr_alloc = NETBUF_HDR_MAX - 2;   /* room for two more */
    globals_storage.dat_lim = 5;
    globals_storage.dat_cnt = 3;                      /* room for two more */

    ADD_PAGES(10, 10);

    /* two header pages + two data pages */
    ASSERT_EQ(1, calloc_calls);
    ASSERT_EQ(4, calloc_count_seen);
    ASSERT_EQ(2, getva_calls);

    /* hdr_alloc took the full request of 10 */
    ASSERT_EQ(NETBUF_HDR_MAX + 8, globals_storage.hdr_alloc);

    /* dat_cnt took the clamped 2 */
    ASSERT_EQ(5u, globals_storage.dat_cnt);
    ASSERT_EQ(0, del_pages_calls);
}

/* 0x00E0E94E "cmpi.w #0xb0,(0x334,A5) / blt" - at or over the ceiling, no
 * header pages at all. */
TEST(header_pool_at_ceiling_takes_none)
{
    reset_state();
    globals_storage.hdr_alloc = NETBUF_HDR_MAX;

    ADD_PAGES(4, 3);

    ASSERT_EQ(3, calloc_count_seen);
    ASSERT_EQ(0, getva_calls);
    /* still charged the request */
    ASSERT_EQ(NETBUF_HDR_MAX + 4, globals_storage.hdr_alloc);
}

/* 0x00E0E9B0 "tst.w D2w / ble" - nothing to do means no allocation at all. */
TEST(zero_total_allocates_nothing)
{
    reset_state();

    ADD_PAGES(0, 0);

    ASSERT_EQ(0, calloc_calls);
    ASSERT_EQ(0, getva_calls);
    ASSERT_EQ(0u, globals_storage.hdr_top);
    ASSERT_EQ(0u, globals_storage.dat_top);
    ASSERT_EQ(0, del_pages_calls);
}

/*
 * 0x00E0EA1A - 0x00E0EA5C: every header buffer gets 0x3E8..0x3FB zeroed, its
 * physical address at 0x3FC and is pushed onto the free list, so the list ends
 * up in reverse order of allocation.
 */
TEST(header_buffers_are_initialised_and_pushed)
{
    uint32_t va0, va1;

    reset_state();
    globals_storage.hdr_top = 0xDEAD0000u;

    ADD_PAGES(2, 0);

    ASSERT_EQ(2, getva_calls);
    ASSERT_EQ((0x100u) << 10, getva_pa_seen[0]);
    ASSERT_EQ((0x101u) << 10, getva_pa_seen[1]);

    va0 = 1 * NETBUF_HDR_SIZE;
    va1 = 2 * NETBUF_HDR_SIZE;

    /* 0x3E8 .. 0x3FB cleared */
    ASSERT_EQ(0u, NETBUF_HDR_FIELD(va0, 0x3E8));
    ASSERT_EQ(0u, NETBUF_HDR_FIELD(va0, 0x3EC));
    ASSERT_EQ(0u, NETBUF_HDR_FIELD(va0, 0x3F0));
    ASSERT_EQ(0u, NETBUF_HDR_FIELD(va0, 0x3F4));
    ASSERT_EQ(0u, NETBUF_HDR_FIELD(va0, 0x3F8));

    /* 0x3FC is the page's physical address */
    ASSERT_EQ((0x100u) << 10, NETBUF_HDR_PHYS(va0));
    ASSERT_EQ((0x101u) << 10, NETBUF_HDR_PHYS(va1));

    /* pushed in order, so the head is the last one */
    ASSERT_EQ(va1, globals_storage.hdr_top);
    ASSERT_EQ(va0, NETBUF_HDR_NEXT(va1));
    ASSERT_EQ(0xDEAD0000u, NETBUF_HDR_NEXT(va0));

    /* 0x3E4 is NOT part of the cleared range - it is the link */
    ASSERT_EQ(NETBUF_HDR_NEXT_OFF, 0x3E4);
}

/*
 * 0x00E0EA98 - 0x00E0EAAC and 0x00E0EADA/0x00E0EAE8: the data pages are
 * chained FORWARD - pages[i] links to pages[i+1] - and the last one links to
 * the previous list head, which becomes reachable from the new head
 * pages[hdr_take].
 */
TEST(data_pages_are_chained_forward)
{
    uint32_t p0, p1, p2;

    reset_state();
    globals_storage.dat_top = 0x0077;

    /* one header page first, so the data pages start at index 1 */
    ADD_PAGES(1, 3);

    p0 = calloc_pages[1];
    p1 = calloc_pages[2];
    p2 = calloc_pages[3];

    ASSERT_EQ(p0, globals_storage.dat_top);
    ASSERT_EQ(p1, NETBUF_DAT_NEXT(p0));
    ASSERT_EQ(p2, NETBUF_DAT_NEXT(p1));
    ASSERT_EQ(0x0077, NETBUF_DAT_NEXT(p2));

    ASSERT_EQ(3u, globals_storage.dat_cnt);
}

/* A single data page: the chaining loop body never runs, but the tail link and
 * the head assignment still happen. */
TEST(single_data_page)
{
    uint32_t p0;

    reset_state();
    globals_storage.dat_top = 0x0055;

    ADD_PAGES(0, 1);

    p0 = calloc_pages[0];
    ASSERT_EQ(p0, globals_storage.dat_top);
    ASSERT_EQ(0x0055, NETBUF_DAT_NEXT(p0));
    ASSERT_EQ(1u, globals_storage.dat_cnt);
}

/*
 * The free-list link is the MMAPE word at entry offset 0x06 (0xEB4800 - 0x1FFA
 * == MMAPE base + 6), not next_vpn at 0x0A.
 */
TEST(free_list_link_is_at_mmape_offset_6)
{
    uint32_t p0;
    const uint8_t *entry;

    reset_state();
    ADD_PAGES(0, 2);

    p0 = calloc_pages[0];
    entry = (const uint8_t *)&mmape_storage[p0];

    /* the link we wrote must be readable as the big-endian-agnostic field at 6 */
    ASSERT_EQ((uintptr_t)&NETBUF_DAT_NEXT(p0), (uintptr_t)(entry + 6));
    ASSERT_EQ(0u, mmape_storage[p0].next_vpn);   /* 0x0A untouched */
}

/*
 * 0x00E0EB06 - 0x00E0EB1A: overshooting the data limit trims with a NEGATIVE
 * data count and a zero header count.
 */
TEST(overshoot_trims_with_a_negative_count)
{
    reset_state();
    globals_storage.dat_lim = 4;
    globals_storage.dat_cnt = 0;

    /* Ask for 10; the clamp allows 4, which exactly fills the pool. */
    ADD_PAGES(0, 10);
    ASSERT_EQ(4u, globals_storage.dat_cnt);
    ASSERT_EQ(0, del_pages_calls);

    /*
     * Now force the overshoot the way NETBUF_$RTN_DAT can: raise dat_cnt above
     * dat_lim before the final test by shrinking the limit mid-flight is not
     * possible from here, so drive the same arithmetic directly.
     */
    reset_state();
    globals_storage.dat_lim = 10;
    globals_storage.dat_cnt = 12;   /* already over: the clamp yields <= 0 */
    ADD_PAGES(0, 5);

    ASSERT_EQ(1, del_pages_calls);
    ASSERT_EQ(0, del_hdr_seen);
    ASSERT_EQ(-2, del_dat_seen);    /* (uint16)10 - (uint16)12 */
}

/* 0x00E0E988 "cmpi.w #0x80,D2w / ble" - more than 0x80 pages at once crashes. */
TEST(total_over_max_crashes)
{
    reset_state();
    globals_storage.dat_lim = 100000;
    globals_storage.dat_cnt = 0;

    ADD_PAGES(NETBUF_HDR_MAX, 100);

    ASSERT_EQ(1, crash_calls);
}

/* 0x00E0EA06 "tst.l (-0x214,A6) / beq" - a GETVA failure crashes. */
TEST(getva_failure_crashes)
{
    reset_state();
    getva_status = 0x00110003;

    ADD_PAGES(1, 0);

    ASSERT_EQ(1, crash_calls);
}

int main(void)
{
    printf("NETBUF_$ADD_PAGES tests\n");

    RUN_TEST(counts_are_clamped_but_hdr_alloc_takes_the_request);
    RUN_TEST(header_pool_at_ceiling_takes_none);
    RUN_TEST(zero_total_allocates_nothing);
    RUN_TEST(header_buffers_are_initialised_and_pushed);
    RUN_TEST(data_pages_are_chained_forward);
    RUN_TEST(single_data_page);
    RUN_TEST(free_list_link_is_at_mmape_offset_6);
    RUN_TEST(overshoot_trims_with_a_negative_count);
    RUN_TEST(total_over_max_crashes);
    RUN_TEST(getva_failure_crashes);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
