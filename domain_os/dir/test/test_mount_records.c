/*
 * dir/test/test_mount_records.c - request records of the three wrappers
 * bead source-u3a8 and source-u8by re-derived.
 *
 * DIR_$ADD_MOUNT (0x00E534B8), DIR_$DROP_MOUNT (0x00E53518) and
 * DIR_$FIX_DIR (0x00E53E84) each used to carry a private request struct
 * with the wrong offsets: the version word sat at +0x0C or +0x0E depending
 * on the file, and the mount body was pushed out to +0x90/+0x98 instead of
 * +0x8E/+0x96.  These tests drive all three builders over a mock DIR_$DO_OP
 * and check the bytes that actually land in the buffer against the image.
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
/* Globals and mocks the three builders need                            */
/* ------------------------------------------------------------------ */

/* Records 21..46 of the operation table. */
MODULE_DATA_DEFINE(dir_$data_t, DIR_$DATA, 0x00E7DBF8);   /* DIR_$OP_TAB is its op_tab */

/* 0x00E245A4, the node ID DIR_$ADD_MOUNT stores at request+0x96. */
uint32_t NODE_$ME;

static uint8_t   mock_request[0x400];
static int16_t   mock_req_size;
static int16_t   mock_resp_size;
static int       mock_do_op_calls;
static status_$t mock_reply_status;
static int       mock_old_fix_calls;
static status_$t mock_old_fix_status;

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
    *received_len = 0;
}

void DIR_$OLD_FIX_DIR(uid_t *dir_uid, status_$t *status_ret)
{
    (void)dir_uid;
    mock_old_fix_calls++;
    *status_ret = mock_old_fix_status;
}

#include "../add_mount.c"
#include "../drop_mount.c"
#include "../fix_dir.c"

/* ------------------------------------------------------------------ */

static void reset_mocks(void)
{
    memset(DIR_$DATA.op_tab, 0, sizeof(DIR_$DATA.op_tab));
    memset(mock_request, 0xCC, sizeof(mock_request));
    mock_do_op_calls = 0;
    mock_req_size = 0;
    mock_resp_size = 0;
    mock_reply_status = status_$ok;
    mock_old_fix_calls = 0;
    mock_old_fix_status = 0;
    NODE_$ME = 0;
}

/*
 * Read the captured buffer back through the record itself rather than by
 * assembling bytes, so the tests say the same thing on a little-endian host
 * as they do on the m68k.
 */
static const dir_$do_op_request_t *req(void)
{
    return (const dir_$do_op_request_t *)(const void *)mock_request;
}

/* ------------------------------------------------------------------ */

/*
 * The body variant source-u3a8 added.  0x8E + DIR_$OP_TAB[24].base_size
 * (0x0C) = 0x9A, exactly the end of the record.
 */
TEST(mount_body_sits_at_0x8e_and_0x96)
{
    ASSERT_EQ(0x8E, offsetof(dir_$do_op_request_t, body.mount.mount_uid));
    ASSERT_EQ(0x96, offsetof(dir_$do_op_request_t, body.mount.node_id));
    ASSERT_EQ(0x0C, sizeof(dir_$req_mount_t));
}

/*
 * 0x00E534C4 op at +0x03, 0x00E534CE uid at +0x04, 0x00E534D6 version at
 * +0x0E, 0x00E534E0 mount uid at +0x8E, 0x00E534E8 NODE_$ME at +0x96.
 */
TEST(add_mount_writes_the_image_offsets)
{
    uid_t dir_uid   = { 0x11112222u, 0x33334444u };
    uid_t mount_uid = { 0x55556666u, 0x77778888u };
    status_$t status = 0x5A5A5A5A;

    reset_mocks();
    DIR_$OP_REC(DIR_OP_ADD_MOUNT >> 1).version   = 0xBEEF;
    DIR_$OP_REC(DIR_OP_ADD_MOUNT >> 1).base_size = 0x000C;
    NODE_$ME = 0x99990000u;
    mock_reply_status = 0x0BADF00D;

    DIR_$ADD_MOUNT(&dir_uid, &mount_uid, &status);

    ASSERT_EQ(1, mock_do_op_calls);
    ASSERT_EQ(DIR_OP_ADD_MOUNT, req()->op);
    ASSERT_EQ(0x11112222u, req()->uid.high);
    ASSERT_EQ(0x33334444u, req()->uid.low);
    ASSERT_EQ(0xBEEF, req()->version);
    ASSERT_EQ(0x55556666u, req()->body.mount.mount_uid.high);
    ASSERT_EQ(0x77778888u, req()->body.mount.mount_uid.low);
    ASSERT_EQ(0x99990000u, req()->body.mount.node_id);

    /* The body size is the table's base_size; the reply is 0x14 bytes. */
    ASSERT_EQ(0x000C, mock_req_size);
    ASSERT_EQ(0x0014, mock_resp_size);

    /* 0x00E5350C: the whole longword at reply+0x04, unconditionally. */
    ASSERT_EQ(0x0BADF00D, status);
}

/*
 * The offsets the private structs used to place these fields at, and which
 * the image never touches: the old Dir_$AddMountRequest had mount_uid at
 * +0x90 and node_id at +0x98, and Dir_$DropMountRequest put its version
 * word at +0x0C, two bytes short.  Assert structurally that the shared
 * record disagrees with all three.
 */
TEST(the_old_private_offsets_are_gone)
{
    ASSERT_EQ(1, offsetof(dir_$do_op_request_t, body.mount.mount_uid) != 0x90);
    ASSERT_EQ(1, offsetof(dir_$do_op_request_t, body.mount.node_id) != 0x98);
    ASSERT_EQ(1, offsetof(dir_$do_op_request_t, version) != 0x0C);
    /* The version really is at +0x0E and the pad word at +0x0C. */
    ASSERT_EQ(0x0C, offsetof(dir_$do_op_request_t, pad_0c));
    ASSERT_EQ(0x0E, offsetof(dir_$do_op_request_t, version));
    /* 0x8E + base_size 0x0C = 0x9A ends the mount body. */
    ASSERT_EQ(0x9A, offsetof(dir_$do_op_request_t, body.mount.node_id)
                    + sizeof(uint32_t));
}

/*
 * 0x00E53524 op, 0x00E5352E uid, 0x00E53536 version at +0x0E (the old
 * struct had my_host_id at +0x0C, two bytes short), 0x00E53540 body uid at
 * +0x8E, 0x00E5354C the DEREFERENCED lv_num at +0x96.
 */
TEST(drop_mount_writes_the_image_offsets)
{
    uid_t mount_point = { 0xAAAA0001u, 0xAAAA0002u };
    uid_t dir_uid     = { 0xBBBB0001u, 0xBBBB0002u };
    uint32_t lv_num   = 0xCAFEBABEu;
    status_$t status  = 0;

    reset_mocks();
    DIR_$OP_REC(DIR_OP_DROP_MOUNT >> 1).version   = 0x1234;
    DIR_$OP_REC(DIR_OP_DROP_MOUNT >> 1).base_size = 0x000C;
    mock_reply_status = 0x000E0016;

    DIR_$DROP_MOUNT(&mount_point, &dir_uid, &lv_num, &status);

    ASSERT_EQ(1, mock_do_op_calls);
    ASSERT_EQ(DIR_OP_DROP_MOUNT, req()->op);
    ASSERT_EQ(0xAAAA0001u, req()->uid.high);
    ASSERT_EQ(0xAAAA0002u, req()->uid.low);
    ASSERT_EQ(0x1234, req()->version);
    ASSERT_EQ(0xBBBB0001u, req()->body.mount.mount_uid.high);
    ASSERT_EQ(0xBBBB0002u, req()->body.mount.mount_uid.low);
    ASSERT_EQ(0xCAFEBABEu, req()->body.mount.node_id);
    ASSERT_EQ(0x000C, mock_req_size);
    ASSERT_EQ(0x0014, mock_resp_size);
    ASSERT_EQ(0x000E0016, status);
}

/*
 * 0x00E53E9A op at +0x03, 0x00E53EA2 uid at +0x04, 0x00E53EAA version at
 * +0x0E (the old struct had the op at +0x00).  DIR_$OP_TAB[15].base_size is
 * 0x0000 in the image, so FIX_DIR sends no body at all.
 */
TEST(fix_dir_writes_the_image_offsets)
{
    uid_t dir_uid = { 0xDEAD0001u, 0xDEAD0002u };
    status_$t status = 0;

    reset_mocks();
    DIR_$OP_REC(DIR_OP_FIX_DIR >> 1).version   = 0x0007;
    DIR_$OP_REC(DIR_OP_FIX_DIR >> 1).base_size = 0x0000;
    mock_reply_status = 0x12345678;

    DIR_$FIX_DIR(&dir_uid, &status);

    ASSERT_EQ(1, mock_do_op_calls);
    /* The op byte is at +0x03, NOT at +0x00 where Dir_$FixDirRequest had it
     * (`move.b #0x48,(-0xa5,A6)` against the request base A6-0xA8). */
    ASSERT_EQ(DIR_OP_FIX_DIR, req()->op);
    ASSERT_EQ(0x03, offsetof(dir_$do_op_request_t, op));
    ASSERT_EQ(0xDEAD0001u, req()->uid.high);
    ASSERT_EQ(0xDEAD0002u, req()->uid.low);
    ASSERT_EQ(0x0007, req()->version);
    ASSERT_EQ(0x0000, mock_req_size);
    ASSERT_EQ(0x0014, mock_resp_size);
    ASSERT_EQ(0, mock_old_fix_calls);
    ASSERT_EQ(0x12345678, status);
}

/* 0x00E53ED0 / 0x00E53ED8: the two statuses that retry the old protocol. */
TEST(fix_dir_falls_back_on_the_two_statuses)
{
    uid_t dir_uid = { 1, 2 };
    status_$t status;

    reset_mocks();
    mock_reply_status = file_$bad_reply_received_from_remote_node;
    mock_old_fix_status = 0x11111111;
    DIR_$FIX_DIR(&dir_uid, &status);
    ASSERT_EQ(1, mock_old_fix_calls);
    ASSERT_EQ(0x11111111, status);

    reset_mocks();
    mock_reply_status = status_$naming_bad_directory;
    mock_old_fix_status = 0x22222222;
    DIR_$FIX_DIR(&dir_uid, &status);
    ASSERT_EQ(1, mock_old_fix_calls);
    ASSERT_EQ(0x22222222, status);

    /* Any other status is returned as it stands. */
    reset_mocks();
    mock_reply_status = 0x000E000C;
    DIR_$FIX_DIR(&dir_uid, &status);
    ASSERT_EQ(0, mock_old_fix_calls);
    ASSERT_EQ(0x000E000C, status);
}

int main(void)
{
    printf("DIR mount / fix_dir request record tests\n");
    RUN_TEST(mount_body_sits_at_0x8e_and_0x96);
    RUN_TEST(add_mount_writes_the_image_offsets);
    RUN_TEST(the_old_private_offsets_are_gone);
    RUN_TEST(drop_mount_writes_the_image_offsets);
    RUN_TEST(fix_dir_writes_the_image_offsets);
    RUN_TEST(fix_dir_falls_back_on_the_two_statuses);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
