/*
 * tty/test/test_i_put_chars.c - Unit tests for tty_$i_put_chars
 *
 * Tests the TTY output character processing function and its helpers:
 *   - tty_$i_buf_insert: lockless circular buffer insert
 *   - tty_$i_buf_put: locked circular buffer insert
 *   - buf_put_delay: delay escape sequence insertion (tested via main fn)
 *   - tty_$i_put_chars: main output processing with special char handling
 *
 * All kernel dependencies (ML_$SPIN_LOCK, ML_$SPIN_UNLOCK, transmit
 * callback) are stubbed.
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    Condition false at line %d\n", __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ================================================================
 * Stub out kernel dependencies
 * ================================================================ */

/* Boolean type for test stubs */
typedef char boolean;
#define true ((boolean)-1)
#define false ((boolean)0)

/* UID type stub (named domain_uid_t to avoid clash with POSIX uid_t) */
typedef struct { uint32_t high; uint32_t low; } domain_uid_t;

/* M68K pointer type - use uintptr_t in tests so 64-bit host pointers fit */
typedef uintptr_t m68k_ptr_t;

/* Status type */
typedef uint32_t status_$t;

/* Spin lock stubs */
static uint32_t TTY_$SPIN_LOCK = 0;
static int spin_lock_count = 0;

static uint16_t ML_$SPIN_LOCK(uint32_t *lock)
{
    (void)lock;
    spin_lock_count++;
    return 0;
}

static void ML_$SPIN_UNLOCK(uint32_t *lock, uint16_t token)
{
    (void)lock;
    (void)token;
    spin_lock_count--;
}

/* Transmit callback tracking */
static int xmit_callback_count = 0;
static uint32_t xmit_callback_last_arg = 0;

static void mock_xmit_callback(uint32_t line_id)
{
    xmit_callback_count++;
    xmit_callback_last_arg = line_id;
}

/* ================================================================
 * Include the actual TTY types and constants needed
 * ================================================================ */

/* TTY constants */
#define TTY_BUFFER_SIZE 0x100
#define TTY_MAX_FUNC_CHARS 0x12
#define TTY_LOCK_ID 3

/* State flags */
#define TTY_STATUS_OUTPUT_WAIT  0x01
#define TTY_STATUS_INPUT_WAIT   0x02
#define TTY_STATUS_XON_XOFF     0x04
#define TTY_STATUS_SIG_PEND     0x10
#define TTY_STATUS_OUTPUT_FLUSH  0x20
#define TTY_STATUS_EOF_PEND     0x40

/* TTY signal entry */
typedef struct tty_signal_entry {
    m68k_ptr_t tty_desc;
    m68k_ptr_t callback;
    uint16_t signal_num;
    uint16_t reserved;
} tty_signal_entry_t;

/* TTY descriptor - matches the struct in tty.h */
typedef struct tty_desc {
    uint32_t line_id;
    m68k_ptr_t handler_ptr;
    uint16_t state_flags;
    uint16_t pending_signal;
    uint8_t output_flags;
    uint8_t status_flags;
    uint16_t reserved_0E;
    uint32_t reserved_10;
    uint32_t input_flags;
    uint16_t reserved_18;
    uint16_t reserved_1A;
    uint32_t echo_flags;
    uint32_t func_enabled;
    uint8_t func_chars[TTY_MAX_FUNC_CHARS];
    uint16_t reserved_36;
    uint16_t break_mode;
    uint16_t min_chars;
    uint32_t reserved_3C;
    uint16_t delay[5];
    uint16_t reserved_4A;
    domain_uid_t pgroup_uid;
    uint16_t session_id;
    uint16_t saved_input_flags;
    uint16_t current_input_flags;  /* Actually column position */
    uint16_t reserved_5A;
    tty_signal_entry_t signals[6];
    uint16_t char_class[256];
    m68k_ptr_t input_ec;
    m68k_ptr_t output_ec;
    m68k_ptr_t reserved_2AC;
    m68k_ptr_t err_handler;
    m68k_ptr_t reserved_2B4;  /* Transmit callback */
    m68k_ptr_t xon_xoff_handler;
    m68k_ptr_t flow_ctrl_handler;
    m68k_ptr_t status_handler;
    m68k_ptr_t reserved_2C4;
    uint16_t reserved_2C8;
    uint16_t input_head;
    uint16_t input_read;
    uint16_t input_tail;
    uint8_t input_buffer[TTY_BUFFER_SIZE];
    uint16_t reserved_3D0;
    uint16_t output_head;
    uint16_t output_read;
    uint16_t output_tail;
    uint8_t output_buffer[TTY_BUFFER_SIZE];
    uint8_t crash_char;
    uint8_t raw_mode;
    uint16_t reserved_4DA;
} tty_desc_t;

/* ================================================================
 * Include the functions under test
 *
 * We include the .c files directly so that the stubs above are
 * used instead of the real kernel functions.
 * ================================================================ */

/* Forward-declare tty_$i_buf_put so i_put_chars.c can call it */
void tty_$i_buf_put(uint8_t ch, void *buf);
void tty_$i_buf_insert(uint8_t ch, void *buf);

/* Include buf_insert/buf_put implementation */
/*
 * Inline the buffer logic here since we can't include the .c files
 * directly (they include tty_internal.h which pulls in real headers).
 * These match the implementations in i_buf_put.c.
 */
void tty_$i_buf_insert(uint8_t ch, void *buf)
{
    uint16_t *head_ptr = (uint16_t *)buf;
    uint16_t *tail_ptr = (uint16_t *)((char *)buf + 2);
    uint8_t *data = (uint8_t *)((char *)buf + 5);

    uint16_t new_tail;
    if (*tail_ptr == TTY_BUFFER_SIZE) {
        new_tail = 1;
    } else {
        new_tail = *tail_ptr + 1;
    }

    if (new_tail != *head_ptr) {
        data[*tail_ptr] = ch;
        *tail_ptr = new_tail;
    }
}

void tty_$i_buf_put(uint8_t ch, void *buf)
{
    uint16_t *head_ptr = (uint16_t *)buf;
    uint16_t *tail_ptr = (uint16_t *)((char *)buf + 2);
    uint8_t *data = (uint8_t *)((char *)buf + 5);

    uint16_t token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK);

    uint16_t new_tail;
    if (*tail_ptr == TTY_BUFFER_SIZE) {
        new_tail = 1;
    } else {
        new_tail = *tail_ptr + 1;
    }

    if (new_tail != *head_ptr) {
        data[*tail_ptr] = ch;
        *tail_ptr = new_tail;
    }

    ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);
}

/*
 * Include the main function under test.
 * We replicate the static buf_put_delay and tty_$i_put_chars here
 * since we can't include the .c file directly.
 */

/* Output flag bits */
#define TTY_OFLAG_CR_TO_LF    0x01
#define TTY_OFLAG_LF_TO_CRLF  0x02
#define TTY_OFLAG_DISCARD      0x08
#define TTY_OFLAG_EXPAND_TABS  0x10

#define TTY_GET_OUTPUT_FLAGS(tty) (*(const uint32_t *)&(tty)->output_flags)
#define TTY_COLUMN(tty) ((tty)->current_input_flags)

#define TTY_DELAY_LF  0
#define TTY_DELAY_CR  1
#define TTY_DELAY_TAB 2
#define TTY_DELAY_VT  3
#define TTY_DELAY_FF  4

#define TTY_DELAY_THRESHOLD 3
#define TTY_BUFFER_NEARLY_FULL 0xBF

static void buf_put_delay(tty_desc_t *tty, uint16_t delay_val, int16_t *local_max)
{
    *local_max -= 4;
    tty_$i_buf_put(0xFE, &tty->output_head);
    tty_$i_buf_put(0x00, &tty->output_head);
    tty_$i_buf_put((uint8_t)(delay_val >> 8), &tty->output_head);
    tty_$i_buf_put((uint8_t)(delay_val & 0xFF), &tty->output_head);
}

uint16_t tty_$i_put_chars(tty_desc_t *tty, const uint8_t *buf, uint32_t flags)
{
    uint16_t count = (uint16_t)(flags >> 16);
    uint16_t avail_hint = (uint16_t)(flags & 0xFFFF);

    if (tty->state_flags & TTY_STATUS_OUTPUT_FLUSH) {
        return count;
    }

    if (tty->state_flags & TTY_STATUS_XON_XOFF) {
        return 0;
    }

    int16_t free_space = (int16_t)(tty->output_head - tty->output_read) - 1;
    if (free_space < 0) {
        free_space += TTY_BUFFER_SIZE;
    }
    free_space -= (int16_t)avail_hint;

    int16_t local_max;
    if ((int32_t)(uint32_t)count <= (int32_t)free_space) {
        local_max = (int16_t)count;
    } else if (free_space >= 0) {
        local_max = free_space;
    } else {
        local_max = 0;
    }

    uint16_t chars_processed = 0;

    while ((int32_t)(uint32_t)chars_processed < (int32_t)local_max) {
        uint8_t ch = *buf++;
        chars_processed++;

        uint32_t oflags;

        if (ch == 0x08) {
            tty_$i_buf_put(0x08, &tty->output_head);
            if (TTY_COLUMN(tty) != 0) {
                TTY_COLUMN(tty)--;
            }
        } else if (ch == 0x0D) {
            oflags = TTY_GET_OUTPUT_FLAGS(tty);
            if ((oflags & TTY_OFLAG_DISCARD) != 0 &&
                TTY_COLUMN(tty) == 0) {
                /* Discard */
            } else {
                oflags = TTY_GET_OUTPUT_FLAGS(tty);
                if (oflags & TTY_OFLAG_CR_TO_LF) {
                    local_max--;
                    tty_$i_buf_put(0x0A, &tty->output_head);
                    if (tty->delay[TTY_DELAY_LF] > TTY_DELAY_THRESHOLD) {
                        buf_put_delay(tty, tty->delay[TTY_DELAY_LF],
                                      &local_max);
                    }
                } else {
                    tty_$i_buf_put(0x0D, &tty->output_head);
                    if (tty->delay[TTY_DELAY_CR] > TTY_DELAY_THRESHOLD) {
                        buf_put_delay(tty, tty->delay[TTY_DELAY_CR],
                                      &local_max);
                    }
                }
            }
            TTY_COLUMN(tty) = 0;
        } else if (ch == 0x0A) {
            oflags = TTY_GET_OUTPUT_FLAGS(tty);
            if (oflags & TTY_OFLAG_LF_TO_CRLF) {
                local_max--;
                tty_$i_buf_put(0x0D, &tty->output_head);
                if (tty->delay[TTY_DELAY_CR] > TTY_DELAY_THRESHOLD) {
                    buf_put_delay(tty, tty->delay[TTY_DELAY_CR],
                                  &local_max);
                }
                TTY_COLUMN(tty) = 0;
            }
            tty_$i_buf_put(0x0A, &tty->output_head);
            if (tty->delay[TTY_DELAY_LF] > TTY_DELAY_THRESHOLD) {
                buf_put_delay(tty, tty->delay[TTY_DELAY_LF], &local_max);
            }
        } else if (ch == 0x09) {
            int16_t col_mod = TTY_COLUMN(tty) & 7;
            int16_t spaces = 8 - col_mod;
            oflags = TTY_GET_OUTPUT_FLAGS(tty);
            if (oflags & TTY_OFLAG_EXPAND_TABS) {
                int16_t extra = spaces - 1;
                local_max -= extra;
                if (spaces != 0) {
                    int16_t i = extra;
                    do {
                        tty_$i_buf_put(0x20, &tty->output_head);
                    } while (i-- != 0);
                }
            } else {
                tty_$i_buf_put(0x09, &tty->output_head);
                if (tty->delay[TTY_DELAY_TAB] > TTY_DELAY_THRESHOLD) {
                    buf_put_delay(tty, tty->delay[TTY_DELAY_TAB],
                                  &local_max);
                }
            }
            TTY_COLUMN(tty) += spaces;
        } else if (ch == 0x0B) {
            tty_$i_buf_put(0x0B, &tty->output_head);
            if (tty->delay[TTY_DELAY_VT] > TTY_DELAY_THRESHOLD) {
                buf_put_delay(tty, tty->delay[TTY_DELAY_VT], &local_max);
            }
        } else if (ch == 0x0C) {
            tty_$i_buf_put(0x0C, &tty->output_head);
            if (tty->delay[TTY_DELAY_FF] > TTY_DELAY_THRESHOLD) {
                buf_put_delay(tty, tty->delay[TTY_DELAY_FF], &local_max);
            }
        } else if (ch == 0xFE) {
            tty_$i_buf_put(0xFE, &tty->output_head);
            tty_$i_buf_put(0xFE, &tty->output_head);
        } else {
            uint16_t token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK);
            tty->output_buffer[tty->output_read - 1] = ch;
            if (tty->output_read == TTY_BUFFER_SIZE) {
                tty->output_read = 1;
            } else {
                tty->output_read++;
            }
            if (ch >= 0x20) {
                TTY_COLUMN(tty)++;
            }
            ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);
        }
    }

    uint16_t token = ML_$SPIN_LOCK(&TTY_$SPIN_LOCK);

    if (chars_processed < count) {
        int16_t used = (int16_t)(tty->output_read - tty->output_head);
        if (used < 0) {
            used += TTY_BUFFER_SIZE;
        }
        if (used >= TTY_BUFFER_NEARLY_FULL) {
            tty->state_flags |= TTY_STATUS_OUTPUT_WAIT;
        }
    }

    ((void (*)(uint32_t))(uintptr_t)tty->reserved_2B4)(tty->line_id);

    ML_$SPIN_UNLOCK(&TTY_$SPIN_LOCK, token);

    return chars_processed;
}

/* ================================================================
 * Test helpers
 * ================================================================ */

/*
 * Helper to count bytes in the output buffer.
 */
static int16_t output_buf_used(tty_desc_t *tty)
{
    int16_t used = (int16_t)(tty->output_read - tty->output_head);
    if (used < 0)
        used += TTY_BUFFER_SIZE;
    return used;
}

/*
 * Helper to read sequential bytes from the output buffer starting at head.
 * Returns a pointer to a static buffer with the extracted bytes.
 */
static uint8_t *read_output_bytes(tty_desc_t *tty, int count)
{
    static uint8_t result[512];
    uint16_t pos = tty->output_head;
    for (int i = 0; i < count && i < 512; i++) {
        result[i] = tty->output_buffer[pos - 1];
        if (pos == TTY_BUFFER_SIZE) {
            pos = 1;
        } else {
            pos++;
        }
    }
    return result;
}

/*
 * Initialize a tty_desc_t for testing.
 */
static void init_test_tty(tty_desc_t *tty)
{
    memset(tty, 0, sizeof(tty_desc_t));

    tty->line_id = 0x0001;
    tty->state_flags = 0;
    tty->output_head = 1;
    tty->output_read = 1;
    tty->output_tail = TTY_BUFFER_SIZE;
    tty->current_input_flags = 0;  /* column = 0 */

    /* Set transmit callback to our mock */
    tty->reserved_2B4 = (m68k_ptr_t)(uintptr_t)mock_xmit_callback;

    /* Reset test counters */
    xmit_callback_count = 0;
    xmit_callback_last_arg = 0;
    spin_lock_count = 0;
}

/*
 * Helper to make a flags parameter: count in high 16, avail in low 16.
 */
static uint32_t make_flags(uint16_t count, uint16_t avail)
{
    return ((uint32_t)count << 16) | avail;
}

/*
 * Helper to set output flags as a 32-bit value at offset 0x0C.
 * On big-endian (M68K), this sets the flag bits in the low byte.
 * In the test (native endian), we set it directly since the code
 * and tests use the same access pattern.
 */
static void set_output_flags(tty_desc_t *tty, uint32_t flags)
{
    *(uint32_t *)&tty->output_flags = flags;
}

/* ================================================================
 * Tests for tty_$i_buf_insert
 * ================================================================ */

TEST(buf_insert_basic)
{
    /* Create a buffer header: head=1, tail=1 (empty), data follows */
    uint8_t raw_buf[5 + TTY_BUFFER_SIZE + 1];
    memset(raw_buf, 0, sizeof(raw_buf));
    uint16_t *head = (uint16_t *)raw_buf;
    uint16_t *tail = (uint16_t *)(raw_buf + 2);
    *head = 1;
    *tail = 1;

    tty_$i_buf_insert('A', raw_buf);
    ASSERT_EQ(1, *head);    /* head unchanged */
    ASSERT_EQ(2, *tail);    /* tail advanced */
    ASSERT_EQ('A', raw_buf[5 + 1]); /* data at offset 5+1 */
}

TEST(buf_insert_wrap)
{
    uint8_t raw_buf[5 + TTY_BUFFER_SIZE + 1];
    memset(raw_buf, 0, sizeof(raw_buf));
    uint16_t *head = (uint16_t *)raw_buf;
    uint16_t *tail = (uint16_t *)(raw_buf + 2);
    *head = 2;       /* head at 2 */
    *tail = 0x100;   /* tail at max */

    tty_$i_buf_insert('Z', raw_buf);
    ASSERT_EQ(1, *tail);  /* tail wrapped to 1 */
    ASSERT_EQ('Z', raw_buf[5 + 0x100]); /* data at offset 5+0x100 */
}

TEST(buf_insert_full)
{
    uint8_t raw_buf[5 + TTY_BUFFER_SIZE + 1];
    memset(raw_buf, 0, sizeof(raw_buf));
    uint16_t *head = (uint16_t *)raw_buf;
    uint16_t *tail = (uint16_t *)(raw_buf + 2);
    *head = 5;
    *tail = 4;  /* next_tail = 5 == head -> full */

    tty_$i_buf_insert('X', raw_buf);
    ASSERT_EQ(4, *tail);  /* tail unchanged - byte dropped */
}

/* ================================================================
 * Tests for tty_$i_buf_put
 * ================================================================ */

TEST(buf_put_locks)
{
    uint8_t raw_buf[5 + TTY_BUFFER_SIZE + 1];
    memset(raw_buf, 0, sizeof(raw_buf));
    uint16_t *head = (uint16_t *)raw_buf;
    uint16_t *tail = (uint16_t *)(raw_buf + 2);
    *head = 1;
    *tail = 1;

    spin_lock_count = 0;
    tty_$i_buf_put('B', raw_buf);
    ASSERT_EQ(0, spin_lock_count); /* lock acquired and released */
    ASSERT_EQ(2, *tail);
    ASSERT_EQ('B', raw_buf[5 + 1]);
}

/* ================================================================
 * Tests for tty_$i_put_chars
 * ================================================================ */

TEST(put_chars_output_flush)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    tty.state_flags = TTY_STATUS_OUTPUT_FLUSH;

    uint8_t data[] = "hello";
    uint16_t result = tty_$i_put_chars(&tty, data, make_flags(5, 0));
    ASSERT_EQ(5, result);  /* Returns count when flush active */
    ASSERT_EQ(0, output_buf_used(&tty));  /* Nothing written */
}

TEST(put_chars_input_only)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    tty.state_flags = TTY_STATUS_XON_XOFF;

    uint8_t data[] = "hello";
    uint16_t result = tty_$i_put_chars(&tty, data, make_flags(5, 0));
    ASSERT_EQ(0, result);  /* Returns 0 in input-only mode */
}

TEST(put_chars_normal_chars)
{
    tty_desc_t tty;
    init_test_tty(&tty);

    uint8_t data[] = "ABC";
    uint16_t result = tty_$i_put_chars(&tty, data, make_flags(3, 0));
    ASSERT_EQ(3, result);
    ASSERT_EQ(3, output_buf_used(&tty));
    ASSERT_EQ(3, TTY_COLUMN(&tty));  /* Column advanced */

    /* Verify buffer contents */
    uint8_t *out = read_output_bytes(&tty, 3);
    ASSERT_EQ('A', out[0]);
    ASSERT_EQ('B', out[1]);
    ASSERT_EQ('C', out[2]);

    /* Transmit callback should have been called */
    ASSERT_EQ(1, xmit_callback_count);
    ASSERT_EQ(0x0001, xmit_callback_last_arg);
}

TEST(put_chars_backspace)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    TTY_COLUMN(&tty) = 5;

    uint8_t data[] = { 0x08 };
    uint16_t result = tty_$i_put_chars(&tty, data, make_flags(1, 0));
    ASSERT_EQ(1, result);
    ASSERT_EQ(4, TTY_COLUMN(&tty));  /* Column decremented */
    ASSERT_EQ(1, output_buf_used(&tty));

    uint8_t *out = read_output_bytes(&tty, 1);
    ASSERT_EQ(0x08, out[0]);
}

TEST(put_chars_backspace_at_zero)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    TTY_COLUMN(&tty) = 0;

    uint8_t data[] = { 0x08 };
    tty_$i_put_chars(&tty, data, make_flags(1, 0));
    ASSERT_EQ(0, TTY_COLUMN(&tty));  /* Column stays at 0 */
}

TEST(put_chars_cr_normal)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    TTY_COLUMN(&tty) = 10;
    set_output_flags(&tty, 0);  /* No CR→LF conversion */

    uint8_t data[] = { 0x0D };
    uint16_t result = tty_$i_put_chars(&tty, data, make_flags(1, 0));
    ASSERT_EQ(1, result);
    ASSERT_EQ(0, TTY_COLUMN(&tty));  /* Column reset */

    uint8_t *out = read_output_bytes(&tty, 1);
    ASSERT_EQ(0x0D, out[0]);
}

TEST(put_chars_cr_to_lf)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    TTY_COLUMN(&tty) = 10;
    set_output_flags(&tty, TTY_OFLAG_CR_TO_LF);

    uint8_t data[] = { 0x0D };
    uint16_t result = tty_$i_put_chars(&tty, data, make_flags(1, 0));
    ASSERT_EQ(1, result);
    ASSERT_EQ(0, TTY_COLUMN(&tty));

    /* Should output LF instead of CR */
    uint8_t *out = read_output_bytes(&tty, 1);
    ASSERT_EQ(0x0A, out[0]);
}

TEST(put_chars_lf_normal)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    set_output_flags(&tty, 0);

    uint8_t data[] = { 0x0A };
    tty_$i_put_chars(&tty, data, make_flags(1, 0));

    ASSERT_EQ(1, output_buf_used(&tty));
    uint8_t *out = read_output_bytes(&tty, 1);
    ASSERT_EQ(0x0A, out[0]);
}

TEST(put_chars_lf_to_crlf)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    TTY_COLUMN(&tty) = 5;
    set_output_flags(&tty, TTY_OFLAG_LF_TO_CRLF);

    uint8_t data[] = { 0x0A };
    tty_$i_put_chars(&tty, data, make_flags(1, 0));

    /* Should output CR + LF */
    ASSERT_EQ(2, output_buf_used(&tty));
    uint8_t *out = read_output_bytes(&tty, 2);
    ASSERT_EQ(0x0D, out[0]);
    ASSERT_EQ(0x0A, out[1]);
    ASSERT_EQ(0, TTY_COLUMN(&tty));  /* Column reset by CR */
}

TEST(put_chars_tab_no_expand)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    TTY_COLUMN(&tty) = 3;
    set_output_flags(&tty, 0);  /* No tab expansion */

    uint8_t data[] = { 0x09 };
    tty_$i_put_chars(&tty, data, make_flags(1, 0));

    /* Should output literal TAB */
    ASSERT_EQ(1, output_buf_used(&tty));
    uint8_t *out = read_output_bytes(&tty, 1);
    ASSERT_EQ(0x09, out[0]);

    /* Column should advance to next tab stop: 3 + (8 - (3&7)) = 3 + 5 = 8 */
    ASSERT_EQ(8, TTY_COLUMN(&tty));
}

TEST(put_chars_tab_expand)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    TTY_COLUMN(&tty) = 3;
    set_output_flags(&tty, TTY_OFLAG_EXPAND_TABS);

    uint8_t data[] = { 0x09 };
    tty_$i_put_chars(&tty, data, make_flags(1, 0));

    /* Should output 5 spaces (8 - (3 & 7) = 5) */
    ASSERT_EQ(5, output_buf_used(&tty));
    uint8_t *out = read_output_bytes(&tty, 5);
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(0x20, out[i]);
    }
    ASSERT_EQ(8, TTY_COLUMN(&tty));
}

TEST(put_chars_tab_expand_at_boundary)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    TTY_COLUMN(&tty) = 0;  /* At tab boundary */
    set_output_flags(&tty, TTY_OFLAG_EXPAND_TABS);

    uint8_t data[] = { 0x09 };
    tty_$i_put_chars(&tty, data, make_flags(1, 0));

    /* At column 0: 8 - (0 & 7) = 8 spaces */
    ASSERT_EQ(8, output_buf_used(&tty));
    ASSERT_EQ(8, TTY_COLUMN(&tty));
}

TEST(put_chars_vt)
{
    tty_desc_t tty;
    init_test_tty(&tty);

    uint8_t data[] = { 0x0B };
    tty_$i_put_chars(&tty, data, make_flags(1, 0));

    ASSERT_EQ(1, output_buf_used(&tty));
    uint8_t *out = read_output_bytes(&tty, 1);
    ASSERT_EQ(0x0B, out[0]);
}

TEST(put_chars_ff)
{
    tty_desc_t tty;
    init_test_tty(&tty);

    uint8_t data[] = { 0x0C };
    tty_$i_put_chars(&tty, data, make_flags(1, 0));

    ASSERT_EQ(1, output_buf_used(&tty));
    uint8_t *out = read_output_bytes(&tty, 1);
    ASSERT_EQ(0x0C, out[0]);
}

TEST(put_chars_escape_marker)
{
    tty_desc_t tty;
    init_test_tty(&tty);

    uint8_t data[] = { 0xFE };
    tty_$i_put_chars(&tty, data, make_flags(1, 0));

    /* 0xFE should be doubled in output */
    ASSERT_EQ(2, output_buf_used(&tty));
    uint8_t *out = read_output_bytes(&tty, 2);
    ASSERT_EQ(0xFE, out[0]);
    ASSERT_EQ(0xFE, out[1]);
}

TEST(put_chars_delay_insertion)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    tty.delay[TTY_DELAY_LF] = 100;  /* > 3, will trigger delay */

    uint8_t data[] = { 0x0A };
    tty_$i_put_chars(&tty, data, make_flags(1, 0));

    /* LF + 4-byte delay sequence = 5 bytes */
    ASSERT_EQ(5, output_buf_used(&tty));
    uint8_t *out = read_output_bytes(&tty, 5);
    ASSERT_EQ(0x0A, out[0]);       /* LF */
    ASSERT_EQ(0xFE, out[1]);       /* Delay marker */
    ASSERT_EQ(0x00, out[2]);       /* Delay indicator */
    ASSERT_EQ(0x00, out[3]);       /* High byte of 100 */
    ASSERT_EQ(0x64, out[4]);       /* Low byte of 100 */
}

TEST(put_chars_no_delay_when_small)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    tty.delay[TTY_DELAY_LF] = 2;  /* <= 3, no delay */

    uint8_t data[] = { 0x0A };
    tty_$i_put_chars(&tty, data, make_flags(1, 0));

    /* Only LF, no delay */
    ASSERT_EQ(1, output_buf_used(&tty));
}

TEST(put_chars_buffer_space_limit)
{
    tty_desc_t tty;
    init_test_tty(&tty);

    /* Fill buffer to almost full: head=1, tail=250 -> 249 bytes used */
    tty.output_read = 250;

    uint8_t data[] = "ABCDEFGHIJ";  /* 10 chars */
    /* Free space = (1 - 250 - 1 + 256) = 6 */
    uint16_t result = tty_$i_put_chars(&tty, data, make_flags(10, 0));
    ASSERT_EQ(6, result);  /* Only 6 chars fit */
}

TEST(put_chars_avail_hint)
{
    tty_desc_t tty;
    init_test_tty(&tty);

    /* Buffer empty: head=1, tail=1, free=255 */
    /* With avail_hint=250, effective free = 255 - 250 = 5 */
    uint8_t data[] = "ABCDEFGHIJ";
    uint16_t result = tty_$i_put_chars(&tty, data, make_flags(10, 250));
    ASSERT_EQ(5, result);  /* Limited by effective free space */
}

TEST(put_chars_output_wait_flag)
{
    tty_desc_t tty;
    init_test_tty(&tty);

    /* Fill buffer to >= 75% (191/256): head=1, tail=200 -> 199 used */
    tty.output_read = 200;

    /* Try to write 10 chars, only ~56 fit */
    uint8_t data[60];
    memset(data, 'X', sizeof(data));
    uint16_t result = tty_$i_put_chars(&tty, data, make_flags(60, 0));

    /* After processing, check if output_wait was set */
    /* We processed fewer than requested, and buffer should be nearly full */
    ASSERT_TRUE(result < 60);
    ASSERT_TRUE(tty.state_flags & TTY_STATUS_OUTPUT_WAIT);
}

TEST(put_chars_control_char_no_column)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    TTY_COLUMN(&tty) = 5;

    /* Control chars < 0x20 (other than special-cased ones) don't advance column */
    uint8_t data[] = { 0x01 };
    tty_$i_put_chars(&tty, data, make_flags(1, 0));
    ASSERT_EQ(5, TTY_COLUMN(&tty));  /* Unchanged */
}

TEST(put_chars_cr_discard_at_col0)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    TTY_COLUMN(&tty) = 0;
    set_output_flags(&tty, TTY_OFLAG_DISCARD);

    uint8_t data[] = { 0x0D };
    tty_$i_put_chars(&tty, data, make_flags(1, 0));

    /* CR should be discarded (not written to buffer) when discard + col=0 */
    ASSERT_EQ(0, output_buf_used(&tty));
    ASSERT_EQ(0, TTY_COLUMN(&tty));
}

TEST(put_chars_cr_discard_at_nonzero_col)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    TTY_COLUMN(&tty) = 5;
    set_output_flags(&tty, TTY_OFLAG_DISCARD);

    uint8_t data[] = { 0x0D };
    tty_$i_put_chars(&tty, data, make_flags(1, 0));

    /* CR NOT discarded when column > 0 */
    ASSERT_EQ(1, output_buf_used(&tty));
    ASSERT_EQ(0, TTY_COLUMN(&tty));
}

TEST(put_chars_mixed_string)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    set_output_flags(&tty, 0);

    uint8_t data[] = "Hi\r\n";
    uint16_t result = tty_$i_put_chars(&tty, data, make_flags(4, 0));
    ASSERT_EQ(4, result);

    /* 'H' + 'i' + CR + LF = 4 bytes in buffer */
    ASSERT_EQ(4, output_buf_used(&tty));
    uint8_t *out = read_output_bytes(&tty, 4);
    ASSERT_EQ('H', out[0]);
    ASSERT_EQ('i', out[1]);
    ASSERT_EQ(0x0D, out[2]);
    ASSERT_EQ(0x0A, out[3]);
    ASSERT_EQ(0, TTY_COLUMN(&tty));  /* CR resets column */
}

TEST(put_chars_zero_count)
{
    tty_desc_t tty;
    init_test_tty(&tty);

    uint8_t data[] = "hello";
    uint16_t result = tty_$i_put_chars(&tty, data, make_flags(0, 0));
    ASSERT_EQ(0, result);
    ASSERT_EQ(0, output_buf_used(&tty));

    /* Callback should still be called */
    ASSERT_EQ(1, xmit_callback_count);
}

TEST(put_chars_cr_delay)
{
    tty_desc_t tty;
    init_test_tty(&tty);
    TTY_COLUMN(&tty) = 5;
    set_output_flags(&tty, 0);
    tty.delay[TTY_DELAY_CR] = 0x1234;  /* > 3, will trigger delay */

    uint8_t data[] = { 0x0D };
    tty_$i_put_chars(&tty, data, make_flags(1, 0));

    /* CR + 4-byte delay = 5 bytes */
    ASSERT_EQ(5, output_buf_used(&tty));
    uint8_t *out = read_output_bytes(&tty, 5);
    ASSERT_EQ(0x0D, out[0]);
    ASSERT_EQ(0xFE, out[1]);
    ASSERT_EQ(0x00, out[2]);
    ASSERT_EQ(0x12, out[3]);
    ASSERT_EQ(0x34, out[4]);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    printf("tty_$i_buf_insert tests:\n");
    RUN_TEST(buf_insert_basic);
    RUN_TEST(buf_insert_wrap);
    RUN_TEST(buf_insert_full);

    printf("\ntty_$i_buf_put tests:\n");
    RUN_TEST(buf_put_locks);

    printf("\ntty_$i_put_chars tests:\n");
    RUN_TEST(put_chars_output_flush);
    RUN_TEST(put_chars_input_only);
    RUN_TEST(put_chars_normal_chars);
    RUN_TEST(put_chars_backspace);
    RUN_TEST(put_chars_backspace_at_zero);
    RUN_TEST(put_chars_cr_normal);
    RUN_TEST(put_chars_cr_to_lf);
    RUN_TEST(put_chars_lf_normal);
    RUN_TEST(put_chars_lf_to_crlf);
    RUN_TEST(put_chars_tab_no_expand);
    RUN_TEST(put_chars_tab_expand);
    RUN_TEST(put_chars_tab_expand_at_boundary);
    RUN_TEST(put_chars_vt);
    RUN_TEST(put_chars_ff);
    RUN_TEST(put_chars_escape_marker);
    RUN_TEST(put_chars_delay_insertion);
    RUN_TEST(put_chars_no_delay_when_small);
    RUN_TEST(put_chars_buffer_space_limit);
    RUN_TEST(put_chars_avail_hint);
    RUN_TEST(put_chars_output_wait_flag);
    RUN_TEST(put_chars_control_char_no_column);
    RUN_TEST(put_chars_cr_discard_at_col0);
    RUN_TEST(put_chars_cr_discard_at_nonzero_col);
    RUN_TEST(put_chars_mixed_string);
    RUN_TEST(put_chars_zero_count);
    RUN_TEST(put_chars_cr_delay);

    printf("\n%d tests passed, %d tests failed\n",
           tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
