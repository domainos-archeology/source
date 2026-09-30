/*
 * network/test/test_ast_get_info.c - NETWORK_$AST_GET_INFO (0x00E1026C)
 *
 * Pins: denied without the capable bit; the request layout (type 6, uid,
 * flags word, version 8, location, length 0x32, net handle = &loc_info);
 * an error status leaves everything alone; the new (0xB8) reply copies the
 * attributes and the reply's location with the caller's loc_info/node kept;
 * the old reply converts the record and rebuilds the location head; the
 * final remote bit follows node != NODE_$ME.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "network/network_internal.h"
#include "vtoc/vtoc.h"
#include "node/node.h"

uint32_t NETWORK_$ALLOWED_SERVICE;
uint32_t NODE_$ME;

static network_$getattr_rqst_t seen_rq;
static int16_t seen_len;
static void *seen_handle;
static uint16_t reply_len_to_give;
static status_$t status_to_give;
static int nold;

void network_$do_request(void *net_handle, void *cmd_buf, int16_t cmd_len,
                         uint32_t param4, uint16_t param5, int16_t param6,
                         void *resp_buf, void *resp_info, status_$t *status_ret)
{
    network_$getattr_reply_t *r = (network_$getattr_reply_t *)resp_buf;
    int i;
    (void)param4; (void)param5; (void)param6;
    seen_handle = net_handle;
    memcpy(&seen_rq, cmd_buf, sizeof(seen_rq));
    seen_len = cmd_len;
    memset(r, 0, sizeof(*r));
    r->type = 7;
    for (i = 0; i < 0x90; i++) r->u.new_.attrs[i] = (uint8_t)(i + 1);
    r->u.new_.loc.uid.high = 0xAAAA;
    r->u.new_.loc.loc_info = 0x5555;
    r->u.new_.loc.node = 0x6666;
    r->u.new_.loc.flags = 0;
    r->u.new_.loc.volume = 0x77;
    *(uint16_t *)resp_info = reply_len_to_give;
    *status_ret = status_to_give;
}

void VTOCE_$OLD_TO_NEW(void *old_vtoce, void *new_vtoce)
{
    (void)old_vtoce;
    nold++;
    memset(new_vtoce, 0x11, 8);
}

#include "../ast_get_info.c"

static file_$obj_loc_t loc;
static uint8_t attrs[0x90];
static uint16_t flags;
static status_$t st;

static void setup(void)
{
    memset(&loc, 0, sizeof(loc));
    memset(attrs, 0, sizeof(attrs));
    loc.reserved_00 = 0xABCD;
    loc.volume = 3;
    loc.block_hint = 0x12345678;
    loc.uid.high = 0x100;
    loc.uid.low = 0x200;
    loc.loc_info = 0x300;
    loc.node = 0x400;
    loc.rights_bits = 0x5A;
    loc.flags = 0x0E;
    NETWORK_$ALLOWED_SERVICE = NETWORK_SERVICE_CAPABLE;
    NODE_$ME = 0x400;
    flags = 0x0102;
    reply_len_to_give = 0xB8;
    status_to_give = 0;
    nold = 0;
    st = 1;
}

TEST(denied)
{
    setup();
    NETWORK_$ALLOWED_SERVICE = 0x00020000;
    NETWORK_$AST_GET_INFO(&loc, &flags, attrs, &st);
    ASSERT_EQ(status_$network_request_denied_by_local_node, st);
}

TEST(request_layout_and_error)
{
    setup();
    status_to_give = 0x110001;
    NETWORK_$AST_GET_INFO(&loc, &flags, attrs, &st);
    ASSERT_EQ(0x110001, st);
    ASSERT_EQ(0x32, seen_len);
    ASSERT_EQ((uintptr_t)&loc.loc_info, (uintptr_t)seen_handle);
    ASSERT_EQ(6, seen_rq.type);
    ASSERT_EQ(0x100, seen_rq.uid.high);
    ASSERT_EQ(0x01, seen_rq._0a);
    ASSERT_EQ(0x02, (uint8_t)seen_rq.big);
    ASSERT_EQ(8, seen_rq.version);
    ASSERT_EQ(0x300, seen_rq.loc.loc_info);
    /* nothing touched on error */
    ASSERT_EQ(0x0E, (uint8_t)loc.flags);
    ASSERT_EQ(0, attrs[0]);
}

TEST(new_reply)
{
    setup();
    NETWORK_$AST_GET_INFO(&loc, &flags, attrs, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, attrs[0]);
    ASSERT_EQ(0x90, attrs[0x8F]);
    ASSERT_EQ(0xAAAA, loc.uid.high);
    ASSERT_EQ(0x300, loc.loc_info);   /* caller's kept */
    ASSERT_EQ(0x400, loc.node);
    ASSERT_EQ(0x77, loc.volume);
    ASSERT_EQ(0x00, (uint8_t)loc.flags);  /* local node: bit 7 clear */
}

TEST(new_reply_no_attrs_remote_node)
{
    setup();
    flags = 0;
    NODE_$ME = 0x999;
    NETWORK_$AST_GET_INFO(&loc, &flags, attrs, &st);
    ASSERT_EQ(0, attrs[0]);
    ASSERT_EQ(0x80, (uint8_t)loc.flags);
}

TEST(old_reply)
{
    setup();
    reply_len_to_give = 0x48;
    NETWORK_$AST_GET_INFO(&loc, &flags, attrs, &st);
    ASSERT_EQ(1, nold);
    ASSERT_EQ(0x11 | 0x01, attrs[3]);
    ASSERT_EQ(0x00C1, loc.reserved_00);
    ASSERT_EQ(0, loc.volume);
    ASSERT_EQ(0, loc.block_hint);
    ASSERT_EQ(0, (uint8_t)loc.rights_bits);
    /* 0x0E|0x80 -> &0xF0|1 = 0x81 -> |0x40 = 0xC1 -> local: &0x7F = 0x41 */
    ASSERT_EQ(0x41, (uint8_t)loc.flags);
    ASSERT_EQ(0x100, loc.uid.high);
}

int main(void)
{
    printf("NETWORK_$AST_GET_INFO tests:\n");
    RUN_TEST(denied);
    RUN_TEST(request_layout_and_error);
    RUN_TEST(new_reply);
    RUN_TEST(new_reply_no_attrs_remote_node);
    RUN_TEST(old_reply);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
