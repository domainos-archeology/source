/*
 * rem_name/test/test_find_wrappers.c - unit tests for REM_NAME_$FIND_NETWORK
 * (0x00E4ADD6), REM_NAME_$FIND_UID (0x00E4AE84) and REM_NAME_$GET_ENTRY
 * (0x00E4AD18): the three "use the current server, else locate and retry"
 * wrappers.  LOCATE_SERVER and the three low-level lookups are mocked.
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

/* ---- mocks: one scripted status per call, in order ---- */
static int       loc_calls;
static status_$t loc_status[4];

void LOCATE_SERVER(uint32_t *node_ret, uint32_t *net_ret, status_$t *status_ret)
{
    int n = loc_calls++;
    *node_ret = 0x700 + n;
    *net_ret = 0x800 + n;
    *status_ret = loc_status[n];
}

static int       lk_calls;
static status_$t lk_status[4];
static uint32_t  lk_net_seen[4], lk_node_seen[4];
static uint32_t  lk_node_arg_seen[4];
static uid_t    *lk_uid_arg_seen[4];
static uint16_t  lk_len_seen[4];

void REM_NAME_$GET_ENTRY_BY_NODE_ID(uint32_t net, uint32_t node, uid_t *dir_uid,
                                     uint32_t target_node, void *entry_ret,
                                     status_$t *status_ret)
{
    int n = lk_calls++;
    (void)dir_uid; (void)entry_ret;
    lk_net_seen[n] = net; lk_node_seen[n] = node;
    lk_node_arg_seen[n] = target_node;
    *status_ret = lk_status[n];
}

void REM_NAME_$GET_ENTRY_BY_UID(uint32_t net, uint32_t node, uid_t *dir_uid,
                                 uid_t *target_uid, void *entry_ret,
                                 status_$t *status_ret)
{
    int n = lk_calls++;
    (void)dir_uid; (void)entry_ret;
    lk_net_seen[n] = net; lk_node_seen[n] = node;
    lk_uid_arg_seen[n] = target_uid;
    *status_ret = lk_status[n];
}

void REM_NAME_$GET_ENTRY_BY_NAME(uint32_t net, uint32_t node, uid_t *dir_uid,
                                  char *name, uint16_t name_len,
                                  void *entry_ret, status_$t *status_ret)
{
    int n = lk_calls++;
    (void)dir_uid; (void)name; (void)entry_ret;
    lk_net_seen[n] = net; lk_node_seen[n] = node;
    lk_len_seen[n] = name_len;
    *status_ret = lk_status[n];
}

#include "../find_network.c"
#include "../find_uid.c"
#include "../get_entry.c"

static uid_t dir_uid = { 1, 2 };
static uid_t tgt_uid = { 3, 4 };
static uint8_t entry[0x30];

static void reset(void)
{
    memset(&rem_name_$data, 0, sizeof(rem_name_$data));
    rem_name_$data.curr_node = 0x11;
    rem_name_$data.curr_net = 0x22;
    loc_calls = 0;
    memset(loc_status, 0, sizeof(loc_status));
    lk_calls = 0;
    memset(lk_status, 0, sizeof(lk_status));
}

/* good last_status: use the cached server, one lookup */
TEST(uses_cached_server_when_last_status_ok)
{
    uint32_t tn = 0x1234;
    uint16_t len = 5;
    status_$t st;

    reset();
    REM_NAME_$FIND_NETWORK(&dir_uid, &tn, entry, &st);
    ASSERT_EQ(0, loc_calls);
    ASSERT_EQ(1, lk_calls);
    ASSERT_EQ(0x22, lk_net_seen[0]);
    ASSERT_EQ(0x11, lk_node_seen[0]);
    ASSERT_EQ(0x1234, lk_node_arg_seen[0]);
    ASSERT_EQ(status_$ok, st);

    reset();
    REM_NAME_$FIND_UID(&dir_uid, &tgt_uid, entry, &st);
    ASSERT_EQ(0, loc_calls);
    ASSERT_EQ(1, lk_calls);
    ASSERT_EQ((uintptr_t)&tgt_uid, (uintptr_t)lk_uid_arg_seen[0]);

    reset();
    REM_NAME_$GET_ENTRY(&dir_uid, "hello", &len, entry, &st);
    ASSERT_EQ(0, loc_calls);
    ASSERT_EQ(1, lk_calls);
    ASSERT_EQ(5, lk_len_seen[0]);
}

/* bad last_status: locate first; a locate failure returns its status */
TEST(bad_last_status_locates_first)
{
    uint32_t tn = 1;
    uint16_t len = 1;
    status_$t st;

    reset();
    rem_name_$data.last_status = 0x000E0040;
    REM_NAME_$FIND_NETWORK(&dir_uid, &tn, entry, &st);
    ASSERT_EQ(1, loc_calls);
    ASSERT_EQ(1, lk_calls);
    ASSERT_EQ(0x800, lk_net_seen[0]);
    ASSERT_EQ(0x700, lk_node_seen[0]);

    reset();
    rem_name_$data.last_status = 0x000E0040;
    loc_status[0] = 0x000E0041;
    REM_NAME_$FIND_UID(&dir_uid, &tgt_uid, entry, &st);
    ASSERT_EQ(1, loc_calls);
    ASSERT_EQ(0, lk_calls);
    ASSERT_EQ(0x000E0041, st);

    reset();
    rem_name_$data.last_status = 0x000E0040;
    loc_status[0] = 0x000E0042;
    REM_NAME_$GET_ENTRY(&dir_uid, "x", &len, entry, &st);
    ASSERT_EQ(0, lk_calls);
    ASSERT_EQ(0x000E0042, st);
}

/* not-found (and, for GET_ENTRY, invalid pathname) is final */
TEST(not_found_is_final)
{
    uint32_t tn = 1;
    uint16_t len = 1;
    status_$t st;

    reset();
    lk_status[0] = status_$naming_name_not_found;
    REM_NAME_$FIND_NETWORK(&dir_uid, &tn, entry, &st);
    ASSERT_EQ(1, lk_calls);
    ASSERT_EQ(0, loc_calls);
    ASSERT_EQ(status_$naming_name_not_found, st);

    reset();
    lk_status[0] = status_$naming_name_not_found;
    REM_NAME_$FIND_UID(&dir_uid, &tgt_uid, entry, &st);
    ASSERT_EQ(1, lk_calls);
    ASSERT_EQ(0, loc_calls);

    reset();
    lk_status[0] = status_$naming_invalid_pathname;
    REM_NAME_$GET_ENTRY(&dir_uid, "x", &len, entry, &st);
    ASSERT_EQ(1, lk_calls);
    ASSERT_EQ(0, loc_calls);
    ASSERT_EQ(status_$naming_invalid_pathname, st);

    /* invalid pathname is NOT final for FIND_NETWORK */
    reset();
    lk_status[0] = status_$naming_invalid_pathname;
    REM_NAME_$FIND_NETWORK(&dir_uid, &tn, entry, &st);
    ASSERT_EQ(1, loc_calls);
    ASSERT_EQ(2, lk_calls);
}

/* other errors: locate and retry from the new server */
TEST(other_error_relocates_and_retries)
{
    uint32_t tn = 1;
    uint16_t len = 1;
    status_$t st;

    reset();
    lk_status[0] = 0x000E0033;
    REM_NAME_$FIND_NETWORK(&dir_uid, &tn, entry, &st);
    ASSERT_EQ(1, loc_calls);
    ASSERT_EQ(2, lk_calls);
    ASSERT_EQ(0x800, lk_net_seen[1]);
    ASSERT_EQ(0x700, lk_node_seen[1]);
    ASSERT_EQ(status_$ok, st);

    /* a failing locate leaves ITS status and does not retry */
    reset();
    lk_status[0] = 0x000E0033;
    loc_status[0] = 0x000E0050;
    REM_NAME_$GET_ENTRY(&dir_uid, "x", &len, entry, &st);
    ASSERT_EQ(1, lk_calls);
    ASSERT_EQ(0x000E0050, st);
}

/* the D2 flag: FIND_NETWORK / GET_ENTRY do not relocate twice, FIND_UID does */
TEST(already_located_flag)
{
    uint32_t tn = 1;
    uint16_t len = 1;
    status_$t st;

    reset();
    rem_name_$data.last_status = 0x000E0040;
    lk_status[0] = 0x000E0033;
    REM_NAME_$FIND_NETWORK(&dir_uid, &tn, entry, &st);
    ASSERT_EQ(1, loc_calls);
    ASSERT_EQ(1, lk_calls);
    ASSERT_EQ(0x000E0033, st);

    reset();
    rem_name_$data.last_status = 0x000E0040;
    lk_status[0] = 0x000E0033;
    REM_NAME_$GET_ENTRY(&dir_uid, "x", &len, entry, &st);
    ASSERT_EQ(1, loc_calls);
    ASSERT_EQ(1, lk_calls);

    reset();
    rem_name_$data.last_status = 0x000E0040;
    lk_status[0] = 0x000E0033;
    REM_NAME_$FIND_UID(&dir_uid, &tgt_uid, entry, &st);
    ASSERT_EQ(2, loc_calls);            /* 0x00E4AEF2: no flag */
    ASSERT_EQ(2, lk_calls);
    ASSERT_EQ(0x801, lk_net_seen[1]);
    ASSERT_EQ(status_$ok, st);
}

int main(void)
{
    printf("REM_NAME_$FIND_NETWORK / FIND_UID / GET_ENTRY tests\n");
    RUN_TEST(uses_cached_server_when_last_status_ok);
    RUN_TEST(bad_last_status_locates_first);
    RUN_TEST(not_found_is_final);
    RUN_TEST(other_error_relocates_and_retries);
    RUN_TEST(already_located_flag);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
