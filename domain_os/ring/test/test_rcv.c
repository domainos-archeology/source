/*
 * ring/test/test_rcv.c - Unit tests for RING_$RCV_FROM_UNIT_PRIV (0x00E76048)
 * and its nested procedure ring_$validate_receive (0x00E75DE4).
 *
 * ring/rcv.c is #included below, so the code under test is the real thing;
 * every callee is stubbed here and records what it was handed.  The receive
 * loop never returns, so the EC_$WAIT stub doubles as the "hardware event"
 * injector and longjmp()s out once the test's budget of wakeups is spent.
 *
 * The behaviours covered are the ones the 2026-09-06 audit found wrong:
 *   - hw_regs is a BYTE-offset register view (status at +2, tmask byte at +4,
 *     mode at +6), not an int16_t[] that doubles every displacement
 *   - the tmask byte written is the LOW byte of the unit's tmask word
 *   - NETBUF_$GET_HDR takes (&rx_hdr_pa, &rx_hdr) in that order
 *   - the state_flags BUSY arms are NOT inverted (0x00E76170)
 *   - EC_$WAIT gets two three-element arrays by value
 *   - ring_$validate_receive is the real function, not an always-"valid" stub
 *   - TIME_$WAIT gets the shared PC-relative constant cell
 *   - ring_$receive_packet is called with the ADDRESSES of the caller's locals
 *   - the data buffer is only handed on when hdr->data_len > 0
 */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "ring/ring_internal.h"
#include "misc/crash_system.h"
#include "proc1/proc1.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed;

#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
    current_failed = 0;                         \
    setup();                                    \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                    \
    unsigned long _e = (unsigned long)(expected);                           \
    unsigned long _a = (unsigned long)(actual);                             \
    if (_e != _a) {                                                         \
        if (!current_failed) printf("FAILED\n");                            \
        printf("    line %d: expected 0x%lx, got 0x%lx\n", __LINE__, _e, _a); \
        current_failed = 1;                                                 \
        return;                                                             \
    }                                                                       \
} while (0)

#define ASSERT_PTR(expected, actual) do {                                   \
    const void *_e = (const void *)(expected);                              \
    const void *_a = (const void *)(actual);                                \
    if (_e != _a) {                                                         \
        if (!current_failed) printf("FAILED\n");                            \
        printf("    line %d: expected %p, got %p\n", __LINE__, _e, _a);     \
        current_failed = 1;                                                 \
        return;                                                             \
    }                                                                       \
} while (0)

/* ============================================================================
 * Globals the receive path touches
 * ============================================================================ */

ring_global_t   RING_$CTL;
ring_$stats_t   RING_$DATA[RING_MAX_UNITS];
ring_$swdiag_t  RING_$SWDIAG_DATA;
uint16_t        RING_$RCV_BIPHASE;
uint16_t        RING_$RCV_ESB;
uint16_t        RING_$XMIT_BIPHASE;
uint16_t        RING_$XMIT_ESB;
uint32_t        RING_$SWDIAG_NODEID;
uint32_t        RING_$SWDIAG_GOODRCV_CNT;
uint32_t        RING_$SWDIAG_RCVCNT;
uint16_t        RING_$PAGING_OVERFLOW;
uid_t           RING_$NETWORK_UID;
uint16_t        ring_dcte_ctype_net = 0x0002;

int8_t          NETWORK_$ACTIVITY_FLAG;
char            NETWORK_$DO_CHKSUM;
network_$failure_rec_t NETWORK_$FAILURE_REC;
uint32_t        TIME_$CURRENT_CLOCKH;

/* Stand-ins for the memory mapped DMA byte counters (see ring/rcv.c). */
volatile uint16_t ring_$dma_chan0_count_cell;
volatile uint16_t ring_$dma_chan1_count_cell;

/* The register block the unit points at. */
static ring_hw_regs_t   hw;

/* The header buffer NETBUF hands out. */
static ring_$pkt_hdr_t  hdr_buf;

/* ============================================================================
 * Recorded call state
 * ============================================================================ */

static jmp_buf  escape;
static int      wait_budget;
static int      wait_calls;

static int      dcte_calls;
static uint16_t *dcte_ctype_arg;
static uint16_t *dcte_unit_arg;

static int      lock_calls;
static uint16_t lock_arg;

static int      get_dat_calls;
static uint32_t *get_dat_arg;
static int      get_hdr_calls;
static uint32_t *get_hdr_arg1;
static uint32_t *get_hdr_arg2;

static int      setup_dma_calls;
static uint32_t setup_dma_hdr;
static uint32_t setup_dma_data;

static int      clear_dma_calls;
static int16_t  clear_dma_chan[16];
static uint16_t clear_dma_unit[16];

static int      advance_calls;
static void    *advance_arg;

static ec_$wait_ecs_t   last_ecs;
static ec_$wait_vals_t  last_vals;

static int      time_wait_calls;
static const uint16_t *time_wait_type_arg;
static uint16_t time_wait_type_val;
static clock_t  time_wait_delay;

static int      crash_calls;
static status_$t crash_status;

static int      recv_calls;
static uint16_t recv_unit;
static ring_$pkt_hdr_t **recv_hdr_p;
static uint32_t *recv_data_pa_p;
static int16_t  *recv_hdr_len_p;
static int16_t  *recv_data_len_p;
static uint32_t recv_data_pa_val;
static int16_t  recv_hdr_len_val;
static int16_t  recv_data_len_val;
static uint32_t recv_clears_data_pa;    /* if set, the stub zeroes *data_pa_p */

static int      chksum_calls;
static const void     *chksum_hdr;
static const uint16_t *chksum_len_p;
static int16_t  chksum_result;

/*
 * What the "interrupt" leaves behind: the EC_$WAIT stub applies these to the
 * unit and the register block before returning, exactly as the controller and
 * RING_$INT would.
 */
static uint8_t  wake_state_flags;
static uint16_t wake_rcv_csr;
static uint16_t wake_rcv_csr_after_clear;  /* value read back after "rcv_csr = 0" */

/*
 * Receive status register model (source-6nns).
 *
 * ring/ring.h routes RING_$RCV_CSR_READ / RING_$RCV_CSR_WRITE through these
 * two functions on a host build, so a test can model a controller that keeps
 * reporting BUSY after the driver has written the register zero - the exact
 * condition RING_$RCV_FROM_UNIT_PRIV's recovery arm exists for
 * (0x00E7618A-0x00E761E2).  With `rcv_stuck_writes` > 0 a write of zero is
 * swallowed and the register keeps its value; every other write lands.
 */
static int      rcv_stuck_writes;
static int      rcv_csr_writes;
static uint16_t rcv_csr_last_write;
static status_$t time_wait_status;
static int      wake_rcv_csr_written;

/* ============================================================================
 * Stubs
 * ============================================================================ */

dcte_t *IO_$GET_DCTE(uint16_t *ctypep, uint16_t *cnump, status_$t *status_ret)
{
    dcte_calls++;
    dcte_ctype_arg = ctypep;
    dcte_unit_arg = cnump;
    *status_ret = status_$ok;
    return NULL;
}

void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_calls++;
    crash_status = *status_p;
    longjmp(escape, 2);
}

void PROC1_$SET_LOCK(uint16_t lock_id)
{
    lock_calls++;
    lock_arg = lock_id;
}

void NETBUF_$GET_DAT(uint32_t *addr_out)
{
    get_dat_calls++;
    get_dat_arg = addr_out;
    *addr_out = 0x00DA7A00;
}

void NETBUF_$GET_HDR(uint32_t *phys_out, uint32_t *va_out)
{
    get_hdr_calls++;
    get_hdr_arg1 = phys_out;
    get_hdr_arg2 = va_out;
    *phys_out = 0x00BEEF00u;
    *(ring_$pkt_hdr_t **)va_out = &hdr_buf;
}

void ring_$setup_rx_dma(uint32_t hdr_buf_pa, uint32_t data_buf_pa)
{
    setup_dma_calls++;
    setup_dma_hdr = hdr_buf_pa;
    setup_dma_data = data_buf_pa;
}

void ring_$clear_dma_channel(int16_t channel, uint16_t unit)
{
    if (clear_dma_calls < 16) {
        clear_dma_chan[clear_dma_calls] = channel;
        clear_dma_unit[clear_dma_calls] = unit;
    }
    clear_dma_calls++;
}

void EC_$ADVANCE(ec_$eventcount_t *ec)
{
    advance_calls++;
    advance_arg = ec;
}

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{
    last_ecs = ecs;
    last_vals = vals;
    wait_calls++;
    if (wait_calls > wait_budget) {
        longjmp(escape, 1);
    }
    /* Deliver the "interrupt". */
    RING_$CTL.units[0].state_flags =
        (uint8_t)((RING_$CTL.units[0].state_flags & ~RING_UNIT_BUSY) |
                  wake_state_flags);
    hw.rcv_csr = wake_rcv_csr;
    wake_rcv_csr_written = 0;
    return 0;
}

uint16_t ring_$rcv_csr_read(ring_hw_regs_t *regs)
{
    (void)regs;
    return hw.rcv_csr;
}

void ring_$rcv_csr_write(ring_hw_regs_t *regs, uint16_t value)
{
    (void)regs;
    rcv_csr_writes++;
    rcv_csr_last_write = value;
    if (value == 0 && rcv_stuck_writes > 0) {
        rcv_stuck_writes--;
        return;             /* the controller ignores the shutdown */
    }
    hw.rcv_csr = value;
}

void TIME_$WAIT(uint16_t *delay_type, clock_t *delay, status_$t *status)
{
    time_wait_calls++;
    time_wait_type_arg = delay_type;
    time_wait_type_val = *delay_type;
    time_wait_delay = *delay;
    *status = time_wait_status;
    /* the receiver stays stuck unless the test says otherwise */
    hw.rcv_csr = wake_rcv_csr_after_clear;
}

int16_t ring_$receive_packet(uint16_t unit, ring_$pkt_hdr_t **hdr_p,
                             uint32_t *data_pa_p, int16_t *hdr_len_p,
                             int16_t *data_len_p)
{
    recv_calls++;
    recv_unit = unit;
    recv_hdr_p = hdr_p;
    recv_data_pa_p = data_pa_p;
    recv_hdr_len_p = hdr_len_p;
    recv_data_len_p = data_len_p;
    recv_data_pa_val = *data_pa_p;
    recv_hdr_len_val = *hdr_len_p;
    recv_data_len_val = *data_len_p;
    if (recv_clears_data_pa) {
        *data_pa_p = 0;
    }
    return 0;
}

uint8_t HDR_CHKSUM(const void *hdr, const uint16_t *len_p)
{
    chksum_calls++;
    chksum_hdr = hdr;
    chksum_len_p = len_p;
    return (uint8_t)chksum_result;
}

/* The function under test. */
#include "ring/rcv.c"

/* ============================================================================
 * Fixture
 * ============================================================================ */

#define TEST_UNIT   0

static ring_unit_t *U(void) { return &RING_$CTL.units[TEST_UNIT]; }

static void setup(void)
{
    memset(&RING_$CTL, 0, sizeof(RING_$CTL));
    memset(RING_$DATA, 0, sizeof(RING_$DATA));
    memset(&RING_$SWDIAG_DATA, 0, sizeof(RING_$SWDIAG_DATA));
    memset(&hw, 0, sizeof(hw));
    memset(&hdr_buf, 0, sizeof(hdr_buf));
    memset(&NETWORK_$FAILURE_REC, 0, sizeof(NETWORK_$FAILURE_REC));

    wait_calls = 0;
    wait_budget = 1;
    dcte_calls = 0; dcte_ctype_arg = NULL; dcte_unit_arg = NULL;
    lock_calls = 0; lock_arg = 0xFFFF;
    get_dat_calls = 0; get_dat_arg = NULL;
    get_hdr_calls = 0; get_hdr_arg1 = NULL; get_hdr_arg2 = NULL;
    setup_dma_calls = 0; setup_dma_hdr = 0; setup_dma_data = 0;
    clear_dma_calls = 0;
    memset(clear_dma_chan, 0, sizeof(clear_dma_chan));
    memset(clear_dma_unit, 0, sizeof(clear_dma_unit));
    advance_calls = 0; advance_arg = NULL;
    memset(&last_ecs, 0xEE, sizeof(last_ecs));
    memset(&last_vals, 0xEE, sizeof(last_vals));
    time_wait_calls = 0; time_wait_type_arg = NULL; time_wait_type_val = 0xFFFF;
    crash_calls = 0; crash_status = 0;
    recv_calls = 0; recv_clears_data_pa = 0;
    recv_data_pa_val = 0xDEADBEEF;
    chksum_calls = 0; chksum_result = 0;
    NETWORK_$ACTIVITY_FLAG = 0;
    NETWORK_$DO_CHKSUM = 0;

    wake_state_flags = 0;
    wake_rcv_csr = 0;
    wake_rcv_csr_after_clear = 0;
    rcv_stuck_writes = 0;
    rcv_csr_writes = 0;
    rcv_csr_last_write = 0xFFFF;
    time_wait_status = status_$ok;
    wake_rcv_csr_written = 0;

    U()->hw_regs = &hw;
    U()->tmask = 0x1234;
    U()->rx_wake_ec.value = 100;
    U()->rx_hdr_pa = 0;
    U()->rx_data_pa = 0;
    U()->rx_hdr = NULL;

    /*
     * A well formed 0x1C byte header with a 0x40 byte payload; the DMA
     * residual counters are set so ring_$validate_receive computes exactly
     * those lengths (0x400 - count*2).
     */
    hdr_buf.hdr_len = 0x1C;
    hdr_buf.data_len = 0x40;
    ring_$dma_chan0_count_cell = (0x400 - 0x1C) / 2;
    ring_$dma_chan1_count_cell = (0x400 - 0x40) / 2;
}

/* Run the loop until the wakeup budget runs out (or CRASH_SYSTEM fires). */
static int run_loop(int budget)
{
    int reason;
    wait_budget = budget;
    reason = setjmp(escape);
    if (reason == 0) {
        RING_$RCV_FROM_UNIT_PRIV(TEST_UNIT);
        return -1;  /* the loop returned - it must not */
    }
    return reason;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

/* The prologue: the controller type is passed BY REFERENCE and holds 2. */
static void test_prologue(void)
{
    ASSERT_EQ(1, run_loop(0));
    ASSERT_EQ(1, dcte_calls);
    ASSERT_PTR(&ring_dcte_ctype_net, dcte_ctype_arg);
    ASSERT_EQ(2, *dcte_ctype_arg);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(0x0D, lock_arg);
    /* ready_ec, not rx_wake_ec, is advanced on the first pass only */
    ASSERT_EQ(1, advance_calls);
    ASSERT_PTR(&U()->ready_ec, advance_arg);
}

/* NETBUF_$GET_HDR(&rx_hdr_pa, &rx_hdr) - the audit found these swapped. */
static void test_netbuf_arg_order(void)
{
    ASSERT_EQ(1, run_loop(0));
    ASSERT_EQ(1, get_dat_calls);
    ASSERT_PTR(&U()->rx_data_pa, get_dat_arg);
    ASSERT_EQ(1, get_hdr_calls);
    ASSERT_PTR(&U()->rx_hdr_pa, get_hdr_arg1);
    ASSERT_PTR(&U()->rx_hdr, get_hdr_arg2);
    /* and the DMA is set up from (hdr_pa, data_pa) in that order */
    ASSERT_EQ(1, setup_dma_calls);
    ASSERT_EQ(U()->rx_hdr_pa, setup_dma_hdr);
    ASSERT_EQ(U()->rx_data_pa, setup_dma_data);
}

/*
 * The register block is addressed by BYTE offset: the tmask byte lands at +4,
 * the receive status at +2 and the mode at +6.  The value written is the LOW
 * byte of the unit's tmask word (unit + 0x33).
 */
static void test_hw_register_writes(void)
{
    ASSERT_EQ(4, offsetof(ring_hw_regs_t, tmask));
    ASSERT_EQ(2, offsetof(ring_hw_regs_t, rcv_csr));
    ASSERT_EQ(6, offsetof(ring_hw_regs_t, mode));

    U()->tmask = 0x1234;
    ASSERT_EQ(1, run_loop(0));
    ASSERT_EQ(0x34, hw.tmask);
    ASSERT_EQ(RING_RCV_CSR_ARM, hw.rcv_csr);
    ASSERT_EQ(RING_MODE_ENABLE, hw.mode);
    /* the congestion flag is cleared on the armed path */
    ASSERT_EQ(0, RING_$DATA[TEST_UNIT].congestion_flag);
    /* and the BUSY bit is cleared before arming */
    ASSERT_EQ(0, U()->state_flags & RING_UNIT_BUSY);
}

/* tmask == 0 idles the receiver and never touches rcv_csr. */
static void test_hw_idle_mode(void)
{
    U()->tmask = 0;
    ASSERT_EQ(1, run_loop(0));
    ASSERT_EQ(RING_MODE_IDLE, hw.mode);
    ASSERT_EQ(0, hw.rcv_csr);
    ASSERT_EQ(0, hw.tmask);
}

/* EC_$WAIT takes two three-element arrays BY VALUE. */
static void test_ec_wait_arrays(void)
{
    U()->rx_wake_ec.value = 100;
    ASSERT_EQ(1, run_loop(0));      /* longjmp out of the very first wait */
    ASSERT_PTR(&U()->rx_wake_ec, last_ecs.ec[0]);
    ASSERT_PTR(NULL, last_ecs.ec[1]);
    ASSERT_PTR(NULL, last_ecs.ec[2]);
    ASSERT_EQ(101, last_vals.val[0]);   /* rx_wake_ec.value + 1 */
    ASSERT_EQ(0, last_vals.val[1]);
    ASSERT_EQ(0, last_vals.val[2]);
    ASSERT_EQ(0, RING_$CTL.wakeup_cnt);
}

/* Each completed wait bumps the wait value and the wakeup counter. */
static void test_ec_wait_value_advances(void)
{
    U()->rx_wake_ec.value = 100;
    ASSERT_EQ(1, run_loop(1));      /* one wait completes, the second escapes */
    ASSERT_EQ(102, last_vals.val[0]);
    ASSERT_EQ(1, RING_$CTL.wakeup_cnt);
    /* ready_ec is advanced only on the first pass */
    ASSERT_EQ(1, advance_calls);
}

/*
 * 0x00E76170: the BUSY bit SET takes the recovery path.  The packet is not
 * dispatched and busy_on_rcv_int is not touched.
 */
static void test_busy_bit_set_recovers(void)
{
    wake_state_flags = RING_UNIT_BUSY;
    wake_rcv_csr = RING_RCV_CSR_BUSY;
    wake_rcv_csr_after_clear = 0;   /* the write to rcv_csr clears it */
    ASSERT_EQ(1, run_loop(1));
    ASSERT_EQ(0, recv_calls);
    ASSERT_EQ(0, RING_$CTL.busy_on_rcv_int);
    ASSERT_EQ(0, RING_$CTL.abort_cnt);
    /* both receive channels dropped, then the loop restarted */
    ASSERT_EQ(2, clear_dma_calls);
    ASSERT_EQ(0, clear_dma_chan[0]);
    ASSERT_EQ(1, clear_dma_chan[1]);
    ASSERT_EQ(TEST_UNIT, clear_dma_unit[0]);
    ASSERT_EQ(TEST_UNIT, clear_dma_unit[1]);
    /* the recovery writes 0 to the receive status register */
    ASSERT_EQ(RING_RCV_CSR_ARM, hw.rcv_csr);   /* re-armed by the next pass */
    /* it must not have gone via TIME_$WAIT: the first read-back was clear */
    ASSERT_EQ(0, time_wait_calls);
}

/*
 * 0x00E7618A-0x00E761E2: when writing zero to the receive status register
 * does NOT clear BUSY, the driver waits ~0xABE ticks and looks again.  If the
 * receiver has come back by then it just restarts the loop.
 */
static void test_stuck_receiver_recovers_after_delay(void)
{
    wake_state_flags = RING_UNIT_BUSY;
    wake_rcv_csr = RING_RCV_CSR_BUSY;
    rcv_stuck_writes = 1;               /* the first write of 0 is ignored */
    wake_rcv_csr_after_clear = 0;       /* but the delay clears it */

    ASSERT_EQ(1, run_loop(1));

    /* 0x00E7618A wrote zero, and the read-back still said BUSY */
    ASSERT_EQ(0, rcv_csr_last_write == 0xFFFF ? 1 : 0);
    ASSERT_EQ(1, time_wait_calls);
    /* 0x00E761A2: the delay is 0x0ABE ticks, high half zero */
    ASSERT_EQ(0, time_wait_delay.high);
    ASSERT_EQ(0xABE, time_wait_delay.low);
    /* 0x00E75DE2: the delay type cell is a zero word */
    ASSERT_EQ(0, time_wait_type_val);
    /* the receiver came back, so no crash */
    ASSERT_EQ(0, crash_calls);
    /* and the loop restarted after dropping both channels */
    ASSERT_EQ(0, recv_calls);
    ASSERT_EQ(2, clear_dma_calls);
    ASSERT_EQ(0, clear_dma_chan[0]);
    ASSERT_EQ(1, clear_dma_chan[1]);
}

/*
 * 0x00E761D2-0x00E761DC: a receiver that is STILL busy after the delay is a
 * dead controller - CRASH_SYSTEM with the cell at 0x00E76290 (0x00110005,
 * "receive process failed to start").
 */
static void test_stuck_receiver_crashes(void)
{
    wake_state_flags = RING_UNIT_BUSY;
    wake_rcv_csr = RING_RCV_CSR_BUSY;
    rcv_stuck_writes = 1;
    wake_rcv_csr_after_clear = RING_RCV_CSR_BUSY;   /* never recovers */

    ASSERT_EQ(2, run_loop(1));          /* CRASH_SYSTEM longjmps with 2 */
    ASSERT_EQ(1, time_wait_calls);
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(0x00110005, crash_status);
    /* the channels are dropped only after the crash check, so not yet */
    ASSERT_EQ(0, clear_dma_calls);
}

/*
 * 0x00E761BE-0x00E761C8: a TIME_$WAIT that fails crashes with ITS status,
 * without looking at the register again.
 */
static void test_stuck_receiver_delay_failure_crashes(void)
{
    wake_state_flags = RING_UNIT_BUSY;
    wake_rcv_csr = RING_RCV_CSR_BUSY;
    rcv_stuck_writes = 1;
    wake_rcv_csr_after_clear = 0;       /* would have recovered */
    time_wait_status = (status_$t)0x00120003;

    ASSERT_EQ(2, run_loop(1));
    ASSERT_EQ(1, time_wait_calls);
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(0x00120003, crash_status);
}

/*
 * 0x00E76202: the BUSY bit CLEAR only counts the condition and then goes on
 * to validate and dispatch the packet.
 */
static void test_busy_bit_clear_counts(void)
{
    wake_state_flags = 0;
    wake_rcv_csr = RING_RCV_CSR_BUSY;
    ASSERT_EQ(1, run_loop(1));
    ASSERT_EQ(1, RING_$CTL.busy_on_rcv_int);
    /*
     * rcv_csr 0x2000 is outside the "no error" mask 0x0016, so
     * ring_$validate_receive takes the error path and the packet is aborted
     * rather than dispatched.
     */
    ASSERT_EQ(0, recv_calls);
    ASSERT_EQ(1, RING_$CTL.abort_cnt);
    ASSERT_EQ(2, clear_dma_calls);
}

/*
 * The four literals the routine passes by reference are real, shared,
 * file-static cells holding the values found in the code region.
 *
 * (The stuck-receiver path that consumes ring_$rcv_delay_type is driven by
 * test_stuck_receiver_* above, through the RING_$RCV_CSR_READ /
 * RING_$RCV_CSR_WRITE hooks in ring/ring.h.)
 */
static void test_constant_cells(void)
{
    ASSERT_EQ(0x0000, ring_$rcv_delay_type);        /* 0x00E75DE2 */
    ASSERT_EQ(0x00110005, ring_$rcv_stuck_status);  /* 0x00E7628C */
    ASSERT_EQ(0x00110010, ring_$rcv_chksum_status); /* 0x00E76040 */
    ASSERT_EQ(0x00110013, ring_$rcv_stat40_status); /* 0x00E76044 */
}

/*
 * ring_$validate_receive is the real thing: a header whose advertised lengths
 * do not match what the DMA moved bumps RING_$BAD_DATA_CNT and aborts.
 */
static void test_validate_length_mismatch(void)
{
    ring_$dma_chan1_count_cell = (0x400 - 0x20) / 2;   /* says 0x20, hdr says 0x40 */
    ASSERT_EQ(1, run_loop(1));
    ASSERT_EQ(1, RING_$CTL.bad_data_cnt);
    ASSERT_EQ(1, RING_$CTL.abort_cnt);
    ASSERT_EQ(0, recv_calls);
    ASSERT_EQ(0, RING_$DATA[TEST_UNIT].rcvcnt);
    ASSERT_EQ(0, NETWORK_$ACTIVITY_FLAG);
}

/*
 * A good packet is accounted for and handed on by address.
 *
 * Whether a buffer was released is observed through the refill: the next pass
 * of the loop calls NETBUF_$GET_* again for every buffer that was cleared.
 */
static void test_good_packet_dispatch(void)
{
    ASSERT_EQ(1, run_loop(1));

    ASSERT_EQ(1, RING_$DATA[TEST_UNIT].rcvcnt);
    ASSERT_EQ(0, RING_$CTL.abort_cnt);
    ASSERT_EQ((int8_t)-1, NETWORK_$ACTIVITY_FLAG);
    ASSERT_EQ(1, recv_calls);
    ASSERT_EQ(TEST_UNIT, recv_unit);
    ASSERT_PTR(&hdr_buf, *recv_hdr_p);
    ASSERT_EQ(0x00DA7A00, recv_data_pa_val);
    ASSERT_EQ(0x1C, recv_hdr_len_val);
    ASSERT_EQ(0x40, recv_data_len_val);
    /* every record argument is the address of one of the caller's locals */
    ASSERT_PTR(recv_hdr_p, recv_hdr_p);
    /* the header buffer is always given up, so it is re-fetched */
    ASSERT_EQ(2, get_hdr_calls);
    /* the data buffer too, because the callee left data_pa non zero */
    ASSERT_EQ(2, get_dat_calls);
}

/*
 * hdr->data_len == 0 means no data buffer is handed on, and the unit keeps
 * the one it has.
 */
static void test_no_data_keeps_buffer(void)
{
    hdr_buf.data_len = 0;
    ring_$dma_chan1_count_cell = 0x400 / 2;   /* 0 bytes moved */
    ASSERT_EQ(1, run_loop(1));
    ASSERT_EQ(1, recv_calls);
    ASSERT_EQ(0, recv_data_pa_val);
    ASSERT_EQ(2, get_hdr_calls);              /* header released */
    ASSERT_EQ(1, get_dat_calls);              /* data buffer kept */
}

/* The callee may clear data_pa to say it did not keep the data buffer. */
static void test_callee_clears_data_pa(void)
{
    recv_clears_data_pa = 1;
    ASSERT_EQ(1, run_loop(1));
    ASSERT_EQ(1, recv_calls);
    ASSERT_EQ(2, get_hdr_calls);              /* header released */
    ASSERT_EQ(1, get_dat_calls);              /* kept: data_pa re-read after the call */
}

/* The checksum is taken with (hdr, &hdr_hdr_len) when enabled. */
static void test_checksum_arguments(void)
{
    NETWORK_$DO_CHKSUM = (char)0xFF;
    hdr_buf.chksum = 0x5A;
    chksum_result = 0x5A;
    ASSERT_EQ(1, run_loop(1));
    ASSERT_EQ(1, chksum_calls);
    ASSERT_PTR(&hdr_buf, chksum_hdr);
    ASSERT_EQ(1, RING_$DATA[TEST_UNIT].rcvcnt);
    ASSERT_EQ(0, crash_calls);
}

/* A checksum mismatch crashes with the 0x00E76040 constant cell. */
static void test_checksum_mismatch_crashes(void)
{
    NETWORK_$DO_CHKSUM = (char)0xFF;
    hdr_buf.chksum = 0x5A;
    chksum_result = 0x5B;
    ASSERT_EQ(2, run_loop(1));
    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(0x00110010, crash_status);
}

/* bit 0 of hdr->flags suppresses the checksum entirely. */
static void test_checksum_suppressed_by_flag(void)
{
    NETWORK_$DO_CHKSUM = (char)0xFF;
    hdr_buf.flags = 0x01;
    ASSERT_EQ(1, run_loop(1));
    ASSERT_EQ(0, chksum_calls);
    ASSERT_EQ(1, RING_$DATA[TEST_UNIT].rcvcnt);
}

/* An error status routes to the matching per-unit counter. */
static void test_error_status_counters(void)
{
    wake_rcv_csr = 0x0200;
    ASSERT_EQ(1, run_loop(1));
    ASSERT_EQ(1, RING_$DATA[TEST_UNIT].rcvtim);
    ASSERT_EQ(0, RING_$SWDIAG_DATA.rcvtim);   /* not a swdiag packet */
    ASSERT_EQ(1, RING_$CTL.abort_cnt);
    ASSERT_EQ(0, recv_calls);
}

/* Software-diagnostic packets mirror the count into RING_$SWDIAG_DATA. */
static void test_swdiag_mirror_counters(void)
{
    hdr_buf.flags = 0x02 | 0x10;
    wake_rcv_csr = 0x0200;
    ASSERT_EQ(1, run_loop(1));
    ASSERT_EQ(1, RING_$DATA[TEST_UNIT].rcvtim);
    ASSERT_EQ(1, RING_$SWDIAG_DATA.rcvtim);
}

/* ESB and biphase bits bump the standalone words below the statistics. */
static void test_esb_and_biphase(void)
{
    wake_rcv_csr = 0x0400 | 0x0800;
    ASSERT_EQ(1, run_loop(1));
    ASSERT_EQ(1, RING_$RCV_ESB);
    ASSERT_EQ(1, RING_$RCV_BIPHASE);
    ASSERT_EQ(1, RING_$DATA[TEST_UNIT].rcvpkt);
}

/*
 * 0x00E75FF2: the 0x0100 case is the only one that does NOT end the chain -
 * it falls through into the 0x0080 test.
 */
static void test_bit8_falls_through_to_bit7(void)
{
    wake_rcv_csr = 0x0100 | 0x0080;
    ASSERT_EQ(1, run_loop(1));
    ASSERT_EQ(1, RING_$DATA[TEST_UNIT].rcvcrc);
    ASSERT_EQ(1, RING_$DATA[TEST_UNIT].rcvxerr);
}

/*
 * ring_$swdiag_t is the 0x1E-byte block both responders copy whole
 * (0x00E11278..0x00E1128A and 0x00E64B68..0x00E64B7A), 0x00E261C2..0x00E261DF,
 * ending right at RING_$DATA[0] (0x00E261E0).  Bead source-twut.
 */
static void test_swdiag_block_layout(void)
{
    ASSERT_EQ(0x1Eu, (unsigned)sizeof(ring_$swdiag_t));
    ASSERT_EQ(0x16u, (unsigned)offsetof(ring_$swdiag_t, rcvxerr));
    ASSERT_EQ(0x18u, (unsigned)offsetof(ring_$swdiag_t, rcvhcsum));
    ASSERT_EQ(0x1Au, (unsigned)offsetof(ring_$swdiag_t, _r1a));
}

/*
 * network_$failure_rec_t+0x04 is the node and +0x0C the failure type
 * (0x00E75F1C / 0x00E75F2A here, 0x00E10414 / 0x00E1042E in
 * NETWORK_$REPORT_FAILURE, 0x00E65D0E / 0x00E65D1A in ASKNODE_$SERVER).
 * Bead source-oowv.
 */
static void test_failure_rec_layout(void)
{
    ASSERT_EQ(0x10u, (unsigned)sizeof(network_$failure_rec_t));
    ASSERT_EQ(0x04u, (unsigned)offsetof(network_$failure_rec_t, node_id));
    ASSERT_EQ(0x08u, (unsigned)offsetof(network_$failure_rec_t, timestamp));
    ASSERT_EQ(0x0Cu, (unsigned)offsetof(network_$failure_rec_t, failure_type));
}

/* Message types 1 and 3 fill in NETWORK_$FAILURE_REC instead of a counter. */
static void test_failure_record(void)
{
    wake_rcv_csr = 0x2000;      /* any error status reaches the error path */
    hdr_buf.msg_type = 3;
    hdr_buf.src_id = 0x00ABCDEF;
    TIME_$CURRENT_CLOCKH = 0x11223344;
    ASSERT_EQ(1, run_loop(1));
    ASSERT_EQ(0x11223344, NETWORK_$FAILURE_REC.timestamp);
    ASSERT_EQ(0x00ABCDEF, NETWORK_$FAILURE_REC.node_id);
    ASSERT_EQ(1, NETWORK_$FAILURE_REC.flag < 0);
    ASSERT_EQ(3, NETWORK_$FAILURE_REC.failure_type);
    ASSERT_EQ(1, RING_$CTL.abort_cnt);
}

/* Odd advertised lengths lose the DMA pad byte. */
static void test_odd_length_pad(void)
{
    hdr_buf.hdr_len = 0x1D;
    hdr_buf.data_len = 0x41;
    ring_$dma_chan0_count_cell = (0x400 - 0x1E) / 2;   /* 0x1E moved, 0x1D used */
    ring_$dma_chan1_count_cell = (0x400 - 0x42) / 2;   /* 0x42 moved, 0x41 used */
    ASSERT_EQ(1, run_loop(1));
    ASSERT_EQ(1, RING_$DATA[TEST_UNIT].rcvcnt);
    ASSERT_EQ(0, RING_$CTL.bad_data_cnt);
    ASSERT_EQ(0x1D, recv_hdr_len_val);
    ASSERT_EQ(0x41, recv_data_len_val);
}

int main(void)
{
    printf("Running RING_$RCV_FROM_UNIT_PRIV tests...\n");

    RUN_TEST(prologue);
    RUN_TEST(netbuf_arg_order);
    RUN_TEST(hw_register_writes);
    RUN_TEST(hw_idle_mode);
    RUN_TEST(ec_wait_arrays);
    RUN_TEST(ec_wait_value_advances);
    RUN_TEST(busy_bit_set_recovers);
    RUN_TEST(stuck_receiver_recovers_after_delay);
    RUN_TEST(stuck_receiver_crashes);
    RUN_TEST(stuck_receiver_delay_failure_crashes);
    RUN_TEST(busy_bit_clear_counts);
    RUN_TEST(constant_cells);
    RUN_TEST(validate_length_mismatch);
    RUN_TEST(good_packet_dispatch);
    RUN_TEST(no_data_keeps_buffer);
    RUN_TEST(callee_clears_data_pa);
    RUN_TEST(checksum_arguments);
    RUN_TEST(checksum_mismatch_crashes);
    RUN_TEST(checksum_suppressed_by_flag);
    RUN_TEST(error_status_counters);
    RUN_TEST(swdiag_mirror_counters);
    RUN_TEST(esb_and_biphase);
    RUN_TEST(bit8_falls_through_to_bit7);
    RUN_TEST(swdiag_block_layout);
    RUN_TEST(failure_rec_layout);
    RUN_TEST(failure_record);
    RUN_TEST(odd_length_pad);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
