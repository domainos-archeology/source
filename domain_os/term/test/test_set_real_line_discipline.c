/*
 * term/test/test_set_real_line_discipline.c - Unit tests for
 * TERM_$SET_REAL_LINE_DISCIPLINE (0x00E1AB62) and TERM_$WRITE (0x00E668D8)
 *
 * The real .c files are #included; ML_$SPIN_LOCK/UNLOCK, DTTY_$RELOAD_FONT,
 * TERM_$SEND_KBD_STRING, SUMA_$RCV, TTY_$K_PUT and TERM_$STATUS_CONVERT are
 * mocked.  The SIO descriptor and the keyboard handler cell live in a host
 * arena addressed through ARCH_HOST_VA_BASE.
 */

#include <stdio.h>
#include <string.h>

#include "term/term_internal.h"

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("%-44s %s\n", #name, current_failed ? "FAIL" : "ok");          \
    } while (0)
#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long e_ = (long long)(expected), a_ = (long long)(actual);       \
        if (e_ != a_) {                                                       \
            printf("  %s:%d: expected 0x%llx, got 0x%llx\n",                  \
                   __FILE__, __LINE__, (unsigned long long)e_,                \
                   (unsigned long long)a_);                                   \
            if (!current_failed) { current_failed = 1; tests_failed++; }      \
        }                                                                     \
    } while (0)
#define ASSERT_PTR_EQ(e, a) ASSERT_EQ((uintptr_t)(e), (uintptr_t)(a))

term_data_t TERM_$DATA;
uint16_t TERM_$KBD_STRING_LEN = 5;
const uint16_t term_$const_word_0 = 0;

static int lock_calls, unlock_calls, lock_depth;
static void *lock_target;
static ml_$spin_token_t unlock_token;
ml_$spin_token_t ML_$SPIN_LOCK(void *lockp)
{
    lock_calls++; lock_depth++; lock_target = lockp;
    return (ml_$spin_token_t)(0x0700 + lock_calls);
}
void (ML_$SPIN_UNLOCK)(void *lockp, uint32_t token_slot)
{
    ml_$spin_token_t token = (ml_$spin_token_t)ARCH_PASCAL_SLOT_WORD(token_slot); (void)token;
    unlock_calls++; lock_depth--; lock_target = lockp; unlock_token = token;
}

static int reload_calls, send_calls;
static void *send_str; static void *send_len;
static int lock_depth_at_reload;
void DTTY_$RELOAD_FONT(void) { reload_calls++; lock_depth_at_reload = lock_depth; }
void TERM_$SEND_KBD_STRING(void *str, void *length) { send_calls++; send_str = str; send_len = length; }
void SUMA_$RCV(uint32_t a, uint8_t b) { (void)a; (void)b; }

static int put_calls; static short *put_line; static const uint16_t *put_opt;
static void *put_buf; static ushort put_count; static ushort *put_count_ptr;
void TTY_$K_PUT(short *line_ptr, void *options, void *buffer, ushort *count,
                status_$t *status)
{
    put_calls++; put_line = line_ptr; put_opt = options; put_buf = buffer;
    put_count = *count; put_count_ptr = count; *status = 0x00360002;
}
static int convert_calls; static status_$t *convert_arg;
void TERM_$STATUS_CONVERT(status_$t *s) { convert_calls++; convert_arg = s; }

#include "../set_real_line_discipline.c"
#include "../write.c"

static uint8_t arena[0x400];
#define DESC_VA  0x100
#define KBD_VA   0x300

static void reset(void)
{
    memset(&TERM_$DATA, 0, sizeof(TERM_$DATA));
    memset(arena, 0, sizeof(arena));
    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    TERM_$MAX_DTTE = 3;
    TERM_$DATA.ptr_tty_i_rcv = 0xE1B92A;
    TERM_$DATA.ptr_tty_i_drain = 0xE1B394;
    TERM_$DATA.ptr_tty_i_hup = 0xE1BECE;
    TERM_$DATA.ptr_tty_i_int = 0xE1BEA8;
    TERM_$DATA.ptr_tty_i_rcv_alt = 0xE1B92A;
    lock_calls = unlock_calls = lock_depth = 0;
    reload_calls = send_calls = put_calls = convert_calls = 0;
}

TEST(line_checks)
{
    unsigned short line; short disc = 0; status_$t st;
    reset();
    line = 4; st = 0;
    TERM_$SET_REAL_LINE_DISCIPLINE(&line, &disc, &st);
    ASSERT_EQ(status_$invalid_line_number, st);
    line = 3; st = 0;
    TERM_$SET_REAL_LINE_DISCIPLINE(&line, &disc, &st);
    ASSERT_EQ(status_$requested_line_or_operation_not_implemented, st);
    ASSERT_EQ(0, lock_calls);
}

TEST(discipline_0_restores_tty_handlers)
{
    unsigned short line = 1; short disc = 0; status_$t st = 0x55;
    sio_desc_t *desc = (sio_desc_t *)(arena + DESC_VA);
    reset();
    TERM_$DATA.dtte[1].tty_handler = DESC_VA;
    TERM_$DATA.dtte[1].handler_ptr = 0x4444;
    TERM_$DATA.dtte[1].discipline = 9;
    TERM_$SET_REAL_LINE_DISCIPLINE(&line, &disc, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(1, lock_calls); ASSERT_EQ(1, unlock_calls);
    ASSERT_PTR_EQ(&TERM_$DATA.tty_spin_lock, lock_target);
    ASSERT_EQ(0x0701, unlock_token);
    ASSERT_EQ(0x4444, desc->owner);
    ASSERT_EQ(0xE1B92A, desc->rcv_handler);
    ASSERT_EQ(0xE1B394, desc->drain_handler);
    ASSERT_EQ(0xE1BECE, desc->dcd_handler);
    ASSERT_EQ(0xE1BEA8, desc->special_rcv);
    ASSERT_EQ(0, TERM_$DATA.dtte[1].discipline);
}

TEST(discipline_3_installs_suma)
{
    unsigned short line = 0; short disc = 3; status_$t st = 0;
    sio_desc_t *desc = (sio_desc_t *)(arena + DESC_VA);
    reset();
    TERM_$DATA.dtte[0].tty_handler = DESC_VA;
    desc->owner = 0x77;
    TERM_$SET_REAL_LINE_DISCIPLINE(&line, &disc, &st);
    ASSERT_EQ(ARCH_PTR_TO_VA(SUMA_$RCV), desc->rcv_handler);
    ASSERT_EQ(0x77, desc->owner);                   /* untouched */
    ASSERT_EQ(3, TERM_$DATA.dtte[0].discipline);
    ASSERT_EQ(1, unlock_calls);
}

TEST(discipline_0_without_descriptor_is_invalid_line)
{
    unsigned short line = 0; short disc = 0; status_$t st = 0;
    reset();
    TERM_$DATA.dtte[0].discipline = 7;
    TERM_$SET_REAL_LINE_DISCIPLINE(&line, &disc, &st);
    ASSERT_EQ(status_$invalid_line_number, st);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(7, TERM_$DATA.dtte[0].discipline);    /* not stored */
}

TEST(discipline_1_clears_kbd_handler)
{
    unsigned short line = 2; short disc = 1; status_$t st = 0;
    m68k_ptr_t *cell = (m68k_ptr_t *)(arena + KBD_VA);
    reset();
    *cell = 0xDEADBEEF;
    TERM_$DATA.dtte[2].alt_handler = KBD_VA;
    TERM_$SET_REAL_LINE_DISCIPLINE(&line, &disc, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, *cell);
    ASSERT_EQ(1, lock_calls); ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(0, reload_calls);
    ASSERT_EQ(1, TERM_$DATA.dtte[2].discipline);
}

TEST(discipline_2_installs_alt_and_replays)
{
    unsigned short line = 2; short disc = 2; status_$t st = 0;
    m68k_ptr_t *cell = (m68k_ptr_t *)(arena + KBD_VA);
    reset();
    TERM_$DATA.dtte[2].alt_handler = KBD_VA;
    TERM_$SET_REAL_LINE_DISCIPLINE(&line, &disc, &st);
    ASSERT_EQ(0xE1B92A, *cell);
    ASSERT_EQ(1, reload_calls);
    ASSERT_EQ(0, lock_depth_at_reload);             /* unlocked first */
    ASSERT_EQ(1, send_calls);
    ASSERT_PTR_EQ(TERM_$KBD_STRING_DATA, send_str);
    ASSERT_PTR_EQ(&TERM_$KBD_STRING_LEN, send_len);
    ASSERT_EQ(2, TERM_$DATA.dtte[2].discipline);

    TERM_$DATA.dtte[2].alt_handler = 0;
    st = 0;
    TERM_$SET_REAL_LINE_DISCIPLINE(&line, &disc, &st);
    ASSERT_EQ(status_$invalid_line_number, st);
}

TEST(other_discipline_is_only_recorded)
{
    unsigned short line = 1; short disc = 9; status_$t st = 0x55;
    reset();
    TERM_$SET_REAL_LINE_DISCIPLINE(&line, &disc, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(9, TERM_$DATA.dtte[1].discipline);
    disc = -1; st = 0;
    TERM_$SET_REAL_LINE_DISCIPLINE(&line, &disc, &st);
    ASSERT_EQ((uint16_t)-1, (uint16_t)TERM_$DATA.dtte[1].discipline);
}

TEST(write_uses_shared_word_and_copies_count)
{
    short line = 2; uint8_t buf[2]; unsigned short count = 7; status_$t st = 0;
    reset();
    TERM_$WRITE(&line, buf, &count, &st);
    ASSERT_EQ(1, put_calls);
    ASSERT_PTR_EQ(&line, put_line);
    ASSERT_PTR_EQ(&term_$const_word_0, put_opt);
    ASSERT_PTR_EQ(buf, put_buf);
    ASSERT_EQ(7, put_count);
    ASSERT_EQ(1, (uintptr_t)put_count_ptr != (uintptr_t)&count);
    ASSERT_EQ(1, convert_calls);
    ASSERT_PTR_EQ(&st, convert_arg);
    ASSERT_EQ(0x00360002, st);
}

int main(void)
{
    RUN_TEST(line_checks);
    RUN_TEST(discipline_0_restores_tty_handlers);
    RUN_TEST(discipline_3_installs_suma);
    RUN_TEST(discipline_0_without_descriptor_is_invalid_line);
    RUN_TEST(discipline_1_clears_kbd_handler);
    RUN_TEST(discipline_2_installs_alt_and_replays);
    RUN_TEST(other_discipline_is_only_recorded);
    RUN_TEST(write_uses_shared_word_and_copies_count);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
