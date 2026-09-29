/*
 * rip/test/test_table.c - Unit tests for RIP_$TABLE_D (0x00E68E2C) and
 * RIP_$TABLE (0x00E68F90).
 *
 * The real rip/table.c and rip/rip_data.c are compiled against a scripted
 * ROUTE_$FIND_PORT and test-owned ROUTE_$PORTP / ROUTE_$PORT_ARRAY tables.
 * Only fields the original writes are asserted: both routines build their
 * records in uninitialised locals and the untouched parts are, as in the
 * image, whatever the stack held.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_failed = 0;
static int tests_passed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { \
        tests_passed++; \
        printf("PASSED\n"); \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    unsigned long _a = (unsigned long)(a); \
    unsigned long _b = (unsigned long)(b); \
    if (_a != _b) { \
        printf("FAILED\n    %s = 0x%lx (%lu), %s = 0x%lx (%lu) at line %d\n", \
               #a, _a, _a, #b, _b, _b, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "rip/rip_internal.h"
#include "route/route.h"

MODULE_DATA_DEFINE(route_$wired_data_t, ROUTE_$WIRED_DATA, 0x00E26EE4);
route_$port_t  ROUTE_$PORT_ARRAY[ROUTE_$MAX_PORTS];

static int      find_port_calls;
static uint16_t find_port_type_seen;
static int32_t  find_port_socket_seen;
static int16_t  find_port_result;

int16_t ROUTE_$FIND_PORT(uint16_t port_type, int32_t socket)
{
    find_port_calls++;
    find_port_type_seen = port_type;
    find_port_socket_seen = socket;
    return find_port_result;
}

#include "../rip_data.c"
#include "../table.c"

static route_$port_t port2, port5;

static void reset(void)
{
    memset(&RIP_$DATA, 0, sizeof(RIP_$DATA));
    memset(ROUTE_$WIRED_DATA.portp, 0, sizeof(ROUTE_$WIRED_DATA.portp));
    memset(ROUTE_$PORT_ARRAY, 0, sizeof(ROUTE_$PORT_ARRAY));
    memset(&port2, 0, sizeof(port2));
    memset(&port5, 0, sizeof(port5));
    port2.network = 0xAAAA0002; port2.port_type = 0x0002; port2.socket = 0x0102;
    port5.network = 0xAAAA0005; port5.port_type = 0x0001; port5.socket = 0x0105;
    ROUTE_$WIRED_DATA.portp[2] = &port2;
    ROUTE_$WIRED_DATA.portp[5] = &port5;
    find_port_calls = 0;
    find_port_result = 2;
}

static void fill_entry(int idx, uint32_t net, int slot, uint32_t exp,
                       uint32_t nh_net, uint8_t nh_last, uint8_t port,
                       uint8_t metric, uint8_t flags)
{
    rip_$route_t *r = &RIP_$DATA.entries[idx].routes[slot];
    RIP_$DATA.entries[idx].network = net;
    r->expiration = exp;
    r->nexthop.network = nh_net;
    r->nexthop.host[0] = 0x10; r->nexthop.host[1] = 0x20; r->nexthop.host[2] = 0x30;
    r->nexthop.host[3] = 0x40; r->nexthop.host[4] = 0x50; r->nexthop.host[5] = nh_last;
    r->port = port;
    r->metric = metric;
    r->flags = flags;
}

/* ==========================================================================
 * RIP_$TABLE_D read
 * ========================================================================== */

TEST(table_d_read_standard_slot)
{
    boolean op = (boolean)0xFF, rt = 0;
    uint16_t index = 7;
    rip_$table_d_buf_t buf;
    status_$t status = -1;
    reset();
    fill_entry(7, 0x11110007, 0, 0x12345678, 0x22220007, 0x66, 2, 3, 0x80 | 0x15);
    memset(&buf, 0xEE, sizeof(buf));

    RIP_$TABLE_D(&op, &rt, &index, &buf, &status);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(index, 7);
    ASSERT_EQ(buf.dest_network, 0x11110007);
    ASSERT_EQ(buf.expiration, 0x12345678);
    ASSERT_EQ(buf.nexthop_network, 0x22220007);
    ASSERT_EQ(buf.nexthop_host[0], 0x10);
    ASSERT_EQ(buf.nexthop_host[5], 0x66);
    ASSERT_EQ(buf.metric, 3);
    ASSERT_EQ(buf.state, 2);
    ASSERT_EQ(buf.port_network, 0x0002);   /* port2.port_type */
    ASSERT_EQ(buf.port_socket, 0x0102);
    ASSERT_EQ(find_port_calls, 0);
}

TEST(table_d_read_nonstandard_slot_masks_index_in_place)
{
    boolean op = (boolean)0xFF, rt = (boolean)0xFF;
    uint16_t index = 0x45;     /* & 0x3F = 5 */
    rip_$table_d_buf_t buf;
    status_$t status = -1;
    reset();
    fill_entry(5, 0x11110005, 1, 0x1, 0x22220005, 0x77, 5, 0x11, 0xC0);
    fill_entry(5, 0x11110005, 0, 0x2, 0x33330005, 0x88, 2, 1, 0x40);

    RIP_$TABLE_D(&op, &rt, &index, &buf, &status);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(index, 5);
    ASSERT_EQ(buf.expiration, 0x1);
    ASSERT_EQ(buf.nexthop_network, 0x22220005);
    ASSERT_EQ(buf.nexthop_host[5], 0x77);
    ASSERT_EQ(buf.metric, 0x11);
    ASSERT_EQ(buf.state, 3);
    ASSERT_EQ(buf.port_network, 0x0001);   /* port5.port_type */
    ASSERT_EQ(buf.port_socket, 0x0105);
}

TEST(table_d_read_invalid_port_gives_1_0)
{
    boolean op = (boolean)0xFF, rt = 0;
    uint16_t index = 1;
    rip_$table_d_buf_t buf;
    status_$t status = -1;
    reset();
    fill_entry(1, 0x11110001, 0, 0, 0, 0, 8, 0, 0);     /* port 8: out of range */

    RIP_$TABLE_D(&op, &rt, &index, &buf, &status);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(buf.port_network, 0x0001);
    ASSERT_EQ(buf.port_socket, 0x0000);

    fill_entry(1, 0x11110001, 0, 0, 0, 0, 0xFF, 0, 0);  /* port 0xFF */
    RIP_$TABLE_D(&op, &rt, &index, &buf, &status);
    ASSERT_EQ(buf.port_network, 0x0001);
    ASSERT_EQ(buf.port_socket, 0x0000);
}

/* ==========================================================================
 * RIP_$TABLE_D write
 * ========================================================================== */

TEST(table_d_write_standard_slot)
{
    boolean op = 0, rt = 0;
    uint16_t index = 0x43;     /* masked to 3 at the store */
    rip_$table_d_buf_t buf;
    status_$t status = -1;
    rip_$route_t *r;
    reset();
    memset(&buf, 0, sizeof(buf));
    buf.expiration = 0xDEADBEEF;
    buf.dest_network = 0x11110003;
    buf.nexthop_network = 0x22220003;
    memcpy(buf.nexthop_host, "\x01\x02\x03\x04\x05\x06", 6);
    buf.port_network = 0x0002;
    buf.port_socket = 0x8102;   /* sign-extended on the way to FIND_PORT */
    buf.metric = 0x0104;        /* only the low byte is stored */
    buf.state = 0x0102;         /* only the low byte, shifted */
    find_port_result = 5;

    RIP_$TABLE_D(&op, &rt, &index, &buf, &status);
    ASSERT_EQ(status, status_$ok);
    ASSERT_EQ(index, 3);
    ASSERT_EQ(find_port_calls, 1);
    ASSERT_EQ(find_port_type_seen, 0x0002);
    ASSERT_EQ((uint32_t)find_port_socket_seen, 0xFFFF8102u);

    ASSERT_EQ(RIP_$DATA.entries[3].network, 0x11110003);
    r = &RIP_$DATA.entries[3].routes[0];
    ASSERT_EQ(r->expiration, 0xDEADBEEF);
    ASSERT_EQ(r->nexthop.network, 0x22220003);
    ASSERT_EQ(r->nexthop.host[0], 0x01);
    ASSERT_EQ(r->nexthop.host[5], 0x06);
    ASSERT_EQ(r->port, 5);
    ASSERT_EQ(r->metric, 0x04);
    ASSERT_EQ(r->flags & 0xC0, 0x80);
}

TEST(table_d_write_unknown_port_sets_status_and_leaves_table)
{
    boolean op = 0, rt = (boolean)0xFF;
    uint16_t index = 9;
    rip_$table_d_buf_t buf;
    status_$t status = -1;
    reset();
    memset(&buf, 0, sizeof(buf));
    buf.dest_network = 0x11110009;
    find_port_result = -1;

    RIP_$TABLE_D(&op, &rt, &index, &buf, &status);
    ASSERT_EQ(status, status_$internet_unknown_network_port);
    ASSERT_EQ(find_port_calls, 1);
    ASSERT_EQ(RIP_$DATA.entries[9].network, 0);
    /* the index is only masked on the successful path */
    ASSERT_EQ(index, 9);
}

/* ==========================================================================
 * RIP_$TABLE
 * ========================================================================== */

TEST(table_read_compacts_the_d_record)
{
    boolean op = (boolean)0xFF;
    uint16_t index = 2;
    rip_$table_buf_t buf;
    reset();
    fill_entry(2, 0x11110002, 0, 0x55555555, 0x22220002, 0xAB, 2, 7, 0x40 | 0x3F);
    /* nexthop host bytes 2..5 = 30 40 50 AB -> low 20 bits 0x050AB */
    memset(&buf, 0, sizeof(buf));
    buf.state_flags = 0xFF;
    find_port_result = 6;

    RIP_$TABLE(&op, &index, &buf);
    ASSERT_EQ(buf.dest_network, 0x11110002);
    ASSERT_EQ(buf.nexthop_host_low, 0x0050AB);
    ASSERT_EQ(buf.expiration, 0x55555555);
    ASSERT_EQ(find_port_calls, 1);
    ASSERT_EQ(find_port_type_seen, 0x0002);      /* port2.port_type */
    ASSERT_EQ(find_port_socket_seen, 0x0102);
    ASSERT_EQ(buf.port_index, 6);
    ASSERT_EQ(buf.metric, 7);
    ASSERT_EQ(buf.state_flags, 0x3F | 0x40);
}

TEST(table_write_goes_through_port_array_then_table_d)
{
    boolean op = 0;
    uint16_t index = 4;
    rip_$table_buf_t buf;
    rip_$route_t *r;
    reset();
    ROUTE_$PORT_ARRAY[3].network = 0x33330003;
    ROUTE_$PORT_ARRAY[3].port_type = 0x0007;
    ROUTE_$PORT_ARRAY[3].socket = 0x0203;
    memset(&buf, 0, sizeof(buf));
    buf.dest_network = 0x11110004;
    buf.nexthop_host_low = 0xFABCD;      /* bits above 20 must not leak in */
    buf.expiration = 0x66666666;
    buf.port_index = 3;
    buf.metric = 9;
    buf.state_flags = 0xC0 | 0x11;
    find_port_result = 1;

    RIP_$TABLE(&op, &index, &buf);
    ASSERT_EQ(find_port_calls, 1);
    ASSERT_EQ(find_port_type_seen, 0x0007);
    ASSERT_EQ(find_port_socket_seen, 0x0203);
    ASSERT_EQ(RIP_$DATA.entries[4].network, 0x11110004);
    r = &RIP_$DATA.entries[4].routes[0];
    ASSERT_EQ(r->expiration, 0x66666666);
    ASSERT_EQ(r->nexthop.network, 0x33330003);
    /* 0x000FABCD over host[2..5]: the top twelve bits (host[2], high
     * nibble of host[3]) are the uninitialised d_buf's, the rest is ours */
    ASSERT_EQ(r->nexthop.host[3] & 0x0F, 0x0F);
    ASSERT_EQ(r->nexthop.host[4], 0xAB);
    ASSERT_EQ(r->nexthop.host[5], 0xCD);
    ASSERT_EQ(r->port, 1);
    ASSERT_EQ(r->metric, 9);
    ASSERT_EQ(r->flags & 0xC0, 0xC0);
}

TEST(table_write_ignores_a_bad_port_index)
{
    boolean op = 0;
    uint16_t index = 4;
    rip_$table_buf_t buf;
    reset();
    memset(&buf, 0, sizeof(buf));
    buf.dest_network = 0x11110004;
    buf.port_index = 8;

    RIP_$TABLE(&op, &index, &buf);
    ASSERT_EQ(find_port_calls, 0);
    ASSERT_EQ(RIP_$DATA.entries[4].network, 0);
}

int main(void)
{
    printf("RIP_$TABLE_D / RIP_$TABLE tests\n");
    RUN_TEST(table_d_read_standard_slot);
    RUN_TEST(table_d_read_nonstandard_slot_masks_index_in_place);
    RUN_TEST(table_d_read_invalid_port_gives_1_0);
    RUN_TEST(table_d_write_standard_slot);
    RUN_TEST(table_d_write_unknown_port_sets_status_and_leaves_table);
    RUN_TEST(table_read_compacts_the_d_record);
    RUN_TEST(table_write_goes_through_port_array_then_table_d);
    RUN_TEST(table_write_ignores_a_bad_port_index);
    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
