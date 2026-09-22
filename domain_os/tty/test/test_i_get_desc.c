/*
 * tty/test/test_i_get_desc.c - Unit tests for TTY_$I_GET_DESC (0x00E66738).
 * Pins the DTTE handler lookup, the 0xB000D status, the console-only
 * discipline switch (with the shared word-2 cell) and unblank, and the two
 * error exits.
 */
#include "tty/tty_internal.h"
#include "term/term.h"
#include "smd/smd.h"
#include "dtty/dtty.h"
#include "test_harness.h"

term_data_t TERM_$DATA;
const uint16_t term_$const_word_2 = 2;
int8_t DTTY_$USE_DTTY;

static status_$t real_line_status;
static short real_line_result;
short TERM_$GET_REAL_LINE(short line_num, status_$t *status_ret)
{
    logf_call("real(%d);", line_num);
    *status_ret = real_line_status;
    return real_line_result;
}

static const void *disc_ptr_seen;
static short disc_line_seen;
void TERM_$SET_DISCIPLINE(short *line_ptr, void *discipline, status_$t *status_ret)
{
    disc_line_seen = *line_ptr;
    disc_ptr_seen = discipline;
    logf_call("setdisc(%d,%d);", *line_ptr, *(const uint16_t *)discipline);
    *status_ret = 0x55;
}

void SMD_$UNBLANK(void) { logf_call("unblank;"); }

#include "../i_get_desc.c"

static tty_desc_t descs[4];

static void reset(void)
{
    int i;
    /* the handler cells are 32-bit VAs: anchor the host arena on descs[] */
    ARCH_HOST_VA_BASE = (uintptr_t)&descs[0] - 0x10000;
    memset(&TERM_$DATA, 0, sizeof(TERM_$DATA));
    for (i = 0; i < 4; i++) {
        TERM_$DATA.dtte[i].handler_ptr = ARCH_PTR_TO_VA(&descs[i]);
    }
    real_line_status = status_$ok;
    real_line_result = 0;
    DTTY_$USE_DTTY = 0;
    log_reset();
}

TEST(real_line_failure_returns_early)
{
    status_$t st = 0;
    reset();
    real_line_status = 0x000b0007;
    TTY_$I_GET_DESC(5, &st);
    ASSERT_EQ(0x000b0007, st);
    ASSERT_STR("real(5);", call_log);
}

TEST(no_handler_is_not_implemented)
{
    status_$t st = 0;
    reset();
    real_line_result = 2;
    TERM_$DATA.dtte[2].handler_ptr = 0;
    TTY_$I_GET_DESC(2, &st);
    ASSERT_EQ(status_$requested_line_or_operation_not_implemented, st);
    ASSERT_STR("real(2);", call_log);
}

TEST(serial_line_no_console_work)
{
    status_$t st = 0;
    reset();
    real_line_result = 1;
    tty_desc_t *d = TTY_$I_GET_DESC(1, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ((unsigned long)&descs[1], (unsigned long)d);
    ASSERT_STR("real(1);", call_log);
}

TEST(console_switches_discipline_and_unblanks)
{
    status_$t st = 0;
    reset();
    real_line_result = 0;
    TERM_$DATA.dtte[0].discipline = 0;
    tty_desc_t *d = TTY_$I_GET_DESC(0, &st);
    ASSERT_EQ((unsigned long)&descs[0], (unsigned long)d);
    ASSERT_STR("real(0);setdisc(0,2);unblank;", call_log);
    ASSERT_EQ((unsigned long)&term_$const_word_2, (unsigned long)disc_ptr_seen);
    ASSERT_EQ(status_$ok, st);            /* local status, not the caller's */
}

TEST(console_discipline_2_only_unblanks)
{
    status_$t st = 0;
    reset();
    TERM_$DATA.dtte[0].discipline = 2;
    TTY_$I_GET_DESC(0, &st);
    ASSERT_STR("real(0);unblank;", call_log);
}

TEST(console_dtty_in_use_only_unblanks)
{
    status_$t st = 0;
    reset();
    DTTY_$USE_DTTY = (int8_t)0xFF;
    TERM_$DATA.dtte[0].discipline = 0;
    TTY_$I_GET_DESC(0, &st);
    ASSERT_STR("real(0);unblank;", call_log);
}

TEST(console_uses_logical_line_for_discipline)
{
    status_$t st = 0;
    reset();
    real_line_result = 0;
    TTY_$I_GET_DESC(3, &st);               /* logical 3 maps to real 0 */
    ASSERT_EQ(3, disc_line_seen);
}

int main(void)
{
    printf("TTY_$I_GET_DESC tests\n");
    RUN_TEST(real_line_failure_returns_early);
    RUN_TEST(no_handler_is_not_implemented);
    RUN_TEST(serial_line_no_console_work);
    RUN_TEST(console_switches_discipline_and_unblanks);
    RUN_TEST(console_discipline_2_only_unblanks);
    RUN_TEST(console_dtty_in_use_only_unblanks);
    RUN_TEST(console_uses_logical_line_for_discipline);
    TEST_SUMMARY();
}
