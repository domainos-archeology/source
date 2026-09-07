/*
 * sio/test/test_i_tstart.c - unit tests for SIO_$I_TSTART / SIO_DELAY_RESTART
 *
 * Compiles the real sio/i_tstart.c and drives it with a synthetic descriptor
 * and transmit buffer, so the argument shape of the TIME_$Q_ADD_CALLBACK call
 * (bead source-0enz) and the state-word masks are exercised against the code
 * that ships, not restated.
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

#include "sio/sio_internal.h"

/* ==========================================================================
 * Globals and callees the code under test links against
 * ========================================================================== */

time_queue_t TIME_$RTEQ;

static clock_t mock_abs_clock;

void TIME_$ABS_CLOCK(clock_t *clock) { *clock = mock_abs_clock; }

static int add_calls;
static time_queue_t *add_queue;
static clock_t add_when;
static uint16_t add_is_absolute;
static clock_t add_now;
static void *add_callback;
static void *add_callback_arg;
static uint16_t add_flags;
static clock_t add_interval;
static time_queue_elem_t *add_qelem;
static status_$t add_status_out;

void TIME_$Q_ADD_CALLBACK(time_queue_t *queue, clock_t *when,
                          uint16_t is_absolute, clock_t *now,
                          void *callback, void *callback_arg,
                          uint16_t flags, clock_t *interval,
                          time_queue_elem_t *qelem, status_$t *status)
{
    add_calls++;
    add_queue = queue;
    add_when = *when;
    add_is_absolute = is_absolute;
    add_now = *now;
    add_callback = callback;
    add_callback_arg = callback_arg;
    add_flags = flags;
    add_interval = *interval;
    add_qelem = qelem;
    *status = add_status_out;
}

/* 0xE0AC02: an unsigned 32x16 multiply keeping the low 32 bits. */
ulong M$MIU$LLW(ulong multiplicand, ushort multiplier)
{
    return (ulong)((uint32_t)multiplicand * (uint32_t)multiplier);
}

/* ==========================================================================
 * The device the descriptor drives
 * ========================================================================== */

#define OUT_MAX 16
static uint8_t out_chars[OUT_MAX];
static int out_count;
static uint32_t out_context;

static void mock_output_char(uint32_t context, uint8_t ch)
{
    out_context = context;
    if (out_count < OUT_MAX) {
        out_chars[out_count] = ch;
    }
    out_count++;
}

static int drain_calls;
static uint32_t drain_owner;

static void mock_drain(uint32_t owner)
{
    drain_calls++;
    drain_owner = owner;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../i_tstart.c"

/* ==========================================================================
 * Fixture
 * ========================================================================== */

/* A 1-based circular buffer of TXBUF_SIZE elements. */
#define TXBUF_SIZE 32

static struct {
    sio_txbuf_t hdr;
    uint8_t     spill[TXBUF_SIZE];
} txbuf_storage;

static sio_desc_t desc;

static sio_txbuf_t *txbuf(void) { return &txbuf_storage.hdr; }

/*
 * The descriptor holds its buffer and its two handlers as 32-bit target
 * virtual addresses, so the host arena base has to sit below everything the
 * test converts (statics and functions alike) for the round trip to fit.
 */
static void va_base_setup(void)
{
    uintptr_t lo = (uintptr_t)&desc;

    if ((uintptr_t)&txbuf_storage < lo) lo = (uintptr_t)&txbuf_storage;
    if ((uintptr_t)mock_output_char < lo) lo = (uintptr_t)mock_output_char;
    if ((uintptr_t)mock_drain < lo) lo = (uintptr_t)mock_drain;

    ARCH_HOST_VA_BASE = lo - 0x1000u;
}

static void setup(void)
{
    va_base_setup();
    memset(&desc, 0, sizeof(desc));
    memset(&txbuf_storage, 0, sizeof(txbuf_storage));
    memset(&TIME_$RTEQ, 0, sizeof(TIME_$RTEQ));
    out_count = 0;
    drain_calls = 0;
    add_calls = 0;
    add_status_out = status_$ok;
    mock_abs_clock = (clock_t){ 0, 0 };

    desc.context = 0x11112222;
    desc.owner = 0x33334444;
    desc.txbuf = ARCH_PTR_TO_VA(txbuf());
    desc.output_char = ARCH_PTR_TO_VA(mock_output_char);
    desc.drain_handler = ARCH_PTR_TO_VA(mock_drain);

    txbuf()->size = TXBUF_SIZE;
    txbuf()->read_idx = 1;
    txbuf()->write_idx = 1;
}

/* Append one byte at the write index, the way the producer would. */
static void tx_put(uint8_t b)
{
    txbuf()->data[txbuf()->write_idx - 1] = b;
    if (txbuf()->write_idx == txbuf()->size) {
        txbuf()->write_idx = 1;
    } else {
        txbuf()->write_idx++;
    }
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0xE1C7B6: any of bits 0..4 set means the port is busy or blocked. */
TEST(blocked_state_sends_nothing)
{
    setup();
    tx_put('A');
    desc.state = SIO_XMIT_CTS_BLOCKED;

    SIO_$I_TSTART(&desc);

    ASSERT_EQ(0, out_count);
    ASSERT_EQ(1, txbuf()->read_idx);   /* the buffer is untouched */
}

/*
 * 0xE1C7CC: with bit 6 set the port sends XOFF, sets bit 7 and clears bit 6.
 */
TEST(defer_pending_sends_xoff)
{
    setup();
    tx_put('A');
    desc.state = SIO_XMIT_DEFER_PENDING;

    SIO_$I_TSTART(&desc);

    ASSERT_EQ(1, out_count);
    ASSERT_EQ(0x13, out_chars[0]);
    ASSERT_EQ(0x11112222u, out_context);
    ASSERT_EQ(SIO_XMIT_DEFER_COMPLETE, desc.state);
    ASSERT_EQ(1, txbuf()->read_idx);   /* nothing was dequeued */
}

/*
 * 0xE1C808 "andi.w #-0xa1": the XON arm clears 0x20 and 0x80, and leaves
 * bit 6 alone (it is already clear on this path).
 */
TEST(defer_inhibit_sends_xon_and_clears_a0)
{
    setup();
    desc.state = SIO_XMIT_DEFER_INHIBIT | SIO_XMIT_DEFER_COMPLETE | 0x0100;

    SIO_$I_TSTART(&desc);

    ASSERT_EQ(1, out_count);
    ASSERT_EQ(0x11, out_chars[0]);
    ASSERT_EQ(0x0100, desc.state);     /* the unrelated high bit survives */
}

/* An empty buffer does nothing at all (0xE1C818). */
TEST(empty_buffer_sends_nothing)
{
    setup();

    SIO_$I_TSTART(&desc);

    ASSERT_EQ(0, out_count);
    ASSERT_EQ(0, desc.state);
}

/* The ordinary path: one character out, transmit-active set. */
TEST(single_character_is_sent)
{
    setup();
    tx_put('A');
    tx_put('B');

    SIO_$I_TSTART(&desc);

    ASSERT_EQ(1, out_count);
    ASSERT_EQ('A', out_chars[0]);
    ASSERT_EQ(SIO_XMIT_ACTIVE, desc.state);
    ASSERT_EQ(2, txbuf()->read_idx);
    ASSERT_EQ(0, drain_calls);         /* one byte still queued */
}

/* 0xE1C928: an emptied buffer calls the drain handler with desc->owner. */
TEST(emptied_buffer_calls_drain)
{
    setup();
    tx_put('A');

    SIO_$I_TSTART(&desc);

    ASSERT_EQ(1, out_count);
    ASSERT_EQ(1, drain_calls);
    ASSERT_EQ(0x33334444u, drain_owner);
}

/* The read index wraps to 1, not 0 (0xE1C830 "move.w #0x1,(A2)"). */
TEST(read_index_wraps_to_one)
{
    setup();
    txbuf()->read_idx = TXBUF_SIZE;
    txbuf()->write_idx = 3;
    txbuf()->data[TXBUF_SIZE - 1] = 'Z';

    SIO_$I_TSTART(&desc);

    ASSERT_EQ('Z', out_chars[0]);
    ASSERT_EQ(1, txbuf()->read_idx);
}

/*
 * 0xE1C854: 0xFE followed by a NON-zero byte is not an escape - that byte is
 * sent as an ordinary character.
 */
TEST(marker_with_nonzero_command_sends_the_command_byte)
{
    setup();
    tx_put(0xFE);
    tx_put('Q');

    SIO_$I_TSTART(&desc);

    ASSERT_EQ(1, out_count);
    ASSERT_EQ('Q', out_chars[0]);
    ASSERT_EQ(0, add_calls);
}

/*
 * The delay escape.  0xFE 0x00 hi lo is a big-endian millisecond count; the
 * expiry handed to the queue is that count times 250 (0xE1C88C), placed in
 * the bottom 32 bits of a 48-bit value whose top word stays zero.
 */
TEST(delay_escape_arms_the_queue)
{
    setup();
    tx_put(0xFE);
    tx_put(0x00);
    tx_put(0x12);          /* 0x1234 ms, big-endian */
    tx_put(0x34);
    mock_abs_clock = (clock_t){ 0x00001234, 0x5678 };

    SIO_$I_TSTART(&desc);

    ASSERT_EQ(0, out_count);
    ASSERT_EQ(1, add_calls);

    /*
     * 0x1234 * 0xFA == 0x0011C6C8, and the 32-bit product lands in the
     * bottom 32 bits of the 48-bit value: high == 0x0011, low == 0xC6C8.
     */
    ASSERT_EQ(0x00000011, add_when.high);
    ASSERT_EQ(0xC6C8, add_when.low);

    ASSERT_EQ((uintptr_t)&TIME_$RTEQ, (uintptr_t)add_queue);
    ASSERT_EQ(0, add_is_absolute);                  /* 0xE1C8DA clr.w */
    ASSERT_EQ(0x00001234, add_now.high);
    ASSERT_EQ(0x5678, add_now.low);
    ASSERT_EQ((uintptr_t)SIO_DELAY_RESTART, (uintptr_t)add_callback);
    ASSERT_EQ((uintptr_t)&desc, (uintptr_t)add_callback_arg);
    ASSERT_EQ(8, add_flags);
    ASSERT_EQ(0, add_interval.high);
    ASSERT_EQ(0, add_interval.low);
    /* 0xE1C8C4 "pea (0x8,A0)": the DESCRIPTOR's own element */
    ASSERT_EQ((uintptr_t)&desc.delay_qelem, (uintptr_t)add_qelem);

    /* 0xE1C89E: transmit-active cleared, delay-active set */
    ASSERT_EQ(SIO_STATE_DELAY_ACTIVE, desc.state);
    /* all four escape bytes were consumed */
    ASSERT_EQ(5, txbuf()->read_idx);
}

/*
 * 0xE1C8F0: when the queue refuses the element the callback is run inline,
 * through the same two-level argument the scanner would have built.  The
 * callback clears both 0x10 and 0x01 (0xE1C6A2 "moveq #-0x12") and re-enters
 * SIO_$I_TSTART, which finds the buffer empty and returns at 0xE1C818 -
 * before the drain handler, which only the ordinary output path reaches.
 */
TEST(queue_failure_restarts_inline)
{
    setup();
    tx_put(0xFE);
    tx_put(0x00);
    tx_put(0x00);
    tx_put(0x05);
    add_status_out = 0x000D0001;

    SIO_$I_TSTART(&desc);

    ASSERT_EQ(1, add_calls);
    ASSERT_EQ(0, desc.state);          /* both bits cleared by the callback */
    ASSERT_EQ(0, out_count);
    ASSERT_EQ(0, drain_calls);
    ASSERT_EQ(txbuf()->write_idx, txbuf()->read_idx);
}

/* The callback on its own: two levels of indirection, mask 0xFFEE. */
TEST(delay_restart_clears_both_bits)
{
    uint32_t cell;
    uint32_t *cell_ptr;

    setup();
    desc.state = SIO_STATE_DELAY_ACTIVE | SIO_XMIT_ACTIVE | SIO_XMIT_INHIBITED;

    cell = ARCH_PTR_TO_VA(&desc);
    cell_ptr = &cell;
    SIO_DELAY_RESTART(&cell_ptr);

    /* 0x04 survives; it also blocks the re-entered SIO_$I_TSTART */
    ASSERT_EQ(SIO_XMIT_INHIBITED, desc.state);
    ASSERT_EQ(0, out_count);
}

int main(void)
{
    printf("=== SIO_$I_TSTART tests ===\n");

    RUN_TEST(blocked_state_sends_nothing);
    RUN_TEST(defer_pending_sends_xoff);
    RUN_TEST(defer_inhibit_sends_xon_and_clears_a0);
    RUN_TEST(empty_buffer_sends_nothing);
    RUN_TEST(single_character_is_sent);
    RUN_TEST(emptied_buffer_calls_drain);
    RUN_TEST(read_index_wraps_to_one);
    RUN_TEST(marker_with_nonzero_command_sends_the_command_byte);
    RUN_TEST(delay_escape_arms_the_queue);
    RUN_TEST(queue_failure_restarts_inline);
    RUN_TEST(delay_restart_clears_both_bits);

    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
