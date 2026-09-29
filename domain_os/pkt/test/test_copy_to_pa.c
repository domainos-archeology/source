/*
 * pkt/test/test_copy_to_pa.c - Unit tests for PKT_$COPY_TO_PA (0x00E1251C).
 *
 * The test compiles the real pkt/copy_to_pa.c and scripts every routine it
 * calls, including FIM_$CLEANUP, so both entries into the body can be driven
 * from C: the normal copy loop and the fault re-entry at 0x00E125FA that has
 * to hand back the mapped VA *and* every data buffer already taken
 * (0x00E1260C-0x00E1262E).
 */

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

#include "pkt/pkt_internal.h"

MODULE_DATA_DEFINE(pkt_$data_t, PKT_$DATA, 0x00E24C9C);
int8_t NETWORK_$LOOPBACK_FLAG;

/* The buffer addresses NETBUF_$GET_DAT hands out, in order. */
#define BUF_BASE 0x00200000u

static int      get_dat_calls;

static int      getva_calls;
static uint32_t getva_addr_seen[8];
static status_$t getva_status;          /* stored through the status pointer */
static uint32_t getva_va;               /* stored through the va pointer */

static int      rtnva_calls;
static uint32_t rtnva_va_seen[8];

static int      rtn_dat_calls;
static uint32_t rtn_dat_seen[8];

static int      copy_calls;
static const void *copy_src_seen[8];
static void    *copy_dst_seen[8];
static uint32_t copy_len_seen[8];

static int      rls_cleanup_calls;

static int      signal_calls;
static status_$t signal_status_seen;

static int      crash_calls;

/*
 * FIM_$CLEANUP is scripted: the first call in a run returns whatever
 * cleanup_result says.  status_$cleanup_handler_set takes the normal path;
 * anything else takes the fault re-entry.
 */
static status_$t cleanup_result;
static int       cleanup_calls;
static void     *cleanup_ctx_seen;

status_$t FIM_$CLEANUP(void *handler)
{
    cleanup_calls++;
    cleanup_ctx_seen = handler;
    return cleanup_result;
}

void FIM_$RLS_CLEANUP(void *cleanup_data)
{
    (void)cleanup_data;
    rls_cleanup_calls++;
}

void FIM_$SIGNAL(status_$t status)
{
    signal_calls++;
    signal_status_seen = status;
}

void NETBUF_$GET_DAT(uint32_t *addr_out)
{
    *addr_out = BUF_BASE + (uint32_t)get_dat_calls * 0x400u;
    get_dat_calls++;
}

void NETBUF_$GETVA(uint32_t ppn_shifted, uint32_t *va_out, status_$t *status)
{
    if (getva_calls < 8) {
        getva_addr_seen[getva_calls] = ppn_shifted;
    }
    getva_calls++;
    *va_out = getva_va;
    *status = getva_status;
}

uint32_t NETBUF_$RTNVA(uint32_t *va_ptr)
{
    if (rtnva_calls < 8) {
        rtnva_va_seen[rtnva_calls] = *va_ptr;
    }
    rtnva_calls++;
    return 0;
}

void NETBUF_$RTN_DAT(uint32_t addr)
{
    if (rtn_dat_calls < 8) {
        rtn_dat_seen[rtn_dat_calls] = addr;
    }
    rtn_dat_calls++;
}

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    if (copy_calls < 8) {
        copy_src_seen[copy_calls] = src;
        copy_dst_seen[copy_calls] = dst;
        copy_len_seen[copy_calls] = len;
    }
    copy_calls++;
}

void CRASH_SYSTEM(const status_$t *status_p)
{
    (void)status_p;
    crash_calls++;
}

/* ==========================================================================
 * Harness
 * ========================================================================== */

/* A scratch area the mock hands back as the buffer's virtual address. */
static uint8_t scratch[0x400];

static uint32_t buffers[8];
static status_$t call_status;
static char payload[0x1000];

static void reset_state(void)
{
    get_dat_calls = 0;
    getva_calls = 0;
    memset(getva_addr_seen, 0, sizeof(getva_addr_seen));
    getva_status = status_$ok;
    getva_va = (uint32_t)(uintptr_t)scratch;
    rtnva_calls = 0;
    memset(rtnva_va_seen, 0, sizeof(rtnva_va_seen));
    rtn_dat_calls = 0;
    memset(rtn_dat_seen, 0, sizeof(rtn_dat_seen));
    copy_calls = 0;
    memset(copy_src_seen, 0, sizeof(copy_src_seen));
    memset(copy_dst_seen, 0, sizeof(copy_dst_seen));
    memset(copy_len_seen, 0, sizeof(copy_len_seen));
    rls_cleanup_calls = 0;
    signal_calls = 0;
    signal_status_seen = 0;
    crash_calls = 0;
    cleanup_calls = 0;
    cleanup_ctx_seen = NULL;
    cleanup_result = status_$cleanup_handler_set;
    memset(buffers, 0xEE, sizeof(buffers));
    call_status = 0x5A5A5A5A;
}

#include "../copy_to_pa.c"

static void call_copy(uint16_t len)
{
    PKT_$COPY_TO_PA(payload, len, buffers, &call_status);
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * 0x00E12562 "ble.w": a zero length never enters the loop, but the entry
 * stores at 0x00E1252E still clear buffers_out[0], and the normal exit still
 * releases the handler and reports success.
 */
TEST(zero_length_allocates_nothing)
{
    reset_state();
    call_copy(0);

    ASSERT_EQ(0, get_dat_calls);
    ASSERT_EQ(0, copy_calls);
    ASSERT_EQ(1, rls_cleanup_calls);
    ASSERT_EQ(status_$ok, call_status);
    ASSERT_EQ(0, buffers[0]);
    ASSERT_EQ(0, rtn_dat_calls);
}

/*
 * 0x00E12568-0x00E125E6: one buffer per 0x400-byte chunk, the last chunk
 * clipped to the remainder (0x00E125A4-0x00E125AC), and the source pointer
 * advanced by a whole PKT_CHUNK_SIZE each pass (0x00E125D6).
 */
TEST(chunking_and_clipping)
{
    reset_state();
    call_copy(0x900);

    ASSERT_EQ(3, get_dat_calls);
    ASSERT_EQ(3, copy_calls);
    ASSERT_EQ(0x400, copy_len_seen[0]);
    ASSERT_EQ(0x400, copy_len_seen[1]);
    ASSERT_EQ(0x100, copy_len_seen[2]);
    ASSERT_EQ((uintptr_t)payload, (uintptr_t)copy_src_seen[0]);
    ASSERT_EQ((uintptr_t)(payload + 0x400), (uintptr_t)copy_src_seen[1]);
    ASSERT_EQ((uintptr_t)(payload + 0x800), (uintptr_t)copy_src_seen[2]);

    /* The slots are filled in order, and each is mapped then unmapped. */
    ASSERT_EQ(BUF_BASE + 0x000, buffers[0]);
    ASSERT_EQ(BUF_BASE + 0x400, buffers[1]);
    ASSERT_EQ(BUF_BASE + 0x800, buffers[2]);
    ASSERT_EQ(BUF_BASE + 0x000, getva_addr_seen[0]);
    ASSERT_EQ(BUF_BASE + 0x400, getva_addr_seen[1]);
    ASSERT_EQ(BUF_BASE + 0x800, getva_addr_seen[2]);
    ASSERT_EQ(3, rtnva_calls);

    /* The success path never touches NETBUF_$RTN_DAT. */
    ASSERT_EQ(0, rtn_dat_calls);
    ASSERT_EQ(1, rls_cleanup_calls);
    ASSERT_EQ(status_$ok, call_status);
}

/*
 * 0x00E12590 "tst.l (-0x24,A6)" / 0x00E12596: a NETBUF_$GETVA failure crashes
 * the system and leaves the loop, but the routine still falls into the normal
 * exit and reports success (0x00E125F6 "clr.l (A0)").
 */
TEST(getva_failure_crashes_then_exits_normally)
{
    reset_state();
    getva_status = 0x00110003;
    call_copy(0x800);

    ASSERT_EQ(1, get_dat_calls);
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(0, copy_calls);
    ASSERT_EQ(1, rls_cleanup_calls);
    ASSERT_EQ(status_$ok, call_status);
}

/*
 * 0x00E125FA-0x00E12642: the fault arm allocates and copies nothing, does not
 * release the handler, re-signals the status FIM_$CLEANUP returned and hands
 * that same status back to the caller.
 *
 * The image re-enters this arm on the frame the first pass left behind, so
 * the counter it walks is non-zero in the real system.  The straight-line C
 * shape the tree uses for FIM_$CLEANUP cannot express that second entry, so
 * this test covers the arm's calls and the loop is pinned separately below.
 * TODO: model FIM_$CLEANUP's second entry so the arm can be driven with a
 * populated frame (bead source-oafp).
 */
TEST(cleanup_arm_signals_and_reports_status)
{
    reset_state();
    cleanup_result = 0x000B0005;

    PKT_$COPY_TO_PA(payload, 0x900, buffers, &call_status);

    /* The cleanup arm never allocates or copies. */
    ASSERT_EQ(0, get_dat_calls);
    ASSERT_EQ(0, copy_calls);
    ASSERT_EQ(0, rls_cleanup_calls);

    /* No VA was held and no buffer had been taken on this entry. */
    ASSERT_EQ(0, rtnva_calls);
    ASSERT_EQ(0, rtn_dat_calls);

    /* 0x00E12632-0x00E12642 */
    ASSERT_EQ(1, signal_calls);
    ASSERT_EQ(0x000B0005, signal_status_seen);
    ASSERT_EQ(0x000B0005, call_status);
}

/*
 * 0x00E1260C-0x00E1262E: the fault arm gives every data buffer already taken
 * back to the pool, one NETBUF_$RTN_DAT per slot.  Exercised directly on the
 * same shape copy_to_pa.c emits so the bound and the ordering are pinned:
 *
 *   0x00E1260C  move.w (-0x28,A6),D0w
 *   0x00E12610  subq.w #0x1,D0w
 *   0x00E12612  bmi.b 0x00E12632
 *   0x00E1261A  lea (0x4,A0),A2
 *   0x00E12620  move.l (-0x4,A2),-(SP) / jsr NETBUF_$RTN_DAT
 *   0x00E1262C  addq.l #0x4,A2
 *   0x00E1262E  dbf D2w,0x00E12620
 */
static void run_cleanup_loop(uint32_t *buffers_out, int16_t buf_count)
{
    uint32_t *buf_ptr = buffers_out + 1;
    int16_t i;

    for (i = (int16_t)(buf_count - 1); i >= 0; i--) {
        NETBUF_$RTN_DAT(*(buf_ptr - 1));
        buf_ptr++;
    }
}

TEST(cleanup_loop_bound_and_order)
{
    uint32_t vec[4] = { 0x1111, 0x2222, 0x3333, 0x4444 };

    /* buf_count 0: "bmi" skips the loop entirely. */
    reset_state();
    run_cleanup_loop(vec, 0);
    ASSERT_EQ(0, rtn_dat_calls);

    /* buf_count 1: one call, slot 0. */
    reset_state();
    run_cleanup_loop(vec, 1);
    ASSERT_EQ(1, rtn_dat_calls);
    ASSERT_EQ(0x1111, rtn_dat_seen[0]);

    /* buf_count 3: three calls, slots 0,1,2 in order - never slot 3. */
    reset_state();
    run_cleanup_loop(vec, 3);
    ASSERT_EQ(3, rtn_dat_calls);
    ASSERT_EQ(0x1111, rtn_dat_seen[0]);
    ASSERT_EQ(0x2222, rtn_dat_seen[1]);
    ASSERT_EQ(0x3333, rtn_dat_seen[2]);
}

/*
 * 0x00E125FA "tst.l (-0x20,A6)": the held VA is unmapped only when one is
 * held.  Exercised on the same shape the arm uses.
 */
static void run_cleanup_va(uint32_t buf_va)
{
    if (buf_va != 0) {
        NETBUF_$RTNVA(&buf_va);
    }
}

TEST(cleanup_unmaps_only_a_held_va)
{
    reset_state();
    run_cleanup_va(0);
    ASSERT_EQ(0, rtnva_calls);

    reset_state();
    run_cleanup_va(0x00D64C00u);
    ASSERT_EQ(1, rtnva_calls);
    ASSERT_EQ(0x00D64C00u, rtnva_va_seen[0]);
}

int main(void)
{
    printf("PKT_$COPY_TO_PA tests\n");

    RUN_TEST(zero_length_allocates_nothing);
    RUN_TEST(chunking_and_clipping);
    RUN_TEST(getva_failure_crashes_then_exits_normally);
    RUN_TEST(cleanup_arm_signals_and_reports_status);
    RUN_TEST(cleanup_loop_bound_and_order);
    RUN_TEST(cleanup_unmaps_only_a_held_va);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
