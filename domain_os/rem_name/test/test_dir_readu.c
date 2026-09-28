/*
 * rem_name/test/test_dir_readu.c - unit tests for REM_NAME_$DIR_READU
 * (0x00E4AC2C).  REM_NAME_$READ_DIR and LOCATE_SERVER are mocked with
 * scripted per-call answers.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  %-52s ", #name);                  \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "rem_name/rem_name_internal.h"

rem_name_data_t rem_name_$data;

/* ---- REM_NAME_$READ_DIR mock ---- */
static int       rd_calls;
static status_$t rd_status[6];
static uint16_t  rd_count[6];
static uint32_t  rd_net_seen[6], rd_node_seen[6];
static uint32_t  rd_start_seen[6];
static void     *rd_buf_seen[6];
static uint16_t  rd_max_seen[6];

void REM_NAME_$READ_DIR(uint32_t net, uint32_t node, uid_t *dir_uid,
                        uint16_t start_index, void *entries_ret,
                        uint16_t max_entries, uint16_t *count_ret,
                        status_$t *status_ret)
{
    int n = rd_calls++;
    (void)dir_uid;
    rd_net_seen[n] = net;
    rd_node_seen[n] = node;
    rd_start_seen[n] = start_index;
    rd_buf_seen[n] = entries_ret;
    rd_max_seen[n] = max_entries;
    *count_ret = rd_count[n];
    *status_ret = rd_status[n];
}

/* ---- LOCATE_SERVER mock ---- */
static int       loc_calls;
static status_$t loc_status;

void LOCATE_SERVER(uint32_t *node_ret, uint32_t *net_ret, status_$t *status_ret)
{
    loc_calls++;
    *node_ret = 0x77;
    *net_ret = 0x88;
    *status_ret = loc_status;
}

#include "../dir_readu.c"

static uid_t   dir_uid = { 1, 2 };
static uint8_t buf[0x30 * 8];

static void reset(void)
{
    memset(&rem_name_$data, 0, sizeof(rem_name_$data));
    rem_name_$data.curr_node = 0x11;
    rem_name_$data.curr_net = 0x22;
    rd_calls = 0;
    memset(rd_status, 0, sizeof(rd_status));
    memset(rd_count, 0, sizeof(rd_count));
    loc_calls = 0;
    loc_status = status_$ok;
}

/* 0x00E4AC54-0x00E4AC5E -> 0x00E4ACFE */
TEST(zero_max_or_zero_continuation_is_ok_and_clears)
{
    int32_t cont = 0x00010005;
    uint16_t max = 0, count = 9;
    status_$t st = 0x55;

    reset();
    REM_NAME_$DIR_READU(&dir_uid, buf, &cont, &max, &count, &st);
    ASSERT_EQ(0, count);
    ASSERT_EQ(0, cont);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, rd_calls);

    cont = 0; max = 4; count = 9; st = 0x55;
    REM_NAME_$DIR_READU(&dir_uid, buf, &cont, &max, &count, &st);
    ASSERT_EQ(0, count);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, rd_calls);
}

/* two successful reads fill max; low word of continuation advances */
TEST(fills_in_several_reads)
{
    int32_t cont = 0x00A50001;
    uint16_t max = 5, count = 0;
    status_$t st = 0x55;

    reset();
    rd_count[0] = 3; rd_count[1] = 2;
    REM_NAME_$DIR_READU(&dir_uid, buf, &cont, &max, &count, &st);

    ASSERT_EQ(2, rd_calls);
    ASSERT_EQ(0x22, rd_net_seen[0]);
    ASSERT_EQ(0x11, rd_node_seen[0]);
    ASSERT_EQ(1, rd_start_seen[0]);
    ASSERT_EQ((uintptr_t)buf, (uintptr_t)rd_buf_seen[0]);
    ASSERT_EQ(5, rd_max_seen[0]);
    ASSERT_EQ(4, rd_start_seen[1]);
    ASSERT_EQ((uintptr_t)(buf + 3 * 0x30), (uintptr_t)rd_buf_seen[1]);
    ASSERT_EQ(2, rd_max_seen[1]);
    ASSERT_EQ(5, count);
    ASSERT_EQ(0x00A50006, cont);          /* high word kept */
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, loc_calls);
}

/* 0x00E4ACBC-0x00E4ACD2: replicated-root end codes */
TEST(replicated_root_end_accepts_partial)
{
    int32_t cont = 0x00000001;
    uint16_t max = 5, count = 0;
    status_$t st = 0x55;

    reset();
    rd_count[0] = 2; rd_status[0] = status_$ok;
    rd_count[1] = 1; rd_status[1] = status_$naming_last_entry_in_replicated_root_returned;
    REM_NAME_$DIR_READU(&dir_uid, buf, &cont, &max, &count, &st);
    ASSERT_EQ(2, rd_calls);
    ASSERT_EQ(3, count);
    ASSERT_EQ(0, cont);
    ASSERT_EQ(status_$ok, st);

    reset();
    cont = 1; count = 0; st = 0x55;
    rd_count[0] = 4; rd_status[0] = status_$naming_cannot_find_entry_in_replicated_root;
    REM_NAME_$DIR_READU(&dir_uid, buf, &cont, &max, &count, &st);
    ASSERT_EQ(4, count);
    ASSERT_EQ(0, cont);
    ASSERT_EQ(status_$ok, st);
}

/* 0x00E4ACD4-0x00E4ACDE: failure after progress -> quiet ok */
TEST(failure_after_progress_is_quiet)
{
    int32_t cont = 1;
    uint16_t max = 5, count = 0;
    status_$t st = 0x55;

    reset();
    rd_count[0] = 2; rd_status[0] = status_$ok;
    rd_count[1] = 0; rd_status[1] = 0x000E0033;
    REM_NAME_$DIR_READU(&dir_uid, buf, &cont, &max, &count, &st);
    ASSERT_EQ(2, rd_calls);
    ASSERT_EQ(2, count);
    ASSERT_EQ(0, cont);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, loc_calls);
}

/* failure on entry 1 -> locate a server and retry from it */
TEST(first_entry_failure_locates_and_retries)
{
    int32_t cont = 1;
    uint16_t max = 2, count = 0;
    status_$t st = 0x55;

    reset();
    rd_status[0] = 0x000E0033;
    rd_count[1] = 2; rd_status[1] = status_$ok;
    REM_NAME_$DIR_READU(&dir_uid, buf, &cont, &max, &count, &st);
    ASSERT_EQ(1, loc_calls);
    ASSERT_EQ(2, rd_calls);
    ASSERT_EQ(0x88, rd_net_seen[1]);
    ASSERT_EQ(0x77, rd_node_seen[1]);
    ASSERT_EQ(2, count);
    ASSERT_EQ(3, cont);
    ASSERT_EQ(status_$ok, st);

    /* a second failure after the locate is quiet (progressed set at 0x00E4ACFA) */
    reset();
    cont = 1; count = 0; st = 0x55;
    rd_status[0] = 0x000E0033;
    rd_status[1] = 0x000E0033;
    REM_NAME_$DIR_READU(&dir_uid, buf, &cont, &max, &count, &st);
    ASSERT_EQ(1, loc_calls);
    ASSERT_EQ(2, rd_calls);
    ASSERT_EQ(0, cont);
    ASSERT_EQ(status_$ok, st);
}

/* 0x00E4ACF2-0x00E4ACF8: LOCATE_SERVER failure keeps ITS status */
TEST(locate_failure_returns_locate_status)
{
    int32_t cont = 1;
    uint16_t max = 2, count = 0;
    status_$t st = 0x55;

    reset();
    rd_status[0] = 0x000E0033;
    loc_status = 0x000E0040;
    REM_NAME_$DIR_READU(&dir_uid, buf, &cont, &max, &count, &st);
    ASSERT_EQ(1, rd_calls);
    ASSERT_EQ(0, cont);
    ASSERT_EQ(0x000E0040, st);
}

/* failure on a continuation other than 1 does not locate */
TEST(failure_not_on_first_entry_is_quiet)
{
    int32_t cont = 7;
    uint16_t max = 2, count = 0;
    status_$t st = 0x55;

    reset();
    rd_status[0] = 0x000E0033;
    REM_NAME_$DIR_READU(&dir_uid, buf, &cont, &max, &count, &st);
    ASSERT_EQ(0, loc_calls);
    ASSERT_EQ(0, cont);
    ASSERT_EQ(status_$ok, st);
}

int main(void)
{
    printf("REM_NAME_$DIR_READU tests\n");
    RUN_TEST(zero_max_or_zero_continuation_is_ok_and_clears);
    RUN_TEST(fills_in_several_reads);
    RUN_TEST(replicated_root_end_accepts_partial);
    RUN_TEST(failure_after_progress_is_quiet);
    RUN_TEST(first_entry_failure_locates_and_retries);
    RUN_TEST(locate_failure_returns_locate_status);
    RUN_TEST(failure_not_on_first_entry_is_quiet);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
