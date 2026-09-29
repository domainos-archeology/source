/*
 * asknode/test/test_who_notopo.c - unit tests for ASKNODE_$WHO_NOTOPO
 *                                  (0x00E65FDC)
 *
 * These tests #include asknode/who_notopo.c and drive the real function
 * through mocked callees, so what is exercised is the emitted translation and
 * not a re-implementation.
 *
 * What they pin down (bead source-gufm):
 *
 *   - the WHO request record: NODE_$ME at +0x08 (0x00E66116), the port's
 *     network at +0x0C (0x00E66134) and 0x5B8D8 at +0x14 (0x00E66138) - the
 *     tree used to write them at +0x04/+0x08/+0x0C;
 *   - the destination record handed to RIP_$FIND_NEXTHOP: the node id goes
 *     into the LONGWORD at +0x06 with "andi.l #0xFFF00000 / ori.l #1"
 *     (0x00E6605A), not into the word pair at +0x04;
 *   - 0x00E660A0 tests RIP_$FIND_NEXTHOP's RETURN value (the metric, zero
 *     for a direct route), not the port index it also wrote;
 *   - 0x00E66158 clears a WORD at pkt_info+0x08, not a longword.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ==========================================================================
 * Test infrastructure
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;
static void reset_mocks(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %s... ", #name);                                        \
    current_failed = 0;                                                       \
    reset_mocks();                                                            \
    test_##name();                                                            \
    if (current_failed == 0) { tests_passed++; printf("PASSED\n"); }          \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {     \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",      \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                       \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#define ASSERT_TRUE(cond) do {                                                \
    if (!(cond)) {                                                            \
        printf("FAILED\n    Assertion failed at line %d: %s\n",               \
               __LINE__, #cond);                                              \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

/* ==========================================================================
 * Headers the translation unit under test needs
 * ========================================================================== */

#include "asknode/asknode_internal.h"

/* ==========================================================================
 * Kernel data
 * ========================================================================== */

uint32_t NODE_$ME;
uint32_t ROUTE_$PORT;
uint32_t TIME_$CLOCKH;
uint16_t PROC1_$AS_ID;
uint32_t ASKNODE_$EMPTY_DATA;
MODULE_DATA_DEFINE(asknode_$data_t, ASKNODE_$DATA, 0x00E82408);
MODULE_DATA_DEFINE(sock_$data_t, SOCK_$DATA, 0x00E27510);
#include "fim/fim.h"
MODULE_DATA_DEFINE(fim_$wired_data_t, FIM_$WIRED_DATA, 0x00E21FE6);
name_$data_t NAME_$DATA;            /* NAME_$ROOT_UID lives in here */

route_$port_t  route_ports[8];
MODULE_DATA_DEFINE(route_$wired_data_t, ROUTE_$WIRED_DATA, 0x00E26EE4);

/* ==========================================================================
 * Mock state
 * ========================================================================== */

static ec_$eventcount_t mock_socket_ec;

static int8_t    mock_allocate_result;      /* < 0 = success */
static uint16_t  mock_allocate_sock;
static uint32_t  mock_allocate_flags;
static int       mock_close_calls;
static uint16_t  mock_close_sock;

static int16_t   mock_nexthop_metric;
static int16_t   mock_nexthop_port;
static status_$t mock_nexthop_status;
static int       mock_nexthop_calls;
static rip_$dest_addr_t mock_nexthop_dest;  /* the record it was handed */

static int       mock_send_calls;
static status_$t mock_send_status;
static uint16_t  mock_send_timeout;
static uint8_t   mock_send_template[0x40];  /* a copy of the request record */
static uint16_t  mock_send_template_len;
static uint8_t   mock_send_pkt_info[0x20];
static uint32_t  mock_send_routing_key;
static uint32_t  mock_send_dest_node;
static uint16_t  mock_send_src_sock;

static int16_t   mock_wait_result;          /* what EC_$WAIT returns */
static int       mock_wait_calls;

static void reset_mocks(void)
{
    memset(&mock_socket_ec, 0, sizeof(mock_socket_ec));
    memset(&ASKNODE_$DATA, 0, sizeof(ASKNODE_$DATA));
    memset(&SOCK_$DATA, 0, sizeof(SOCK_$DATA));
    memset(FIM_$WIRED_DATA.quit_ec, 0, sizeof(FIM_$WIRED_DATA.quit_ec));
    memset(FIM_$WIRED_DATA.quit_value, 0, sizeof(FIM_$WIRED_DATA.quit_value));
    memset(route_ports, 0, sizeof(route_ports));
    memset(&NAME_$DATA, 0, sizeof(NAME_$DATA));
    {
        int i;
        for (i = 0; i < 8; i++) {
            ROUTE_$WIRED_DATA.portp[i] = &route_ports[i];
        }
    }

    NODE_$ME = 0x00012345;
    ROUTE_$PORT = 0x00000011;
    TIME_$CLOCKH = 0x1000;
    PROC1_$AS_ID = 1;
    ASKNODE_$EMPTY_DATA = 0;

    /* socket 6's entry */
    SOCK_$DATA.socket_ptr[6] = (sock_$sock_t *)&mock_socket_ec;

    mock_allocate_result = (int8_t)0xFF;     /* success */
    mock_allocate_sock = 6;
    mock_allocate_flags = 0;
    mock_close_calls = 0;
    mock_close_sock = 0;

    mock_nexthop_metric = 0;
    mock_nexthop_port = 0;
    mock_nexthop_status = 0;
    mock_nexthop_calls = 0;
    memset(&mock_nexthop_dest, 0, sizeof(mock_nexthop_dest));

    mock_send_calls = 0;
    mock_send_status = 0;
    mock_send_timeout = 0;
    memset(mock_send_template, 0, sizeof(mock_send_template));
    mock_send_template_len = 0;
    memset(mock_send_pkt_info, 0, sizeof(mock_send_pkt_info));
    mock_send_routing_key = 0;
    mock_send_dest_node = 0;
    mock_send_src_sock = 0;

    mock_wait_result = 1;                    /* timeout: leave the loop */
    mock_wait_calls = 0;
}

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

uint32_t DIR_$FIND_NET(uid_t *dir_uid, uint32_t *index)
{ (void)dir_uid; (void)index; return 0x00000077; }

int16_t RIP_$FIND_NEXTHOP(void *addr_info, boolean flags, int16_t *port_ret,
                          void *nexthop_ret, status_$t *status_ret)
{
    (void)flags;
    mock_nexthop_calls++;
    memcpy(&mock_nexthop_dest, addr_info, sizeof(mock_nexthop_dest));
    memset(nexthop_ret, 0, sizeof(rip_$nexthop_t));
    *port_ret = mock_nexthop_port;
    *status_ret = mock_nexthop_status;
    return mock_nexthop_metric;
}

int8_t SOCK_$ALLOCATE(uint16_t *sock_ret, uint32_t proto_bufpages,
                      uint32_t max_queue)
{
    (void)max_queue;
    mock_allocate_flags = proto_bufpages;
    *sock_ret = mock_allocate_sock;
    return mock_allocate_result;
}

void SOCK_$CLOSE(uint16_t sock_num)
{ mock_close_calls++; mock_close_sock = sock_num; }

int32_t EC_$READ(ec_$eventcount_t *ec) { return (int32_t)ec->value; }

int16_t EC_$WAIT(ec_$wait_ecs_t ecs, ec_$wait_vals_t vals)
{ (void)ecs; (void)vals; mock_wait_calls++; return mock_wait_result; }

int16_t PKT_$NEXT_ID(void) { return 0x1234; }

void PKT_$SEND_INTERNET(uint32_t routing_key, uint32_t dest_node,
                        uint16_t dest_sock, int32_t src_node_or,
                        uint32_t src_node, uint16_t src_sock,
                        void *pkt_info, uint16_t request_id,
                        void *template, uint16_t template_len,
                        void *data, int16_t data_len,
                        uint16_t *retry_hint, uint16_t *timeout_out,
                        status_$t *status_ret)
{
    (void)dest_sock; (void)src_node_or; (void)src_node; (void)request_id;
    (void)data; (void)data_len;
    mock_send_calls++;
    mock_send_routing_key = routing_key;
    mock_send_dest_node = dest_node;
    mock_send_src_sock = src_sock;
    mock_send_template_len = template_len;
    if (template_len > sizeof(mock_send_template)) {
        template_len = sizeof(mock_send_template);
    }
    memcpy(mock_send_template, template, template_len);
    memcpy(mock_send_pkt_info, pkt_info, sizeof(mock_send_pkt_info));
    *retry_hint = 0;
    *timeout_out = mock_send_timeout;
    *status_ret = mock_send_status;
}

void APP_$RECEIVE(uint16_t sock_num, void *result, status_$t *status_ret)
{
    (void)sock_num;
    memset(result, 0, sizeof(app_$receive_rec_t));
    *status_ret = 0x00110006;       /* nothing to receive */
}

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{ memcpy(dst, src, len); }

void NETBUF_$RTN_HDR(uint32_t *va_ptr) { (void)va_ptr; }

void PKT_$DUMP_DATA(uint32_t *buffers, int16_t len)
{ (void)buffers; (void)len; }

/* ==========================================================================
 * The translation unit under test
 * ========================================================================== */

#include "../who_notopo.c"

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static int32_t   node_list[8];
static uint16_t  out_count;
static status_$t out_status;

static void run(int32_t node, int32_t port, int16_t max_count)
{
    int32_t  n = node;
    int32_t  p = port;
    int16_t  m = max_count;

    memset(node_list, 0, sizeof(node_list));
    out_count = 0xFFFF;
    out_status = 0xDEADBEEF;
    ASKNODE_$WHO_NOTOPO(&n, &p, node_list, &m, &out_count, &out_status);
}

static uint16_t tpl_w(unsigned off)
{
    uint16_t v;
    memcpy(&v, mock_send_template + off, sizeof(v));
    return v;
}

static uint32_t tpl_l(unsigned off)
{
    uint32_t v;
    memcpy(&v, mock_send_template + off, sizeof(v));
    return v;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * 0x00E6610E - 0x00E66138.  The four stores land at +0x00 (one longword
 * covering the version and the request type), +0x08, +0x0C and +0x14.  The
 * send is told the record is 0x18 bytes.
 */
TEST(request_record_field_offsets)
{
    route_ports[3].network = 0xCAFEBABEu;
    route_ports[3].active  = 1;
    mock_nexthop_port = 3;
    mock_nexthop_metric = 0;

    run(0, 0x55, 4);

    ASSERT_EQ(1, mock_send_calls);
    ASSERT_EQ(0x18, mock_send_template_len);
    ASSERT_EQ(3, tpl_w(0x00));                  /* version */
    ASSERT_EQ(ASKNODE_REQ_TIME_SYNC, tpl_w(0x02));   /* 0x45 */
    ASSERT_EQ(0x00012345u, tpl_l(0x08));        /* NODE_$ME */
    ASSERT_EQ(0xCAFEBABEu, tpl_l(0x0C));        /* the port's network */
    ASSERT_EQ(0x5B8D8u, tpl_l(0x14));           /* the magic constant */
}

/*
 * The record is the one PKT_$SEND_INTERNET is handed, so the fields the
 * original never writes must not be written either: with the frame poisoned
 * through a previous call, +0x04, +0x10 and +0x12 carry no value this
 * function put there.  What the test can state is that the four fields above
 * are the ONLY ones the emitted code assigns - checked by comparing two runs
 * whose only difference is a field the request does not carry.
 */
TEST(request_record_carries_only_the_four_fields)
{
    uint8_t first[0x18];

    route_ports[0].network = 0x11111111u;
    mock_nexthop_port = 0;
    mock_nexthop_metric = 0;
    run(0, 0x55, 4);
    memcpy(first, mock_send_template, sizeof(first));

    /* a different max_count changes nothing in the request */
    reset_mocks();
    route_ports[0].network = 0x11111111u;
    mock_nexthop_port = 0;
    mock_nexthop_metric = 0;
    run(0, 0x55, 7);
    ASSERT_EQ(0, memcmp(first, mock_send_template, sizeof(first)));
}

/*
 * 0x00E66056 - 0x00E66062.  The routing key is the record's network and the
 * node id goes into the LONGWORD at +0x06 (rip_$dest_addr_t.host_lo), masked
 * to its top 12 bits and OR'd with 1.  The tree used to write 1 into the
 * word pair at +0x04 instead.
 */
TEST(rip_destination_record_puts_node_1_in_host_lo)
{
    mock_nexthop_metric = 0;
    run(0, 0x00ABCDEF, 4);

    ASSERT_EQ(1, mock_nexthop_calls);
    ASSERT_EQ(0x00ABCDEFu, mock_nexthop_dest.network);
    ASSERT_EQ(1u, mock_nexthop_dest.host_lo & 0x000FFFFFu);
}

/*
 * 0x00E6609C - 0x00E660BC.  For a REMOTE query the local node is listed only
 * when RIP_$FIND_NEXTHOP reported a direct route, i.e. returned zero.  The
 * port index is deliberately non-zero in both halves so that the old test
 * (on the port index) would have given the opposite answer each time.
 */
TEST(local_node_listed_on_a_direct_route_not_a_zero_port)
{
    /* direct route (metric 0) but port index 5 */
    mock_nexthop_metric = 0;
    mock_nexthop_port = 5;
    route_ports[5].network = 0x22222222u;
    run(0x00099999, 0x55, 4);
    ASSERT_EQ(1, out_count);
    ASSERT_EQ((int32_t)0x00012345, node_list[0]);
    /* the destination the send used is then zero, not the queried node */
    ASSERT_EQ(0u, mock_send_dest_node);

    /* indirect route (metric 3) with port index 0 */
    reset_mocks();
    mock_nexthop_metric = 3;
    mock_nexthop_port = 0;
    run(0x00099999, 0x55, 4);
    ASSERT_EQ(0, out_count);
    ASSERT_EQ(0, node_list[0]);
    ASSERT_EQ(0x00099999u, mock_send_dest_node);
}

/* A local query always lists the local node whatever the metric is. */
TEST(local_query_lists_the_local_node_whatever_the_metric)
{
    mock_nexthop_metric = 9;
    mock_nexthop_port = 5;
    run(0, 0x55, 4);
    ASSERT_EQ(1, out_count);
    ASSERT_EQ((int32_t)0x00012345, node_list[0]);
}

/*
 * 0x00E66148 - 0x00E6615C: the 30-byte ASKNODE_$DATA.pkt_info copy, then a WORD
 * clear at +0x08 and the packet length at +0x00.  Byte 0x0A must survive -
 * clearing a longword there is what the tree used to do.
 */
TEST(pkt_info_clears_only_the_word_at_offset_8)
{
    uint16_t v;

    memset(&ASKNODE_$DATA.pkt_info, 0xA5, sizeof(ASKNODE_$DATA.pkt_info));
    mock_nexthop_metric = 0;
    run(0, 0x55, 4);

    ASSERT_EQ(1, mock_send_calls);
    memcpy(&v, mock_send_pkt_info + 0x00, 2);
    ASSERT_EQ(0x90, v);                         /* packet length */
    memcpy(&v, mock_send_pkt_info + 0x08, 2);
    ASSERT_EQ(0x0000, v);                       /* the cleared word */
    memcpy(&v, mock_send_pkt_info + 0x0A, 2);
    ASSERT_EQ(0xA5A5, v);                       /* still the copied byte */
    memcpy(&v, mock_send_pkt_info + 0x06, 2);
    ASSERT_EQ(0xA5A5, v);                       /* and the word below it */
}

/* 0x00E660C0 - 0x00E660E8: the socket flags and the failure status. */
TEST(socket_allocation_flags_and_failure)
{
    mock_nexthop_metric = 0;
    run(0, 0x55, 4);
    ASSERT_EQ(0x00200020u, mock_allocate_flags);

    reset_mocks();
    mock_allocate_result = 0;                   /* >= 0 means failure */
    mock_nexthop_metric = 0;
    run(0, 0x55, 4);
    ASSERT_EQ(status_$network_no_more_free_sockets, out_status);
    ASSERT_EQ(0, mock_send_calls);
    ASSERT_EQ(0, mock_close_calls);
}

/* 0x00E66002: a non-positive count returns before anything else happens. */
TEST(non_positive_max_count_returns_immediately)
{
    run(0, 0x55, 0);
    ASSERT_EQ(0u, out_status);
    ASSERT_EQ(0, out_count);
    ASSERT_EQ(0, mock_nexthop_calls);
    ASSERT_EQ(0, mock_send_calls);
}

/* 0x00E66088 - 0x00E66098: a routing failure is reported and nothing opens. */
TEST(nexthop_failure_is_reported)
{
    mock_nexthop_status = 0x003C0001;
    run(0, 0x55, 4);
    ASSERT_EQ(0x003C0001u, out_status);
    ASSERT_EQ(0, mock_send_calls);
    ASSERT_EQ(0, mock_close_calls);
}

int main(void)
{
    printf("ASKNODE_$WHO_NOTOPO tests\n");
    RUN_TEST(request_record_field_offsets);
    RUN_TEST(request_record_carries_only_the_four_fields);
    RUN_TEST(rip_destination_record_puts_node_1_in_host_lo);
    RUN_TEST(local_node_listed_on_a_direct_route_not_a_zero_port);
    RUN_TEST(local_query_lists_the_local_node_whatever_the_metric);
    RUN_TEST(pkt_info_clears_only_the_word_at_offset_8);
    RUN_TEST(socket_allocation_flags_and_failure);
    RUN_TEST(non_positive_max_count_returns_immediately);
    RUN_TEST(nexthop_failure_is_reported);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
