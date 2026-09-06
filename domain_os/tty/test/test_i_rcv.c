/*
 * tty/test/test_i_rcv.c - Unit tests for TTY_$I_RCV (0x00E1B92A)
 *
 * The real tty/i_rcv.c and tty/i_buf_insert.c are #included below, so the
 * function under test is the real one; every callee it reaches is stubbed
 * here and records what it was handed.
 *
 * Covered:
 *   - character-class dispatch through all 19 jump-table entries
 *   - input ring placement (the 0x2D1 + tail displacement)
 *   - the 0x20 / 0x1000 input-flag tests at 0xE1B95E / 0xE1B976
 *   - the xon/xoff handler's second (boolean) argument
 *   - the flow-control handler's three arguments
 *   - class 0x04 emitting 0x08 twice
 *   - class 0x08 -> KILL_LINE, class 0x09 -> WORD_ERASE
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "tty/tty_internal.h"
#include "time/time.h"
#include "mmu/mmu.h"
#include "misc/crash_system.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

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
        printf("FAILED\n    Expected: 0x%lx (%lu), Got: 0x%lx (%lu) at line %d\n", \
               _e, _e, _a, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_STR(expected, actual) do { \
    if (strcmp((expected), (actual)) != 0) { \
        printf("FAILED\n    Expected: \"%s\", Got: \"%s\" at line %d\n", \
               (expected), (actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ============================================================================
 * Call log + stubs for every callee TTY_$I_RCV reaches
 * ============================================================================ */

static char call_log[512];

static void logf_call(const char *fmt, ...)
{
    va_list ap;
    size_t used = strlen(call_log);
    va_start(ap, fmt);
    vsnprintf(call_log + used, sizeof(call_log) - used, fmt, ap);
    va_end(ap);
}

static void log_reset(void) { call_log[0] = '\0'; }

void TTY_$I_ECHO_CHAR(tty_desc_t *tty, uint8_t ch)
{ (void)tty; logf_call("echo(%02x);", ch); }

void TTY_$I_XMIT_CHAR(tty_desc_t *tty, uint16_t ch)
{ (void)tty; logf_call("xmit(%04x);", ch); }

void TTY_$I_FLUSH_INPUT(tty_desc_t *tty)
{ (void)tty; logf_call("flush_in;"); }

void TTY_$I_FLUSH_OUTPUT(tty_desc_t *tty)
{ (void)tty; logf_call("flush_out;"); }

void TTY_$I_SIGNAL(tty_desc_t *tty, short sig)
{ (void)tty; logf_call("signal(%02x);", (unsigned)sig); }

void TTY_$I_BREAK_CHAR(tty_desc_t *tty, uint8_t ch)
{ (void)tty; logf_call("break(%02x);", ch); }

void TTY_$I_DELETE_CHAR(tty_desc_t *tty)
{ (void)tty; logf_call("del;"); }

void TTY_$I_KILL_LINE(tty_desc_t *tty)
{ (void)tty; logf_call("kill;"); }

void TTY_$I_WORD_ERASE(tty_desc_t *tty)
{ (void)tty; logf_call("werase;"); }

void TTY_$I_NEWLINE(tty_desc_t *tty)
{ (void)tty; logf_call("nl;"); }

void TTY_$I_ADVANCE_EC(m68k_ptr_t ec)
{ logf_call("advance_ec(%lx);", (unsigned long)ec); }

void TIME_$CLOCK(clock_t *c)
{ c->high = 0x11223344; c->low = 0x5566; logf_call("clock;"); }

static int8_t mmu_normal_mode_result = 0;
int8_t MMU_$NORMAL_MODE(void) { return mmu_normal_mode_result; }

static status_$t crash_status_seen = -1;
void CRASH_SYSTEM(const status_$t *status_p)
{ crash_status_seen = *status_p; logf_call("crash(%lx);", (unsigned long)*status_p); }

/* Recorded arguments of the two indirect handlers */
static uint32_t xon_line_id;
static boolean  xon_stop;
static int      xon_calls;

static void mock_xon_xoff(uint32_t line_id, boolean stop)
{
    xon_line_id = line_id;
    xon_stop = stop;
    xon_calls++;
    logf_call("xonxoff(%lx,%02x);", (unsigned long)line_id, (unsigned char)stop);
}

static uint32_t flow_line_id;
static boolean  flow_arg2;
static boolean  flow_arg3;
static int      flow_calls;

static void mock_flow_ctrl(uint32_t line_id, boolean a, boolean b)
{
    flow_line_id = line_id;
    flow_arg2 = a;
    flow_arg3 = b;
    flow_calls++;
    logf_call("flow(%lx,%02x,%02x);", (unsigned long)line_id,
              (unsigned char)a, (unsigned char)b);
}

/* ============================================================================
 * The code under test
 * ============================================================================ */

#include "../i_buf_insert.c"
#include "../i_rcv.c"

/* ============================================================================
 * Fixture
 * ============================================================================ */

static tty_desc_t tty;

static void setup(uint16_t class_for_all)
{
    int i;
    memset(&tty, 0, sizeof(tty));
    tty.line_id = 0xDEADBEEFu;
    tty.input_head = 1;
    tty.input_read = 1;
    tty.input_tail = 1;
    tty.input_size = TTY_BUFFER_SIZE;
    tty.output_head = 1;
    tty.output_read = 1;
    tty.output_tail = TTY_BUFFER_SIZE;
    for (i = 0; i < 256; i++) {
        tty.char_class[i] = class_for_all;
    }
    log_reset();
    xon_calls = flow_calls = 0;
    crash_status_seen = -1;
}

/* ============================================================================
 * Layout: the circular-buffer record shape the assembly assumes
 * ============================================================================ */

/*
 * tty_$i_buf_insert stores at buf + 5 + tail with a 1-based tail, and
 * TTY_$I_RCV stores the same element at (0x2d1,A2,tail).  Both statements are
 * only true when input_buffer[] begins 6 bytes past &input_read.
 */
TEST(layout_input_record)
{
    tty_desc_t t;
    ASSERT_EQ(6, (unsigned long)((char *)&t.input_buffer[0] - (char *)&t.input_read));
    ASSERT_EQ(2, (unsigned long)((char *)&t.input_tail - (char *)&t.input_read));
    ASSERT_EQ(4, (unsigned long)((char *)&t.input_size - (char *)&t.input_read));
    /* Same record shape for the output side (buf == &output_head). */
    ASSERT_EQ(6, (unsigned long)((char *)&t.output_buffer[0] - (char *)&t.output_head));
}

/*
 * The class-0x12 store and tty_$i_buf_insert must land on the same byte for
 * the same tail value.
 */
TEST(store_agrees_with_buf_insert)
{
    setup(TTY_CHAR_CLASS_NORMAL);
    tty.input_tail = 7;
    TTY_$I_RCV(&tty, 'A');
    ASSERT_EQ('A', tty.input_buffer[6]);
    ASSERT_EQ(8, tty.input_tail);

    /* Same position via the insert helper */
    tty.input_tail = 7;
    tty.input_buffer[6] = 0;
    tty_$i_buf_insert('B', &tty.input_read);
    ASSERT_EQ('B', tty.input_buffer[6]);
    ASSERT_EQ(8, tty.input_tail);
}

/* Wrap: tail 0x100 stores at input_buffer[0xFF] and wraps to 1. */
TEST(store_wraps_at_end)
{
    setup(TTY_CHAR_CLASS_NORMAL);
    tty.input_tail = TTY_BUFFER_SIZE;
    tty.input_head = TTY_BUFFER_SIZE;
    tty.input_read = 2;
    TTY_$I_RCV(&tty, 'Z');
    ASSERT_EQ('Z', tty.input_buffer[TTY_BUFFER_SIZE - 1]);
    ASSERT_EQ(1, tty.input_tail);
}

/* ============================================================================
 * Entry-path flag tests
 * ============================================================================ */

/* 0xE1B95E: btst.b #5,(0x17,A2) is bit 5 (0x20) of the long at 0x14. */
TEST(strip_high_bit_uses_mask_0x20)
{
    setup(TTY_CHAR_CLASS_NORMAL);
    tty.input_flags = 0x00000020;
    TTY_$I_RCV(&tty, 0xC1);
    ASSERT_EQ(0x41, tty.input_buffer[0]);

    /* 0x2000 is NOT the strip bit */
    setup(TTY_CHAR_CLASS_NORMAL);
    tty.input_flags = 0x00002000;
    TTY_$I_RCV(&tty, 0xC1);
    ASSERT_EQ(0xC1, tty.input_buffer[0]);
}

/* 0xE1B96C: 0xFF with input_flags bit 12 set stuffs a literal 0xFF first. */
TEST(parity_escape_prefix)
{
    setup(TTY_CHAR_CLASS_NORMAL);
    tty.input_flags = 0x00001000;
    TTY_$I_RCV(&tty, 0xFF);
    /*
     * buf_insert put 0xFF at [0] and advanced input_tail to 2, then the
     * class-0x12 body stored the second 0xFF at [1] -- but wrote back
     * next_tail, which was computed at 0xE1B940 from the PRE-insert tail.  So
     * the tail ends at 2, not 3: the original overwrites its own first byte on
     * the next character.  Preserved as-is (archivist rule).
     */
    ASSERT_EQ(0xFF, tty.input_buffer[0]);
    ASSERT_EQ(0xFF, tty.input_buffer[1]);
    ASSERT_EQ(2, tty.input_tail);

    /* Without bit 12 there is no prefix */
    setup(TTY_CHAR_CLASS_NORMAL);
    TTY_$I_RCV(&tty, 0xFF);
    ASSERT_EQ(0xFF, tty.input_buffer[0]);
    ASSERT_EQ(2, tty.input_tail);
}

/* 0xE1B952: next_tail == input_read sets the overflow bit and skips the store */
TEST(overflow_bit_blocks_store)
{
    setup(TTY_CHAR_CLASS_NORMAL);
    tty.input_tail = 5;
    tty.input_read = 6;    /* next_tail (6) == input_read -> full */
    TTY_$I_RCV(&tty, 'X');
    ASSERT_EQ(TTY_ERR_OVERFLOW, tty.pending_signal & TTY_ERR_OVERFLOW);
    ASSERT_EQ(0, tty.input_buffer[4]);
    ASSERT_EQ(5, tty.input_tail);
}

/* 0xE1B994: raw mode (state_flags bit 6) forces class 0x12 */
TEST(raw_mode_forces_normal_class)
{
    setup(TTY_CHAR_CLASS_SIGINT);
    tty.state_flags = TTY_FLAG_RAW_MODE;
    TTY_$I_RCV(&tty, 'q');
    ASSERT_STR("", call_log);
    ASSERT_EQ('q', tty.input_buffer[0]);
    /* state_flags was masked with 0xFF1F, clearing the raw bit for this call */
    ASSERT_EQ(0, tty.state_flags & TTY_FLAG_RAW_MODE);
}

/* ============================================================================
 * Class dispatch
 * ============================================================================ */

TEST(class_signals)
{
    setup(TTY_CHAR_CLASS_SIGINT);
    TTY_$I_RCV(&tty, 3);
    ASSERT_STR("echo(03);flush_in;signal(02);", call_log);

    setup(TTY_CHAR_CLASS_SIGQUIT);
    TTY_$I_RCV(&tty, 0x1c);
    ASSERT_STR("echo(1c);flush_in;signal(03);", call_log);

    setup(TTY_CHAR_CLASS_SIGTSTP);
    TTY_$I_RCV(&tty, 0x1a);
    ASSERT_STR("echo(1a);flush_in;signal(15);", call_log);
}

TEST(class_break_and_nl)
{
    setup(TTY_CHAR_CLASS_BREAK);
    TTY_$I_RCV(&tty, 0x0d);
    ASSERT_STR("break(0d);", call_log);

    setup(TTY_CHAR_CLASS_NL);
    TTY_$I_RCV(&tty, 0x0a);
    ASSERT_STR("break(0a);", call_log);
}

/* 0xE1BB40: class 4 sets EOF-pending and, when echo is on, emits 0x08 TWICE */
TEST(class_eof_emits_bs_twice)
{
    setup(TTY_CHAR_CLASS_EOF);
    tty.input_flags = 0x00000001;   /* echo enabled */
    TTY_$I_RCV(&tty, 4);
    ASSERT_EQ(TTY_STATUS_EOF_PEND, tty.state_flags & TTY_STATUS_EOF_PEND);
    ASSERT_STR("echo(04);xmit(0800);xmit(0800);", call_log);

    /* echo off: flag set, nothing emitted */
    setup(TTY_CHAR_CLASS_EOF);
    TTY_$I_RCV(&tty, 4);
    ASSERT_EQ(TTY_STATUS_EOF_PEND, tty.state_flags & TTY_STATUS_EOF_PEND);
    ASSERT_STR("", call_log);
}

/* 0xE1BA42 / 0xE1BA60: the handler's 2nd argument is st (0xFF) then clr (0) */
TEST(class_xon_xoff_handler_second_arg)
{
    setup(TTY_CHAR_CLASS_XON);
    tty.xon_xoff_handler = mock_xon_xoff;
    TTY_$I_RCV(&tty, 0x13);
    ASSERT_EQ(1, xon_calls);
    ASSERT_EQ(0xDEADBEEFu, xon_line_id);
    ASSERT_EQ((unsigned char)true, (unsigned char)xon_stop);
    ASSERT_EQ(TTY_STATUS_XON_XOFF, tty.state_flags & TTY_STATUS_XON_XOFF);
    ASSERT_STR("xonxoff(deadbeef,ff);", call_log);

    setup(TTY_CHAR_CLASS_XOFF);
    tty.state_flags = TTY_STATUS_XON_XOFF;
    tty.xon_xoff_handler = mock_xon_xoff;
    tty.output_ec = 0x1234;
    TTY_$I_RCV(&tty, 0x11);
    ASSERT_EQ(1, xon_calls);
    ASSERT_EQ((unsigned char)false, (unsigned char)xon_stop);
    ASSERT_EQ(0, tty.state_flags & TTY_STATUS_XON_XOFF);
    ASSERT_STR("xonxoff(deadbeef,00);advance_ec(1234);", call_log);
}

/* A null handler pointer must be skipped (tst.l (0x2b8,A2)) */
TEST(class_xon_null_handler)
{
    setup(TTY_CHAR_CLASS_XON);
    TTY_$I_RCV(&tty, 0x13);
    ASSERT_EQ(0, xon_calls);
    ASSERT_EQ(TTY_STATUS_XON_XOFF, tty.state_flags & TTY_STATUS_XON_XOFF);
}

/* 0xE1BACE/0xE1BAF6/0xE1BAE2: the parity bit is re-ORed, then the editor runs */
TEST(class_editing_dispatch)
{
    setup(TTY_CHAR_CLASS_DEL);
    tty.state_flags = TTY_FLAG_PARITY_ERR;
    TTY_$I_RCV(&tty, 0x7f);
    ASSERT_STR("del;", call_log);
    ASSERT_EQ(TTY_FLAG_PARITY_ERR, tty.state_flags & TTY_FLAG_PARITY_ERR);

    /* class 0x08 -> TTY_$I_KILL_LINE (0xE1BAF6 -> 0xE1B6AC) */
    setup(TTY_CHAR_CLASS_KILL);
    TTY_$I_RCV(&tty, 0x15);
    ASSERT_STR("kill;", call_log);

    /* class 0x09 -> TTY_$I_WORD_ERASE (0xE1BAE2 -> 0xE1B716) */
    setup(TTY_CHAR_CLASS_WERASE);
    TTY_$I_RCV(&tty, 0x17);
    ASSERT_STR("werase;", call_log);
}

/* 0xE1BBEC: re-echo input_head..input_tail, wrapping at 0x100 */
TEST(class_reprint)
{
    setup(TTY_CHAR_CLASS_REPRINT);
    tty.input_flags = 0x00000001;
    tty.input_head = 2;
    tty.input_tail = 5;
    tty.input_buffer[1] = 'a';
    tty.input_buffer[2] = 'b';
    tty.input_buffer[3] = 'c';
    TTY_$I_RCV(&tty, 0x12);
    ASSERT_STR("echo(12);nl;echo(61);echo(62);echo(63);", call_log);

    /* echo disabled -> nothing at all */
    setup(TTY_CHAR_CLASS_REPRINT);
    tty.input_head = 2;
    tty.input_tail = 5;
    TTY_$I_RCV(&tty, 0x12);
    ASSERT_STR("", call_log);
}

TEST(class_discard)
{
    setup(TTY_CHAR_CLASS_DISCARD);
    TTY_$I_RCV(&tty, 0x0f);
    ASSERT_STR("echo(0f);xmit(0800);xmit(0800);break(0f);", call_log);
}

TEST(class_flushout)
{
    setup(TTY_CHAR_CLASS_FLUSHOUT);
    TTY_$I_RCV(&tty, 0x0f);
    ASSERT_STR("flush_out;", call_log);
    ASSERT_EQ(TTY_STATUS_OUTPUT_FLUSH, tty.state_flags & TTY_STATUS_OUTPUT_FLUSH);

    /* Already flushing and echo on -> just echo */
    setup(TTY_CHAR_CLASS_FLUSHOUT);
    tty.state_flags = TTY_STATUS_OUTPUT_FLUSH;
    tty.input_flags = 0x00000001;
    TTY_$I_RCV(&tty, 0x0f);
    ASSERT_STR("echo(0f);", call_log);
}

/* 0xE1BBA8: bit 4 set -> (bit 3 set ? drop : re-dispatch as 0x0D) */
TEST(class_cr)
{
    /* bit 4 clear -> plain break char */
    setup(TTY_CHAR_CLASS_CR);
    TTY_$I_RCV(&tty, 0x0a);
    ASSERT_STR("break(0a);", call_log);

    /* bit 4 set, bit 3 set -> dropped */
    setup(TTY_CHAR_CLASS_CR);
    tty.input_flags = 0x00000018;
    TTY_$I_RCV(&tty, 0x0a);
    ASSERT_STR("", call_log);

    /* bit 4 set, bit 3 clear -> re-dispatch 0x0D through class NORMAL */
    setup(TTY_CHAR_CLASS_CR);
    tty.char_class[0x0d] = TTY_CHAR_CLASS_BREAK;
    tty.input_flags = 0x00000010;
    TTY_$I_RCV(&tty, 0x0a);
    ASSERT_STR("break(0d);", call_log);
}

/* 0xE1BBC2 */
TEST(class_crlf)
{
    /* bit 9 set -> dropped outright */
    setup(TTY_CHAR_CLASS_CRLF);
    tty.input_flags = 0x00000200;
    TTY_$I_RCV(&tty, 0x0d);
    ASSERT_STR("", call_log);
    ASSERT_EQ(1, tty.input_tail);

    /* bit 3 set, bit 4 set -> dropped */
    setup(TTY_CHAR_CLASS_CRLF);
    tty.input_flags = 0x00000018;
    TTY_$I_RCV(&tty, 0x0d);
    ASSERT_STR("", call_log);
    ASSERT_EQ(1, tty.input_tail);

    /* bit 3 set, bit 4 clear -> re-dispatch as 0x0A */
    setup(TTY_CHAR_CLASS_CRLF);
    tty.char_class[0x0a] = TTY_CHAR_CLASS_BREAK;
    tty.input_flags = 0x00000008;
    TTY_$I_RCV(&tty, 0x0d);
    ASSERT_STR("break(0a);", call_log);

    /* bit 3 clear -> falls through into the class 0x12 body */
    setup(TTY_CHAR_CLASS_CRLF);
    TTY_$I_RCV(&tty, 0x0d);
    ASSERT_EQ(0x0d, tty.input_buffer[0]);
    ASSERT_EQ(2, tty.input_tail);
}

/* 0xE1BB7A: class 0x10 inserts via buf_insert and echoes ch<<8 */
TEST(class_tab)
{
    setup(TTY_CHAR_CLASS_TAB);
    tty.input_flags = 0x00000001;
    tty.column = 0x0042;
    TTY_$I_RCV(&tty, 0x09);
    ASSERT_EQ(0x09, tty.input_buffer[0]);
    ASSERT_EQ(2, tty.input_tail);
    ASSERT_EQ(0x0042, tty.saved_input_flags);
    ASSERT_STR("xmit(0900);", call_log);
}

/* 0xE1BA86: crash only when MMU_$NORMAL_MODE returns a non-negative byte */
TEST(class_crash)
{
    setup(TTY_CHAR_CLASS_CRASH);
    mmu_normal_mode_result = 0;
    TTY_$I_RCV(&tty, 0x1b);
    ASSERT_EQ(0x000B0008, crash_status_seen);

    setup(TTY_CHAR_CLASS_CRASH);
    mmu_normal_mode_result = (int8_t)0xFF;
    TTY_$I_RCV(&tty, 0x1b);
    ASSERT_EQ((unsigned long)(status_$t)-1, (unsigned long)crash_status_seen);
    mmu_normal_mode_result = 0;
}

/* Class >= 0x13 (0xE1B9B2 bcc) does nothing at all */
TEST(class_out_of_range)
{
    setup(0x13);
    TTY_$I_RCV(&tty, 'x');
    ASSERT_STR("", call_log);
    ASSERT_EQ(1, tty.input_tail);
}

/* ============================================================================
 * Class 0x12 body details
 * ============================================================================ */

/* 0xE1BC6A: >= 0xC0 pending characters triggers the flow-control handler */
TEST(flow_control_handler_args)
{
    setup(TTY_CHAR_CLASS_NORMAL);
    tty.flow_ctrl_handler = mock_flow_ctrl;
    tty.input_flags = 0x00000002;   /* hardware flow control */
    tty.input_read = 1;
    tty.input_tail = 0xC0;          /* after the store, tail - read == 0xC0 */
    tty.input_head = 0xC0;
    TTY_$I_RCV(&tty, 'p');
    ASSERT_EQ(1, flow_calls);
    ASSERT_EQ(0xDEADBEEFu, flow_line_id);
    ASSERT_EQ((unsigned char)true, (unsigned char)flow_arg2);
    ASSERT_EQ((unsigned char)true, (unsigned char)flow_arg3);

    /* One below the threshold: not called */
    setup(TTY_CHAR_CLASS_NORMAL);
    tty.flow_ctrl_handler = mock_flow_ctrl;
    tty.input_read = 1;
    tty.input_tail = 0xBF;
    tty.input_head = 0xBF;
    TTY_$I_RCV(&tty, 'p');
    ASSERT_EQ(0, flow_calls);

    /* Without the hw-flow input flag the third argument is false */
    setup(TTY_CHAR_CLASS_NORMAL);
    tty.flow_ctrl_handler = mock_flow_ctrl;
    tty.input_read = 1;
    tty.input_tail = 0xC0;
    tty.input_head = 0xC0;
    TTY_$I_RCV(&tty, 'p');
    ASSERT_EQ(1, flow_calls);
    ASSERT_EQ((unsigned char)false, (unsigned char)flow_arg3);
}

/* 0xE1BC8C: tst.b D4b tests bit 7 of the saved state_flags word */
TEST(parity_error_emits_2f)
{
    setup(TTY_CHAR_CLASS_NORMAL);
    tty.state_flags = TTY_FLAG_PARITY_ERR;
    TTY_$I_RCV(&tty, 'y');
    ASSERT_STR("xmit(2f00);", call_log);

    /* bit 15 must NOT trigger it */
    setup(TTY_CHAR_CLASS_NORMAL);
    tty.state_flags = 0x8000;
    TTY_$I_RCV(&tty, 'y');
    ASSERT_STR("", call_log);
}

/* 0xE1BCB2: break_mode != 0 commits the line and advances the input EC */
TEST(break_mode_commits_line)
{
    setup(TTY_CHAR_CLASS_NORMAL);
    tty.break_mode = 1;
    tty.input_ec = 0x9999;
    TTY_$I_RCV(&tty, 'k');
    ASSERT_EQ(2, tty.input_head);
    ASSERT_STR("advance_ec(9999);", call_log);

    /* Pending signal -> TTY_SIG_WINCH */
    setup(TTY_CHAR_CLASS_NORMAL);
    tty.break_mode = 1;
    tty.state_flags = TTY_STATUS_SIG_PEND;
    TTY_$I_RCV(&tty, 'k');
    ASSERT_STR("advance_ec(0);signal(1a);", call_log);

    /* break_mode 3 also timestamps into the 48-bit clock cell at 0x2C4 */
    setup(TTY_CHAR_CLASS_NORMAL);
    tty.break_mode = 3;
    TTY_$I_RCV(&tty, 'k');
    ASSERT_STR("advance_ec(0);clock;", call_log);
    ASSERT_EQ(0x11223344u, tty.last_input_clock_high);
    ASSERT_EQ(0x5566, tty.last_input_clock_low);

    /* break_mode 2 does not timestamp */
    setup(TTY_CHAR_CLASS_NORMAL);
    tty.break_mode = 2;
    TTY_$I_RCV(&tty, 'k');
    ASSERT_STR("advance_ec(0);", call_log);
}

/* 0xE1BC44: the column is latched only on the first character of a line */
TEST(column_latched_on_first_char)
{
    setup(TTY_CHAR_CLASS_NORMAL);
    tty.column = 0x0037;
    TTY_$I_RCV(&tty, 'a');
    ASSERT_EQ(0x0037, tty.saved_input_flags);

    tty.column = 0x0099;
    TTY_$I_RCV(&tty, 'b');           /* tail (2) != head (1) now */
    ASSERT_EQ(0x0037, tty.saved_input_flags);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void)
{
    printf("TTY_$I_RCV tests\n");

    RUN_TEST(layout_input_record);
    RUN_TEST(store_agrees_with_buf_insert);
    RUN_TEST(store_wraps_at_end);
    RUN_TEST(strip_high_bit_uses_mask_0x20);
    RUN_TEST(parity_escape_prefix);
    RUN_TEST(overflow_bit_blocks_store);
    RUN_TEST(raw_mode_forces_normal_class);
    RUN_TEST(class_signals);
    RUN_TEST(class_break_and_nl);
    RUN_TEST(class_eof_emits_bs_twice);
    RUN_TEST(class_xon_xoff_handler_second_arg);
    RUN_TEST(class_xon_null_handler);
    RUN_TEST(class_editing_dispatch);
    RUN_TEST(class_reprint);
    RUN_TEST(class_discard);
    RUN_TEST(class_flushout);
    RUN_TEST(class_cr);
    RUN_TEST(class_crlf);
    RUN_TEST(class_tab);
    RUN_TEST(class_crash);
    RUN_TEST(class_out_of_range);
    RUN_TEST(flow_control_handler_args);
    RUN_TEST(parity_error_emits_2f);
    RUN_TEST(break_mode_commits_line);
    RUN_TEST(column_latched_on_first_char);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
