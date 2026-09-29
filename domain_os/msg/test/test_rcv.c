/*
 * msg/test/test_rcv.c - Unit tests for the MSG receive path
 * (MSG_$$RCV_INTERNAL 0x00E59548, MSG_$RCVI 0x00E596B2, MSG_$RCV 0x00E594F4).
 *
 * Compiles the real msg/rcv_internal.c and msg/rcv.c and supplies the MSG
 * globals plus a scripted APP_$RECEIVE, so the 18-argument shape recovered
 * for bead source-xtsx is exercised end to end: the template/data split, the
 * clamping, the msg_$hw_addr_t record and the internet-address special case.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_failed = 0;
static int tests_run = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %s... ", #name);                                        \
    tests_run++;                                                              \
    test_##name();                                                            \
    printf("done\n");                                                         \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    unsigned long long _e = (unsigned long long)(expected);                   \
    unsigned long long _a = (unsigned long long)(actual);                     \
    if (_e != _a) {                                                           \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",      \
               _e, _a, __LINE__);                                             \
        tests_failed++;                                                       \
        return;                                                               \
    }                                                                         \
} while (0)

#include "msg/msg_internal.h"
#include "app/app.h"

/* Globals the code under test links against. */
MODULE_DATA_DEFINE(msg_$unwired_data_t, MSG_$UNWIRED_DATA, 0x00E80D84);
uint16_t PROC1_$AS_ID;

/*
 * The netbuf page.  MSG_$$RCV_INTERNAL rounds rec.data down to 1KB to find
 * it, so the reply record and the template must live inside one page.  The
 * arena base is one page below the array so no VA handed out is zero.
 */
#define ARENA_PAGES 4
static uint8_t arena[ARENA_PAGES * 1024] __attribute__((aligned(1024)));
#define PAGE_VA     1024u
#define REPLY_OFF   0x040u
#define TEMPLATE_OFF 0x080u

static uint8_t *page(void)     { return arena + PAGE_VA; }
static msg_$reply_hdr_t *reply_hdr(void)
{
    return (msg_$reply_hdr_t *)(page() + REPLY_OFF);
}
static uint8_t *template_bytes(void) { return page() + TEMPLATE_OFF; }

/* Scripted APP_$RECEIVE. */
static int app_receive_calls;
static uint16_t app_receive_sock;
static status_$t app_receive_status;
static app_$receive_rec_t app_receive_rec;

void APP_$RECEIVE(uint16_t sock_num, void *result, status_$t *status_ret)
{
    app_receive_calls++;
    app_receive_sock = sock_num;
    *(app_$receive_rec_t *)result = app_receive_rec;
    *status_ret = app_receive_status;
}

static int data_copy_calls;
static const char *data_copy_src;
static char *data_copy_dst;
static uint32_t data_copy_len;

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    data_copy_calls++;
    data_copy_src = (const char *)src;
    data_copy_dst = (char *)dst;
    data_copy_len = len;
    memcpy(dst, src, len);
}

static int dat_copy_calls;
static int16_t dat_copy_len;
static char *dat_copy_dst;

void PKT_$DAT_COPY(uint32_t *buffers, int16_t len, char *dest_va)
{
    (void)buffers;
    dat_copy_calls++;
    dat_copy_len = len;
    dat_copy_dst = dest_va;
}

static int dump_data_calls;
static int16_t dump_data_len;

void PKT_$DUMP_DATA(uint32_t *buffers, int16_t len)
{
    (void)buffers;
    dump_data_calls++;
    dump_data_len = len;
}

static int rtn_hdr_calls;
static uint32_t rtn_hdr_va;

void NETBUF_$RTN_HDR(uint32_t *va_ptr)
{
    rtn_hdr_calls++;
    rtn_hdr_va = *va_ptr;
}

#include "../rcv_internal.c"
#include "../rcv.c"

/* --------------------------------------------------------------------- */

#define TEST_SOCK 0x21
#define TEST_ASID 5

static char template_out[64];
static char data_out[64];

static void setup(void)
{
    memset(&MSG_$UNWIRED_DATA, 0, sizeof(MSG_$UNWIRED_DATA));
    memset(arena, 0, sizeof(arena));
    memset(template_out, 0, sizeof(template_out));
    memset(data_out, 0, sizeof(data_out));
    ARCH_HOST_VA_BASE = (uintptr_t)arena;

    PROC1_$AS_ID = TEST_ASID;
    /* MSG_$UNWIRED_DATA.ownership[n] is MSG_$UNWIRED_DATA.ownership[n - 1] */
    MSG_$UNWIRED_DATA.ownership[TEST_SOCK][(0x3F - TEST_ASID) >> 3] |= 1u << (TEST_ASID & 7);

    reply_hdr()->prefix.template_len = 8;
    reply_hdr()->prefix.data_len = 0x20;
    reply_hdr()->prefix.request_id = 0x00A5;
    reply_hdr()->dest_node = 0x11112222;
    reply_hdr()->dest_sock = 0x3333;
    reply_hdr()->src_node = 0x44445555;
    reply_hdr()->src_sock = 0x6666;
    reply_hdr()->proto_family = 0x07;
    reply_hdr()->proto_type = 0x01;
    reply_hdr()->proto_subtype = 0x02;

    memcpy(template_bytes(), "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345", 32);

    memset(&app_receive_rec, 0, sizeof(app_receive_rec));
    app_receive_rec.reply = ARCH_PTR_TO_VA(reply_hdr());
    app_receive_rec.data = ARCH_PTR_TO_VA(template_bytes());
    app_receive_rec.data_pages[0] = 0x9000;
    app_receive_rec.hdr_f06 = 0xAAAABBBB;
    app_receive_rec.hdr_f12 = 0xCCCCDDDD;
    /* the queue depth lives in bits 7..14 of flags_lo */
    app_receive_rec.flags_lo = (uint16_t)(3u << 7);
    app_receive_status = status_$ok;

    /* the netbuf event words the page carries */
    *(uint16_t *)(page() + NETBUF_HDR_EC_PARAM1) = 0x0077;
    *(uint16_t *)(page() + NETBUF_HDR_EC_PARAM2) = 0x0088;

    app_receive_calls = data_copy_calls = dat_copy_calls = 0;
    dump_data_calls = rtn_hdr_calls = 0;
}

static status_$t call_internal(uint16_t template_max, uint16_t data_max,
                              msg_$hw_addr_t *hw_addr,
                              uint16_t *template_len_ret,
                              uint16_t *data_len_ret,
                              uint16_t *ec1, uint16_t *ec2)
{
    uint32_t dest_net = 0, dest_node = 0, src_net = 0, src_node = 0;
    uint16_t dest_sock = 0, src_sock = 0, msg_type = 0;
    status_$t status = -1;

    MSG_$$RCV_INTERNAL(TEST_SOCK,
                       &dest_net, &dest_node, &dest_sock,
                       &src_net, &src_node, &src_sock,
                       hw_addr, &msg_type,
                       template_out, template_max, template_len_ret,
                       data_out, data_max, data_len_ret,
                       ec1, ec2, &status);
    return status;
}

/* Every out-parameter comes from the reply header or the receive record. */
TEST(internal_reports_the_addresses)
{
    uint32_t dest_net = 0, dest_node = 0, src_net = 0, src_node = 0;
    uint16_t dest_sock = 0, src_sock = 0, msg_type = 0;
    uint16_t tlen = 0, dlen = 0, ec1 = 0, ec2 = 0;
    msg_$hw_addr_t hw;
    status_$t status = -1;

    setup();
    memset(&hw, 0, sizeof(hw));

    MSG_$$RCV_INTERNAL(TEST_SOCK,
                       &dest_net, &dest_node, &dest_sock,
                       &src_net, &src_node, &src_sock,
                       &hw, &msg_type,
                       template_out, 32, &tlen,
                       data_out, 32, &dlen,
                       &ec1, &ec2, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, app_receive_calls);
    ASSERT_EQ(TEST_SOCK, app_receive_sock);
    ASSERT_EQ(0xAAAABBBBu, dest_net);
    ASSERT_EQ(0xCCCCDDDDu, src_net);
    ASSERT_EQ(0x11112222u, dest_node);
    ASSERT_EQ(0x3333, dest_sock);
    ASSERT_EQ(0x44445555u, src_node);
    ASSERT_EQ(0x6666, src_sock);
    ASSERT_EQ(0x00A5, msg_type);
    ASSERT_EQ(0x0077, ec1);
    ASSERT_EQ(0x0088, ec2);
}

/* The msg_$hw_addr_t record: three bytes, the queue depth, and 0/0xFFFF. */
TEST(internal_builds_the_hw_addr_record)
{
    uint16_t tlen = 0, dlen = 0, ec1 = 0, ec2 = 0;
    msg_$hw_addr_t hw;

    setup();
    memset(&hw, 0xEE, sizeof(hw));

    call_internal(32, 32, &hw, &tlen, &dlen, &ec1, &ec2);

    ASSERT_EQ(0x07, hw.proto_family);
    ASSERT_EQ(3, hw.flags);             /* (3 << 7) masked and shifted back */
    ASSERT_EQ(0x01, hw.proto_type);
    ASSERT_EQ(0x02, hw.proto_subtype);
    ASSERT_EQ(0x0000, hw.reserved2);
    ASSERT_EQ(0xFFFF, hw.reserved3);
    /* reserved1 is never written - the 0xEE fill must survive */
    ASSERT_EQ(0xEEEE, hw.reserved1);
}

/* The template is copied from rec.data and clamped to template_max. */
TEST(internal_clamps_the_template)
{
    uint16_t tlen = 0, dlen = 0, ec1 = 0, ec2 = 0;
    msg_$hw_addr_t hw;

    setup();
    memset(&hw, 0, sizeof(hw));

    call_internal(32, 32, &hw, &tlen, &dlen, &ec1, &ec2);
    ASSERT_EQ(8, tlen);
    ASSERT_EQ(8, data_copy_len);
    ASSERT_EQ('A', template_out[0]);
    ASSERT_EQ('H', template_out[7]);
    ASSERT_EQ(0, template_out[8]);

    setup();
    call_internal(4, 32, &hw, &tlen, &dlen, &ec1, &ec2);
    ASSERT_EQ(4, tlen);
    ASSERT_EQ(4, data_copy_len);
}

/* The payload is clamped to data_max, but DUMP_DATA gets the full length. */
TEST(internal_clamps_the_payload)
{
    uint16_t tlen = 0, dlen = 0, ec1 = 0, ec2 = 0;
    msg_$hw_addr_t hw;

    setup();
    memset(&hw, 0, sizeof(hw));

    call_internal(32, 0x10, &hw, &tlen, &dlen, &ec1, &ec2);

    ASSERT_EQ(0x10, dlen);
    ASSERT_EQ(1, dat_copy_calls);
    ASSERT_EQ(0x10, dat_copy_len);
    ASSERT_EQ(1, dump_data_calls);
    ASSERT_EQ(0x20, dump_data_len);     /* the FULL reply->data_len */
}

/* 0xE5965C: no payload pages at all still returns the header. */
TEST(internal_with_no_payload_pages)
{
    uint16_t tlen = 0, dlen = 0xFFFF, ec1 = 0, ec2 = 0;
    msg_$hw_addr_t hw;

    setup();
    memset(&hw, 0, sizeof(hw));
    app_receive_rec.data_pages[0] = 0;

    call_internal(32, 32, &hw, &tlen, &dlen, &ec1, &ec2);

    ASSERT_EQ(0, dlen);
    ASSERT_EQ(0, dat_copy_calls);
    ASSERT_EQ(0, dump_data_calls);
    ASSERT_EQ(1, rtn_hdr_calls);
    ASSERT_EQ(PAGE_VA, rtn_hdr_va);
}

/*
 * 0xE595F6: proto_type 2 with subtype 0x29 peels a 16-byte address off the
 * front of the template and shortens the reply's template length in place.
 */
TEST(internal_peels_the_internet_address)
{
    uint16_t tlen = 0, dlen = 0, ec1 = 0, ec2 = 0;
    msg_$hw_addr_t hw;

    setup();
    memset(&hw, 0, sizeof(hw));
    reply_hdr()->proto_type = MSG_PROTO_TYPE_INET;
    reply_hdr()->proto_subtype = MSG_PROTO_SUBTYPE_INET;
    reply_hdr()->prefix.template_len = 0x18;      /* 16 address bytes + 8 template */

    call_internal(32, 32, &hw, &tlen, &dlen, &ec1, &ec2);

    ASSERT_EQ('A', hw.inet_addr[0]);
    ASSERT_EQ('P', hw.inet_addr[15]);
    ASSERT_EQ(8, tlen);                    /* 0x18 - 0x10 */
    ASSERT_EQ(8, reply_hdr()->prefix.template_len);   /* written back in place */
    ASSERT_EQ('Q', template_out[0]);       /* the copy starts past the addr */
}

/* An APP_$RECEIVE failure leaves every out-parameter alone. */
TEST(internal_propagates_a_receive_failure)
{
    uint16_t tlen = 0x1234, dlen = 0x5678, ec1 = 0, ec2 = 0;
    msg_$hw_addr_t hw;
    status_$t status;

    setup();
    memset(&hw, 0, sizeof(hw));
    app_receive_status = 0x00110026;

    status = call_internal(32, 32, &hw, &tlen, &dlen, &ec1, &ec2);

    ASSERT_EQ(0x00110026, status);
    ASSERT_EQ(0x1234, tlen);
    ASSERT_EQ(0x5678, dlen);
    ASSERT_EQ(0, rtn_hdr_calls);
}

/* --------------------------------------------------------------------- */

TEST(rcvi_rejects_an_out_of_range_socket)
{
    msg_$socket_t sock;
    uint32_t dn = 0, dnode = 0, sn = 0, snode = 0;
    uint16_t dsock = 0, ssock = 0, mtype = 0, tmax = 32, tlen = 0;
    uint16_t dmax = 32, dlen = 0;
    msg_$hw_addr_t hw;
    status_$t status = -1;

    setup();
    memset(&hw, 0, sizeof(hw));

    sock = 0;
    MSG_$RCVI(&sock, &dn, &dnode, &dsock, &sn, &snode, &ssock, &hw, &mtype,
              template_out, &tmax, &tlen, data_out, &dmax, &dlen, &status);
    ASSERT_EQ(status_$msg_socket_out_of_range, status);
    ASSERT_EQ(0, app_receive_calls);

    sock = MSG_MAX_SOCKET + 1;
    MSG_$RCVI(&sock, &dn, &dnode, &dsock, &sn, &snode, &ssock, &hw, &mtype,
              template_out, &tmax, &tlen, data_out, &dmax, &dlen, &status);
    ASSERT_EQ(status_$msg_socket_out_of_range, status);

    /* 0xE0 is the last accepted number */
    sock = MSG_MAX_SOCKET;
    MSG_$RCVI(&sock, &dn, &dnode, &dsock, &sn, &snode, &ssock, &hw, &mtype,
              template_out, &tmax, &tlen, data_out, &dmax, &dlen, &status);
    ASSERT_EQ(status_$msg_no_owner, status);   /* got past the range check */
}

TEST(rcvi_rejects_a_socket_the_caller_does_not_own)
{
    msg_$socket_t sock = TEST_SOCK;
    uint32_t dn = 0, dnode = 0, sn = 0, snode = 0;
    uint16_t dsock = 0, ssock = 0, mtype = 0, tmax = 32, tlen = 0;
    uint16_t dmax = 32, dlen = 0;
    msg_$hw_addr_t hw;
    status_$t status = -1;

    setup();
    memset(&hw, 0, sizeof(hw));
    PROC1_$AS_ID = TEST_ASID + 1;       /* a different address space */

    MSG_$RCVI(&sock, &dn, &dnode, &dsock, &sn, &snode, &ssock, &hw, &mtype,
              template_out, &tmax, &tlen, data_out, &dmax, &dlen, &status);

    ASSERT_EQ(status_$msg_no_owner, status);
    ASSERT_EQ(0, app_receive_calls);
}

/* MSG_$RCV hands back only the record's first word. */
TEST(rcv_returns_proto_family_only)
{
    msg_$socket_t sock = TEST_SOCK;
    uint32_t src_node = 0;
    uint16_t src_sock = 0, proto_family = 0, msg_type = 0;
    uint16_t tmax = 32, tlen = 0, dmax = 32, dlen = 0;
    status_$t status = -1;

    setup();

    MSG_$RCV(&sock, &src_node, &src_sock, &proto_family, &msg_type,
             template_out, &tmax, &tlen, data_out, &dmax, &dlen, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0x07, proto_family);
    ASSERT_EQ(0x44445555u, src_node);
    ASSERT_EQ(0x6666, src_sock);
    ASSERT_EQ(0x00A5, msg_type);
    ASSERT_EQ(8, tlen);
    ASSERT_EQ(0x20, dlen);
}

int main(void)
{
    printf("=== MSG receive-path tests ===\n");

    RUN_TEST(internal_reports_the_addresses);
    RUN_TEST(internal_builds_the_hw_addr_record);
    RUN_TEST(internal_clamps_the_template);
    RUN_TEST(internal_clamps_the_payload);
    RUN_TEST(internal_with_no_payload_pages);
    RUN_TEST(internal_peels_the_internet_address);
    RUN_TEST(internal_propagates_a_receive_failure);
    RUN_TEST(rcvi_rejects_an_out_of_range_socket);
    RUN_TEST(rcvi_rejects_a_socket_the_caller_does_not_own);
    RUN_TEST(rcv_returns_proto_family_only);

    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
