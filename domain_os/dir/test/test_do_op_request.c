/*
 * dir/test/test_do_op_request.c - layout tests for dir_$do_op_request_t
 *
 * The shared request record every DIR_$<op>U client wrapper builds and hands
 * to DIR_$DO_OP (0x00E4C02C).  These tests pin the three offsets bead
 * source-qc2n reported wrong across the whole family - the op byte at +0x03,
 * the protocol version word at +0x0E and the path-length word at +0x8E - and
 * then drive DIR_$DROP_LINKU (0x00E517F6) end to end over mocks so the
 * builder is checked against the record, not only the record against itself.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
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

#include "dir/dir_internal.h"

/* ------------------------------------------------------------------ */
/* Globals and mocks the builder under test needs                       */
/* ------------------------------------------------------------------ */

/* Records 21..46 of the operation table; only DROP_LINKU's is filled in. */
MODULE_DATA_DEFINE(dir_$data_t, DIR_$DATA, 0x00E7DBF8);   /* DIR_$OP_TAB is its op_tab */

static uint8_t   mock_request[0x400];
static int16_t   mock_req_size;
static int16_t   mock_resp_size;
static int       mock_do_op_calls;
static status_$t mock_reply_status;
static uid_t     mock_reply_uid;
static int       mock_old_calls;

void DIR_$DO_OP(void *request, int16_t req_size, int16_t resp_size,
                void *response_arg, uint16_t *received_len)
{
    Dir_$OpResponse *response = (Dir_$OpResponse *)response_arg;

    mock_do_op_calls++;
    mock_req_size = req_size;
    mock_resp_size = resp_size;
    memcpy(mock_request, request, sizeof(mock_request));
    memset(response, 0, sizeof(*response));
    response->status = mock_reply_status;
    response->uid = mock_reply_uid;
    *received_len = 0;
}

void DIR_$OLD_DROP_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                         uid_t *target_uid, status_$t *status_ret)
{
    (void)dir_uid; (void)name; (void)name_len; (void)target_uid;
    mock_old_calls++;
    *status_ret = 0x11111111;
}

#include "../drop_linku.c"

static void reset_mocks(void)
{
    memset(DIR_$DATA.op_tab, 0, sizeof(DIR_$DATA.op_tab));
    memset(mock_request, 0xCC, sizeof(mock_request));
    mock_do_op_calls = 0;
    mock_old_calls = 0;
    mock_req_size = 0;
    mock_resp_size = 0;
    mock_reply_status = status_$ok;
    mock_reply_uid.high = 0;
    mock_reply_uid.low = 0;
}

/* ------------------------------------------------------------------ */

TEST(record_offsets_match_the_image)
{
    ASSERT_EQ(0x03, offsetof(dir_$do_op_request_t, op));
    ASSERT_EQ(0x04, offsetof(dir_$do_op_request_t, uid));
    ASSERT_EQ(0x0E, offsetof(dir_$do_op_request_t, version));
    ASSERT_EQ(0x12, offsetof(dir_$do_op_request_t, reply_version));
    ASSERT_EQ(0x8E, offsetof(dir_$do_op_request_t, body));
}

TEST(every_body_variant_starts_at_0x8e)
{
    ASSERT_EQ(0x8E, offsetof(dir_$do_op_request_t, body.name.path_len));
    ASSERT_EQ(0x90, offsetof(dir_$do_op_request_t, body.name.name));
    ASSERT_EQ(0x90, offsetof(dir_$do_op_request_t, body.add_entry.file_uid));
    ASSERT_EQ(0x98, offsetof(dir_$do_op_request_t, body.add_entry.flags));
    ASSERT_EQ(0x9C, offsetof(dir_$do_op_request_t, body.add_entry.name));
    ASSERT_EQ(0x90, offsetof(dir_$do_op_request_t, body.cname.new_len));
    ASSERT_EQ(0x92, offsetof(dir_$do_op_request_t, body.cname.name));
    ASSERT_EQ(0x90, offsetof(dir_$do_op_request_t, body.add_link.target_len));
    ASSERT_EQ(0x92, offsetof(dir_$do_op_request_t, body.add_link.target_ptr));
    ASSERT_EQ(0x96, offsetof(dir_$do_op_request_t, body.add_link.name));
    ASSERT_EQ(0x90, offsetof(dir_$do_op_request_t, body.uid_name.target_uid));
    ASSERT_EQ(0x98, offsetof(dir_$do_op_request_t, body.uid_name.name));
    ASSERT_EQ(0x90, offsetof(dir_$do_op_request_t, body.word_name.flags));
    ASSERT_EQ(0x92, offsetof(dir_$do_op_request_t, body.word_name.name));
    ASSERT_EQ(0x8E, offsetof(dir_$do_op_request_t, body.uid_body.uid));
    ASSERT_EQ(0x8E, offsetof(dir_$do_op_request_t,
                             body.set_default_acl.acl_type_uid));
    ASSERT_EQ(0x96, offsetof(dir_$do_op_request_t,
                             body.set_default_acl.acl_uid));
    ASSERT_EQ(0x92, offsetof(dir_$do_op_request_t, body.resolve.path_len));
    ASSERT_EQ(0xAC, offsetof(dir_$do_op_request_t, body.resolve.flags));
    ASSERT_EQ(0x96, offsetof(dir_$do_op_request_t, body.set_def_prot.prot));
    ASSERT_EQ(0xC2, offsetof(dir_$do_op_request_t, body.set_def_prot.acl_uid));
    ASSERT_EQ(0x8E, offsetof(dir_$do_op_request_t, body.set_prot.prot));
    ASSERT_EQ(0xBA, offsetof(dir_$do_op_request_t, body.set_prot.acl_uid));
    ASSERT_EQ(0xC2, offsetof(dir_$do_op_request_t, body.set_prot.prot_type));
    ASSERT_EQ(0x90, offsetof(dir_$do_op_request_t, body.delete_file.flag0));
    ASSERT_EQ(0x92, offsetof(dir_$do_op_request_t, body.delete_file.name));
}

/*
 * 0x00E51846 / 0x00E5184E / 0x00E51856 / 0x00E5182C: the builder writes the
 * op byte at +0x03, the uid at +0x04, the version word at +0x0E and the name
 * length at +0x8E, with the name from +0x90.
 */
TEST(drop_linku_writes_op_at_3_version_at_e_and_len_at_8e)
{
    uid_t dir_uid = { 0xAABBCCDD, 0x11223344 };
    uid_t target;
    uint16_t name_len = 5;
    status_$t status;
    char name[] = "abcde";

    reset_mocks();
    /* DIR_OP_DROP_LINKU is 0x40, so the record index is 0x20 - 21 = 11. */
    DIR_$OP_REC(DIR_OP_DROP_LINKU >> 1).version = 0x1234;
    DIR_$OP_REC(DIR_OP_DROP_LINKU >> 1).base_size = 2;

    DIR_$DROP_LINKU(&dir_uid, name, &name_len, &target, &status);

    ASSERT_EQ(1, mock_do_op_calls);
    ASSERT_EQ(DIR_OP_DROP_LINKU, mock_request[0x03]);
    ASSERT_EQ(0xAABBCCDD, *(uint32_t *)(mock_request + 0x04));
    ASSERT_EQ(0x11223344, *(uint32_t *)(mock_request + 0x08));
    ASSERT_EQ(0x1234, *(uint16_t *)(mock_request + 0x0E));
    ASSERT_EQ(5, *(uint16_t *)(mock_request + 0x8E));
    ASSERT_EQ('a', mock_request[0x90]);
    ASSERT_EQ('e', mock_request[0x94]);
    /* req_size = base_size + name_len, resp_size = 0x1C (0x00E51864). */
    ASSERT_EQ(2 + 5, mock_req_size);
    ASSERT_EQ(0x1c, mock_resp_size);
}

/* 0x00E51818-0x00E51820: 0 and anything above 0xFF are rejected before the
 * request is built at all. */
TEST(drop_linku_rejects_out_of_range_lengths)
{
    uid_t dir_uid = { 1, 2 };
    uid_t target;
    status_$t status;
    uint16_t zero = 0, too_big = 0x100;
    char name[300];

    memset(name, 'x', sizeof(name));

    reset_mocks();
    DIR_$DROP_LINKU(&dir_uid, name, &zero, &target, &status);
    ASSERT_EQ(0, mock_do_op_calls);
    ASSERT_EQ(status_$naming_invalid_leaf, status);

    reset_mocks();
    DIR_$DROP_LINKU(&dir_uid, name, &too_big, &target, &status);
    ASSERT_EQ(0, mock_do_op_calls);
    ASSERT_EQ(status_$naming_invalid_leaf, status);
}

/* 0x00E518A4: the returned uid is the 8 bytes at reply+0x14. */
TEST(drop_linku_returns_the_uid_at_reply_0x14)
{
    uid_t dir_uid = { 1, 2 };
    uid_t target = { 0, 0 };
    uint16_t name_len = 1;
    status_$t status;
    char name[] = "a";

    reset_mocks();
    mock_reply_uid.high = 0xFEEDFACE;
    mock_reply_uid.low = 0x0BADF00D;

    DIR_$DROP_LINKU(&dir_uid, name, &name_len, &target, &status);

    ASSERT_EQ(0xFEEDFACE, target.high);
    ASSERT_EQ(0x0BADF00D, target.low);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0, mock_old_calls);
}

/* 0x00E51882 / 0x00E5188A: both statuses take the legacy fallback. */
TEST(drop_linku_falls_back_on_the_two_statuses)
{
    uid_t dir_uid = { 1, 2 };
    uid_t target = { 0, 0 };
    uint16_t name_len = 1;
    status_$t status;
    char name[] = "a";

    reset_mocks();
    mock_reply_status = file_$bad_reply_received_from_remote_node;
    DIR_$DROP_LINKU(&dir_uid, name, &name_len, &target, &status);
    ASSERT_EQ(1, mock_old_calls);
    ASSERT_EQ(0x11111111, status);

    reset_mocks();
    mock_reply_status = status_$naming_bad_directory;
    DIR_$DROP_LINKU(&dir_uid, name, &name_len, &target, &status);
    ASSERT_EQ(1, mock_old_calls);
}

int main(void)
{
    printf("dir_$do_op_request_t tests\n");
    RUN_TEST(record_offsets_match_the_image);
    RUN_TEST(every_body_variant_starts_at_0x8e);
    RUN_TEST(drop_linku_writes_op_at_3_version_at_e_and_len_at_8e);
    RUN_TEST(drop_linku_rejects_out_of_range_lengths);
    RUN_TEST(drop_linku_returns_the_uid_at_reply_0x14);
    RUN_TEST(drop_linku_falls_back_on_the_two_statuses);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
