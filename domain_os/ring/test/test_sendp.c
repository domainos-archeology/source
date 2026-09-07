/*
 * ring/test/test_sendp.c - unit tests for RING_$SENDP
 *
 * Compiles the real ring/sendp.c and drives it with a synthetic unit record,
 * statistics block and register file.  The point of the tests is the parts
 * beads source-bwuv and source-x1es were about: the poll/deadline loop, the
 * three TIME_$WAIT2 setups and their delay-type constants, and the result
 * WORD masks (the original writes them with byte operations on the even and
 * odd halves of the word at (A3)).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

#include "ring/ring_internal.h"

/* ==========================================================================
 * Globals the code under test links against
 * ========================================================================== */

/* The register file, declared up here because the mocks below observe it. */
static ring_hw_regs_t regs;

ring_global_t RING_$DATA;
ring_$stats_t RING_$STATS[RING_MAX_UNITS];
uint16_t RING_$XMIT_BIPHASE;
uint16_t RING_$XMIT_ESB;
char NETWORK_$DO_CHKSUM;
int8_t NETWORK_$ACTIVITY_FLAG;

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

/* The real 48-bit helpers, reproduced so the test stays one program. */
void ADD48(clock_t *dst, clock_t *src)
{
    uint32_t low = (uint32_t)dst->low + (uint32_t)src->low;
    dst->low = (uint16_t)low;
    dst->high = dst->high + src->high + (low >> 16);
}

int8_t SUB48(clock_t *dst, clock_t *src)
{
    uint16_t dst_low = dst->low;
    uint16_t src_low = src->low;

    dst->low = (uint16_t)(dst_low - src_low);
    dst->high = dst->high - src->high - (dst_low < src_low ? 1u : 0u);
    return ((int32_t)dst->high >= 0) ? (int8_t)-1 : 0;
}

/*
 * A clock that advances by one tick per read, so the poll loop terminates.
 *
 * The transmit CSR is memory-backed here, so a test cannot make a read return
 * something different from the last value the code wrote.  The hook below is
 * how a scripted status gets in: TIME_$ABS_CLOCK runs at 0xE75A4E, right
 * after "move.w #0x6000,(A4)", so installing the value there is the first
 * chance the test gets after the start command.  Each mock also snapshots the
 * CSR as it found it, which is how the command writes are observed.
 */
#define ABS_CLOCK_MAX 8
static clock_t mock_clock;
static int abs_clock_calls;
static uint16_t abs_clock_csr[ABS_CLOCK_MAX];
static uint16_t csr_script;
static int csr_script_active;

void TIME_$ABS_CLOCK(clock_t *clock)
{
    if (abs_clock_calls < ABS_CLOCK_MAX) {
        abs_clock_csr[abs_clock_calls] = regs.xmit_csr;
    }
    abs_clock_calls++;
    *clock = mock_clock;
    mock_clock.low = (uint16_t)(mock_clock.low + 1);
    if (csr_script_active) {
        regs.xmit_csr = csr_script;
    }
}

/* Every TIME_$WAIT2 call, in order. */
#define WAIT2_MAX 8
static int wait2_calls;
static uint16_t wait2_delay_type[WAIT2_MAX];
static clock_t wait2_delay[WAIT2_MAX];
static uint32_t wait2_count[WAIT2_MAX];
static int8_t wait2_result[WAIT2_MAX];
static uint16_t wait2_csr_seen[WAIT2_MAX];

int8_t TIME_$WAIT2(uint16_t *delay_type, clock_t *delay, void *extra_ec,
                   uint32_t *count, status_$t *status)
{
    int i = wait2_calls;

    (void)extra_ec;
    if (i < WAIT2_MAX) {
        wait2_delay_type[i] = *delay_type;
        wait2_delay[i] = *delay;
        wait2_count[i] = *count;
        wait2_csr_seen[i] = regs.xmit_csr;
    }
    wait2_calls++;
    *status = status_$ok;
    return (i < WAIT2_MAX) ? wait2_result[i] : (int8_t)-1;
}

static int wait_calls;
static uint16_t wait_delay_type;
static clock_t wait_delay;

void TIME_$WAIT(uint16_t *delay_type, clock_t *delay, status_$t *status)
{
    wait_calls++;
    wait_delay_type = *delay_type;
    wait_delay = *delay;
    *status = status_$ok;
}

static int tx_dma_calls;
static int tx_dma_bump_ec;      /* advance the transmit eventcount per call */
static uint32_t tx_dma_hdr_pa;
static int16_t tx_dma_hdr_len;
static uint32_t tx_dma_data_pa;
static int16_t tx_dma_data_len;

void ring_$setup_tx_dma(uint32_t hdr_pa, int16_t hdr_len,
                        uint32_t data_pa, int16_t data_len)
{
    tx_dma_calls++;
    if (tx_dma_bump_ec) {
        RING_$DATA.units[1].tx_ec.value++;
    }
    tx_dma_hdr_pa = hdr_pa;
    tx_dma_hdr_len = hdr_len;
    tx_dma_data_pa = data_pa;
    tx_dma_data_len = data_len;
}

#define CLEAR_DMA_MAX 8
static int clear_dma_calls;
static uint16_t clear_dma_csr[CLEAR_DMA_MAX];

void ring_$clear_dma_channel(int16_t channel, uint16_t unit)
{
    (void)channel; (void)unit;
    if (clear_dma_calls < CLEAR_DMA_MAX) {
        clear_dma_csr[clear_dma_calls] = regs.xmit_csr;
    }
    clear_dma_calls++;
}

static uint16_t chksum_len_seen;

uint8_t HDR_CHKSUM(const void *hdr, const uint16_t *len_p)
{
    (void)hdr;
    chksum_len_seen = *len_p;
    return 0x5A;
}

static int mcr_calls;

void MMU_$MCR_CHANGE(uint16_t bit) { (void)bit; mcr_calls++; }

static uint32_t parity_result;
static uint32_t parity_ppn1, parity_ppn2;

uint32_t PARITY_$CHK_IO(uint32_t ppn1, uint32_t ppn2)
{
    parity_ppn1 = ppn1;
    parity_ppn2 = ppn2;
    return parity_result;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../sendp.c"

/* ==========================================================================
 * Fixture
 * ========================================================================== */

#define UNIT 1

static ring_$pkt_hdr_t hdr;
static uint32_t data_desc[4];
static uint16_t send_opts;
static uint16_t result_flags;
static status_$t status;

static ring_unit_t *unit_data(void) { return &RING_$DATA.units[UNIT]; }
static ring_$stats_t *stats(void) { return &RING_$STATS[UNIT]; }

static void setup(void)
{
    int i;

    memset(&RING_$DATA, 0, sizeof(RING_$DATA));
    memset(RING_$STATS, 0, sizeof(RING_$STATS));
    memset(&regs, 0, sizeof(regs));
    memset(&hdr, 0, sizeof(hdr));
    memset(data_desc, 0, sizeof(data_desc));

    abs_clock_calls = wait2_calls = wait_calls = 0;
    csr_script = 0;
    csr_script_active = 0;
    tx_dma_bump_ec = 0;
    parity_result = 0;
    memset(clear_dma_csr, 0, sizeof(clear_dma_csr));
    memset(abs_clock_csr, 0, sizeof(abs_clock_csr));
    memset(wait2_csr_seen, 0, sizeof(wait2_csr_seen));
    tx_dma_calls = clear_dma_calls = mcr_calls = 0;
    RING_$XMIT_BIPHASE = RING_$XMIT_ESB = 0;
    NETWORK_$DO_CHKSUM = 0;
    NETWORK_$ACTIVITY_FLAG = 0;
    parity_result = 0;
    send_opts = 0;
    result_flags = 0xFFFF;
    status = -1;
    mock_clock = (clock_t){ 0, 0 };

    for (i = 0; i < WAIT2_MAX; i++) {
        wait2_result[i] = (int8_t)-1;   /* "the eventcount fired" */
    }

    unit_data()->initialized = (boolean)-1;
    unit_data()->hw_regs = &regs;
    unit_data()->tx_ec.value = 0;

    RING_$DATA.max_data_len = 1024;
    /* a zero poll timeout makes the deadline the attempt start, so the very
     * first SUB48 in the poll loop is non-negative and the loop exits at once */
    RING_$DATA.poll_timeout = (clock_t){ 0, 0 };
    RING_$DATA.wait_timeout = (clock_t){ 0, 0x0064 };
    RING_$DATA.xmit_timeout1 = (clock_t){ 0, 0x0011 };
    RING_$DATA.xmit_timeout2 = (clock_t){ 0, 0x0022 };

    regs.mode = 0x2000;         /* ready */

    hdr.msg_type = 2;
    hdr.flags = 0;
}

static void run(uint16_t data_len)
{
    uint16_t unit = UNIT;

    RING_$SENDP(&unit, 0x00010000u, &hdr, 0x0020, data_desc, 0,
                data_len, &send_opts, &result_flags, &status);
}

/* ==========================================================================
 * Refusals
 * ========================================================================== */

/* 0xE7595A: an uninitialised unit sets bit 4 of the result WORD. */
TEST(uninitialised_unit)
{
    setup();
    unit_data()->initialized = 0;

    run(0);

    ASSERT_EQ(0x0010, result_flags);
    ASSERT_EQ(status_$io_controller_not_in_system, status);
    ASSERT_EQ(0, tx_dma_calls);
    /* this arm branches to the epilogue, so last_success is NOT published */
    ASSERT_EQ(0, stats()->last_success);
}

/* 0xE75974 */
TEST(data_too_long)
{
    setup();
    RING_$DATA.max_data_len = 4;

    run(5);

    ASSERT_EQ(0x0000, result_flags);
    ASSERT_EQ(status_$network_data_length_too_large, status);
}

/* 0xE75996: bit 13 of the MODE register, not the transmit CSR. */
TEST(controller_not_ready)
{
    setup();
    regs.mode = 0;

    run(0);

    ASSERT_EQ(0x0010, result_flags);
    ASSERT_EQ(status_$network_transmit_failed, status);
}

/* ==========================================================================
 * The DMA arm and the checksum
 * ========================================================================== */

/* 0xE759D6: a zero data length programs the HEADER address as the data one. */
TEST(zero_length_uses_header_address)
{
    setup();
    unit_data()->tx_ec.value = 5;   /* already complete */
    regs.xmit_csr = 0x0014;

    run(0);

    ASSERT_EQ(1, tx_dma_calls);
    ASSERT_EQ(0x00010000u, tx_dma_hdr_pa);
    ASSERT_EQ(0x0020, tx_dma_hdr_len);
    ASSERT_EQ(0x00010000u, tx_dma_data_pa);
    ASSERT_EQ(0, tx_dma_data_len);
}

TEST(nonzero_length_copies_the_descriptor)
{
    setup();
    unit_data()->tx_ec.value = 5;
    csr_script = 0x0014;
    csr_script_active = 1;
    data_desc[0] = 0x00ABCDE0u;

    run(64);

    ASSERT_EQ(0x00ABCDE0u, tx_dma_data_pa);
    ASSERT_EQ(64, tx_dma_data_len);
}

/*
 * 0xE75A00 "pea (0x14,A6)": HDR_CHKSUM is given the ADDRESS of the hdr_len
 * argument slot, and its result lands in hdr->chksum.
 */
TEST(checksum_uses_the_hdr_len_argument)
{
    setup();
    unit_data()->tx_ec.value = 5;
    csr_script = 0x0014;
    csr_script_active = 1;
    NETWORK_$DO_CHKSUM = (char)-1;

    run(0);

    ASSERT_EQ(0x0020, chksum_len_seen);
    ASSERT_EQ(0x5A, hdr.chksum);
}

TEST(checksum_disabled_writes_one)
{
    setup();
    unit_data()->tx_ec.value = 5;
    csr_script = 0x0014;
    csr_script_active = 1;

    run(0);

    ASSERT_EQ(1, hdr.chksum);
}

/* ==========================================================================
 * The poll loop and the waits (bead source-bwuv)
 * ========================================================================== */

/*
 * 0xE75A78: the eventcount already at the target means the poll loop exits
 * through 0xE75A80 without ever reading the clock a second time.
 */
TEST(poll_sees_immediate_completion)
{
    setup();
    /*
     * The target is tx_ec.value + 1, so "already complete" means the count
     * moved between 0xE75A26 and the 0xE75A7C test; the DMA-setup mock stands
     * in for the transmitter doing that.
     */
    tx_dma_bump_ec = 1;
    csr_script = 0x0014;
    csr_script_active = 1;

    run(0);

    /* one TIME_$ABS_CLOCK for the deadline seed, none for the loop body */
    ASSERT_EQ(1, abs_clock_calls);
    /* and it saw the start command (0xE75A4A "move.w #0x6000,(A4)") */
    ASSERT_EQ(0x6000, abs_clock_csr[0]);
    ASSERT_EQ(0, wait2_calls);
    ASSERT_EQ(0x8000, result_flags);
    ASSERT_EQ(status_$ok, status);
}

/*
 * With a zero poll timeout the deadline equals the attempt start, so the very
 * first SUB48 is non-negative and the loop falls out to the first
 * TIME_$WAIT2, which must use the ABSOLUTE delay type (0x00E75DE0 == 1) and
 * the deadline built from RING_$DATA.wait_timeout.
 */
TEST(poll_timeout_takes_the_absolute_wait)
{
    setup();
    csr_script = 0x0014;
    csr_script_active = 1;
    wait2_result[0] = (int8_t)-1;   /* the eventcount fired */

    run(0);

    ASSERT_EQ(1, wait2_calls);
    ASSERT_EQ(1, wait2_delay_type[0]);          /* absolute */
    ASSERT_EQ(0x0064, wait2_delay[0].low);      /* attempt_start + wait_timeout */
    ASSERT_EQ(1, wait2_count[0]);               /* tx_ec.value + 1 */
    ASSERT_EQ(1, RING_$DATA.xmit_waited);       /* 0xE75AC0 */
    ASSERT_EQ(status_$ok, status);
}

/*
 * 0xE75AF0: when the first absolute wait times out, the second one adds
 * 0..7 to the deadline's HIGH longword, taken from the bottom 32 bits of the
 * attempt start.
 */
TEST(second_absolute_wait_jitters_the_high_longword)
{
    setup();
    csr_script = 0x0014;
    csr_script_active = 1;
    mock_clock = (clock_t){ 0, 0x0005 };        /* low 32 bits & 7 == 5 */
    wait2_result[0] = 0;                        /* timed out */
    wait2_result[1] = (int8_t)-1;

    run(0);

    ASSERT_EQ(2, wait2_calls);
    ASSERT_EQ(1, wait2_delay_type[1]);          /* still absolute */
    ASSERT_EQ(5, wait2_delay[1].high);          /* attempt_start.high + 5 */
    ASSERT_EQ(0x0005, wait2_delay[1].low);
}

/*
 * 0xE75B72: when both absolute waits time out the transmitter is aborted.
 *
 * The "still busy after the abort" arm (0xE75B7E, the third wait on
 * xmit_timeout2, and the 0xE75BAA hardware-error exit) cannot be reached from
 * a memory-backed register file: "move.w #0x4000,(A4)" is immediately
 * followed by "move.w (A4),D0w", so the read can only ever see 0x4000, whose
 * bit 13 is clear.  It would take a register model that can return a value
 * other than the last one written.
 */
TEST(both_absolute_waits_time_out_then_abort)
{
    setup();
    wait2_result[0] = 0;
    wait2_result[1] = 0;

    run(0);

    /* 0xE75B72 wrote the abort just before the 0xE75BC0 clear */
    ASSERT_EQ(0x4000, clear_dma_csr[0]);
    ASSERT_EQ(1, stats()->xmit_tim);            /* the timeout was counted */
}

/*
 * 0xE75BDA-0xE75C14: a first timeout with neither flag set re-arms with the
 * force start, which uses the RELATIVE type and xmit_timeout1.
 */
TEST(retry_pending_takes_the_force_start_arm)
{
    setup();
    wait2_result[0] = 0;
    wait2_result[1] = 0;
    /* the third call is the force-start arm's; let it report completion */
    wait2_result[2] = (int8_t)-1;

    run(0);

    /* the first pass ends at 0xE75C10 with retry_pending set ... */
    ASSERT_EQ(2, tx_dma_calls);                 /* it went round again */
    ASSERT_EQ(1, stats()->xmit_tim);            /* 0xE75BE0 */
    /* ... and the second pass is the force-start arm */
    ASSERT_EQ(3, wait2_calls);
    ASSERT_EQ(0, wait2_delay_type[2]);          /* relative */
    ASSERT_EQ(0x0011, wait2_delay[2].low);      /* xmit_timeout1 */
    ASSERT_EQ(0x7000, wait2_csr_seen[2]);       /* 0xE75B40 */
    /* 0xE75DD2 publishes the force-start flag */
    ASSERT_EQ((int8_t)-1, stats()->biphase_flag);
}

/* ==========================================================================
 * Status decode (bead source-x1es: word masks, not byte views)
 * ========================================================================== */

static void decode(uint16_t csr)
{
    setup();
    unit_data()->tx_ec.value = 5;
    csr_script = csr;
    csr_script_active = 1;
    run(0);
}

TEST(complete_status)
{
    decode(0x0014);

    ASSERT_EQ(0x8000, result_flags);
    ASSERT_EQ(1, stats()->xmitcnt);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ((boolean)-1, NETWORK_$ACTIVITY_FLAG);
    /* 0xE75DC2: D5 was set, so last_success ends up 0 */
    ASSERT_EQ(0, stats()->last_success);
    ASSERT_EQ(1, mcr_calls);
}

TEST(modem_errors_set_two_low_bits)
{
    decode(0x0C00);

    ASSERT_EQ(0x0020 | 0x0004, result_flags & 0x00FF);
    ASSERT_EQ(1, RING_$XMIT_BIPHASE);
    ASSERT_EQ(1, RING_$XMIT_ESB);
    ASSERT_EQ(1, stats()->xmit_modem);
}

/* 0xE75C8C: a bus error PARITY_$CHK_IO blames on us aborts outright. */
TEST(bus_error_owned_by_parity)
{
    setup();
    unit_data()->tx_ec.value = 5;
    csr_script = 0x0040;
    csr_script_active = 1;
    parity_result = 2;
    run(0);

    ASSERT_EQ(status_$network_memory_parity_error_during_transmit, status);
    ASSERT_EQ(0x00010000u >> 10, parity_ppn1);
    ASSERT_EQ(0x00010000u >> 10, parity_ppn2);   /* data_pa == hdr_pa here */
    ASSERT_EQ(0, stats()->xmit_bus);
    ASSERT_EQ(0, wait_calls);
}

/*
 * A bus error PARITY_$CHK_IO does not claim is counted and retried until the
 * budget of twenty runs out (0xE75D94 "subq.w #0x1,D6w").
 */
TEST(bus_error_not_ours_counts_and_retries)
{
    setup();
    unit_data()->tx_ec.value = 5;
    csr_script = 0x0040;
    csr_script_active = 1;
    parity_result = 1;
    run(0);

    ASSERT_EQ(0x0080, result_flags & 0x00FF);
    ASSERT_EQ(0x14, stats()->xmit_bus);          /* one per attempt */
    ASSERT_EQ(0x13, wait_calls);                 /* one fewer wait */
    ASSERT_EQ(0, wait_delay_type);               /* relative */
    ASSERT_EQ(0x01F4, wait_delay.low);           /* the default 500 */
}

/* 0xE75CD8 "bset.b #0x0,(A3)" is bit 8 of the word, not bit 0. */
TEST(overrun_sets_bit_eight)
{
    decode(0x0001);

    /* 0x0200 is the transmit_done "retried" bit (0xE75DCA) */
    ASSERT_EQ(0x0300, result_flags);
    ASSERT_EQ(0x14, stats()->xmit_orun);
}

/* 0xE75CEC "bset.b #0x2,(A3)" is bit 10. */
TEST(no_return_sets_bit_ten)
{
    decode(0x0020);

    ASSERT_EQ(0x0600, result_flags);
    ASSERT_EQ(0x14, stats()->xmit_nortn);
}

/* 0xE75D3A "bset.b #0x6,(A3)" is bit 14, and the delay is reset to 500. */
TEST(wait_ack_sets_bit_fourteen)
{
    decode(0x0002);

    /* this arm is past the 0xE75D0A "st D5b", so transmit_done does NOT set
     * the 0x0200 retried bit */
    ASSERT_EQ(0x4000, result_flags);
    ASSERT_EQ(0, stats()->last_success);
    ASSERT_EQ(0x14, stats()->xmit_wack);
    ASSERT_EQ(0x01F4, wait_delay.low);
}

/*
 * 0xE75D4C: no token bit clear.  A msg_type in 1..4 is reported as delivered;
 * anything else counts a "no acknowledge" and retries after 250 ticks.
 */
TEST(known_msg_type_is_reported_delivered)
{
    setup();
    unit_data()->tx_ec.value = 5;
    hdr.msg_type = 4;
    csr_script = 0x0000;
    csr_script_active = 1;
    run(0);

    ASSERT_EQ(0x2000, result_flags);
    ASSERT_EQ(1, stats()->xmitcnt);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, wait_calls);
}

TEST(unknown_msg_type_counts_nack_and_retries)
{
    setup();
    unit_data()->tx_ec.value = 5;
    hdr.msg_type = 5;
    csr_script = 0x0000;
    csr_script_active = 1;
    run(0);

    ASSERT_EQ(0x2000, result_flags & 0x2000);
    ASSERT_EQ(0x14, stats()->xmit_nack);
    ASSERT_EQ(0xFA, wait_delay.low);            /* 0xE75D76 */
}

TEST(zero_msg_type_counts_nack)
{
    setup();
    unit_data()->tx_ec.value = 5;
    hdr.msg_type = 0;
    csr_script = 0x0000;
    csr_script_active = 1;
    run(0);

    ASSERT_EQ(0x14, stats()->xmit_nack);
}

/* 0xE75D80: bit 3 clear records the unexpected status word. */
TEST(unexpected_status_is_recorded)
{
    decode(0x0010);

    ASSERT_EQ(0x0010, RING_$UNEXPECTED_XMIT_STAT);
    ASSERT_EQ(0x0040, result_flags & 0x00FF);
}

TEST(unexpected_status_with_bit3_is_not_recorded)
{
    decode(0x0018 | 0x0010);   /* 0x18 is the packet-error pair, tested first */

    ASSERT_EQ(0, RING_$UNEXPECTED_XMIT_STAT);
    ASSERT_EQ(0x14, stats()->xmit_error);
}

/*
 * 0xE75D90: the "no retry" gate.  Either the header's flags bit 7 or bit 0 of
 * the option word stops the retry that the 0x220 arm would otherwise do.
 */
TEST(no_retry_from_header_flags)
{
    setup();
    unit_data()->tx_ec.value = 5;
    csr_script = 0x0020;
    csr_script_active = 1;
    hdr.flags = 0x80;
    run(0);

    ASSERT_EQ(0, wait_calls);
    ASSERT_EQ(0x0600, result_flags);
}

TEST(no_retry_from_send_opts_bit_zero)
{
    setup();
    unit_data()->tx_ec.value = 5;
    csr_script = 0x0020;
    csr_script_active = 1;
    send_opts = 0x0001;
    run(0);

    ASSERT_EQ(0, wait_calls);
}

/* 0xE75D98: the header's flags bit 1 also stops the retry, one step later. */
TEST(header_flag_bit1_stops_the_retry)
{
    setup();
    unit_data()->tx_ec.value = 5;
    csr_script = 0x0001;         /* overrun, which jumps straight to `retry` */
    csr_script_active = 1;
    hdr.flags = 0x02;
    run(0);

    ASSERT_EQ(1, stats()->xmit_orun);
    ASSERT_EQ(0, wait_calls);
}

/*
 * 0xE75DC2: last_success is the ONE'S COMPLEMENT of the activity flag, and
 * setting it also sets bit 9 of the result word.
 */
TEST(no_activity_publishes_last_success)
{
    decode(0x0001);              /* the overrun arm never sets D5 */

    ASSERT_EQ((int8_t)-1, stats()->last_success);
    ASSERT_EQ(0x0200, result_flags & 0x0200);
}

int main(void)
{
    printf("=== RING_$SENDP tests ===\n");

    RUN_TEST(uninitialised_unit);
    RUN_TEST(data_too_long);
    RUN_TEST(controller_not_ready);
    RUN_TEST(zero_length_uses_header_address);
    RUN_TEST(nonzero_length_copies_the_descriptor);
    RUN_TEST(checksum_uses_the_hdr_len_argument);
    RUN_TEST(checksum_disabled_writes_one);
    RUN_TEST(poll_sees_immediate_completion);
    RUN_TEST(poll_timeout_takes_the_absolute_wait);
    RUN_TEST(second_absolute_wait_jitters_the_high_longword);
    RUN_TEST(both_absolute_waits_time_out_then_abort);
    RUN_TEST(retry_pending_takes_the_force_start_arm);
    RUN_TEST(complete_status);
    RUN_TEST(modem_errors_set_two_low_bits);
    RUN_TEST(bus_error_owned_by_parity);
    RUN_TEST(bus_error_not_ours_counts_and_retries);
    RUN_TEST(overrun_sets_bit_eight);
    RUN_TEST(no_return_sets_bit_ten);
    RUN_TEST(wait_ack_sets_bit_fourteen);
    RUN_TEST(known_msg_type_is_reported_delivered);
    RUN_TEST(unknown_msg_type_counts_nack_and_retries);
    RUN_TEST(zero_msg_type_counts_nack);
    RUN_TEST(unexpected_status_is_recorded);
    RUN_TEST(unexpected_status_with_bit3_is_not_recorded);
    RUN_TEST(no_retry_from_header_flags);
    RUN_TEST(no_retry_from_send_opts_bit_zero);
    RUN_TEST(header_flag_bit1_stops_the_retry);
    RUN_TEST(no_activity_publishes_last_success);

    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
