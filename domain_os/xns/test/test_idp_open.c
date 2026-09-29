/*
 * xns/test/test_idp_open.c - XNS_IDP_$OPEN (0x00E187AC), bead source-oz2p
 *
 * The three things the bead names:
 *
 *   - 0x00E18992 "move.w (-0x26,A6),(0x8,A2)" stores the channel index as a
 *     WORD at the caller's +0x08, which is the high half of the source
 *     network the connect arm reads from the same place;
 *   - 0x00E18998-0x00E189B6 calls EC2_$REGISTER_EC1 UNCONDITIONALLY, on
 *     SOCK_$SOCKET_PTR[user_socket - 1], with the caller's status cell - not
 *     only when a socket was allocated, and never with NULL;
 *   - 0x00E188E6 "bclr.b #0x7,(0x16,A4)" clears bit 15 of the freshly
 *     allocated socket descriptor's flags word.
 *
 * xns/idp_open.c is #included below so the function under test is the real
 * one; every callee is stubbed here.
 */

#include <stdio.h>
#include <string.h>

#include "xns/xns_internal.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed;

#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
    current_failed = 0;                         \
    setup();                                    \
    name();                                     \
    if (current_failed) {                       \
        tests_failed++;                         \
        printf("FAIL\n");                       \
    } else {                                    \
        tests_passed++;                         \
        printf("ok\n");                         \
    }                                           \
} while (0)

#define ASSERT_NE(unexpected, actual, what) do {                        \
    unsigned long _e = (unsigned long)(unexpected);                     \
    unsigned long _a = (unsigned long)(actual);                         \
    if (_a == _e) {                                                     \
        printf("\n    %s: got 0x%lx, which it must not be", (what), _a); \
        current_failed = 1;                                             \
    }                                                                   \
} while (0)

#define ASSERT_EQ(expected, actual, what) do {                          \
    unsigned long _e = (unsigned long)(expected);                       \
    unsigned long _a = (unsigned long)(actual);                         \
    if (_a != _e) {                                                     \
        printf("\n    %s: got 0x%lx, expected 0x%lx", (what), _a, _e);  \
        current_failed = 1;                                             \
    }                                                                   \
} while (0)

/* ============================================================================
 * Globals
 * ============================================================================ */

MODULE_DATA_DEFINE(xns_$idp_data_t, XNS_IDP_$DATA, 0x00E2B314);

MODULE_DATA_DEFINE(sock_$data_t, SOCK_$DATA, 0x00E27510);
uint16_t PROC1_$AS_ID;

/* ============================================================================
 * Stubs
 * ============================================================================ */

static int      find_socket_calls;
static int8_t   find_socket_result;

int8_t xns_$find_socket(int16_t socket)
{
    (void)socket;
    find_socket_calls++;
    return find_socket_result;
}

static int       alloc_calls;
static uint16_t  alloc_socket;          /* what the stub hands back */
static int8_t    alloc_result;
static uint16_t  alloc_args[4];

int8_t SOCK_$ALLOCATE_USER(uint16_t *sock_ret, uint16_t proto_hi,
                           uint16_t proto_lo, uint16_t queue_hi,
                           uint16_t queue_lo)
{
    alloc_calls++;
    alloc_args[0] = proto_hi;
    alloc_args[1] = proto_lo;
    alloc_args[2] = queue_hi;
    alloc_args[3] = queue_lo;
    *sock_ret = alloc_socket;
    return alloc_result;
}

static int      sock_close_calls;
static uint16_t sock_close_arg;

void SOCK_$CLOSE(uint16_t sock_num)
{
    sock_close_calls++;
    sock_close_arg = sock_num;
}

static int      set_cleanup_calls;
static uint16_t set_cleanup_arg;

void PROC2_$SET_CLEANUP(uint16_t kind)
{
    set_cleanup_calls++;
    set_cleanup_arg = kind;
}

static int        reg_ec1_calls;
static void      *reg_ec1_arg;
static status_$t *reg_ec1_status;
static void      *reg_ec1_result;

void *EC2_$REGISTER_EC1(ec_$eventcount_t *ec1, status_$t *status_ret)
{
    reg_ec1_calls++;
    reg_ec1_arg = ec1;
    reg_ec1_status = status_ret;
    return reg_ec1_result;
}

static int        os_open_calls;
static xns_$os_open_opt_t os_open_seen;
static uint16_t   os_open_channel;       /* what the stub writes back */
static status_$t  os_open_status;

void XNS_IDP_$OS_OPEN(xns_$os_open_opt_t *options, status_$t *status_ret)
{
    os_open_calls++;
    os_open_seen = *options;
    options->flags_channel = os_open_channel;
    *status_ret = os_open_status;
}

void XNS_IDP_$OS_CLOSE(int16_t *channel, status_$t *status_ret)
{
    (void)channel;
    *status_ret = status_$ok;
}

static int excl_start_calls;
static int excl_stop_calls;

void ML_$EXCLUSION_START(ml_$exclusion_t *e) { (void)e; excl_start_calls++; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *e)  { (void)e; excl_stop_calls++; }

void XNS_IDP_$DEMUX(xns_$pkt_desc_t *rec, uint16_t *port_type,
                    uint16_t *port_socket, boolean *mac_broadcast,
                    status_$t *status_ret)
{
    (void)rec; (void)port_type; (void)port_socket; (void)mac_broadcast;
    (void)status_ret;
}

/* The code under test, for real. */
#include "../idp_open.c"

/* ============================================================================
 * Fixtures
 * ============================================================================ */

#define TEST_USER_SOCKET 0x0021
#define TEST_CHANNEL     0x0005

static xns_$idp_open_opt_t opt;
static status_$t st;
static sock_$sock_t sock_descs[4];

static xns_$channel_t *channel_base(uint16_t idx)
{
    return &XNS_IDP_$DATA.channels[idx];
}

static void setup(void)
{
    memset(&XNS_IDP_$DATA, 0, sizeof(XNS_IDP_$DATA));
    memset(&SOCK_$DATA, 0, sizeof(SOCK_$DATA));
    memset(sock_descs, 0, sizeof(sock_descs));
    memset(&opt, 0, sizeof(opt));

    find_socket_calls = 0;
    find_socket_result = 0;
    alloc_calls = 0;
    alloc_result = -1;                  /* success */
    alloc_socket = TEST_USER_SOCKET;
    sock_close_calls = 0;
    set_cleanup_calls = 0;
    reg_ec1_calls = 0;
    reg_ec1_arg = NULL;
    reg_ec1_result = (void *)0x12345678;
    os_open_calls = 0;
    os_open_channel = TEST_CHANNEL;
    os_open_status = status_$ok;
    excl_start_calls = 0;
    excl_stop_calls = 0;
    PROC1_$AS_ID = 3;

    /* SOCK_$DATA.socket_ptr is indexed with the socket number. */
    SOCK_$DATA.socket_ptr[TEST_USER_SOCKET] = (sock_$sock_t *)&sock_descs[0];
    sock_descs[0].flags = 0xFFFF;

    opt.version = 1;
    opt.socket = 0x0451;
    opt.buffer_size = 0x40;

    st = 0x5A5A5A5A;
}

/* ============================================================================
 * The socket descriptor's flags bit
 * ============================================================================ */

/*
 * 0x00E188D2-0x00E188E6: "A0 = 0xE28DB4 / D0 = sock << 2 / A1 = A0 + D0 /
 * A4 = (-0x4,A1) / bclr.b #0x7,(0x16,A4)".  The -4 is what makes the table
 * 1-based, and +0x16 is sock_$sock_t.flags.
 */
static void test_allocated_socket_gets_its_top_flag_cleared(void)
{
    XNS_IDP_$OPEN(&opt, &st);

    ASSERT_EQ(status_$ok, st, "status");
    ASSERT_EQ(1, alloc_calls, "a socket was allocated");
    ASSERT_EQ(0x7FFF, sock_descs[0].flags, "bit 15 cleared, the rest kept");
}

/*
 * 0x00E188A6-0x00E188BA: the depth word is pushed three times ("move.w (SP),
 * -(SP)" twice) with 0x400 above it.
 */
static void test_allocate_user_arguments(void)
{
    opt.buffer_size = 0x123;

    XNS_IDP_$OPEN(&opt, &st);

    ASSERT_EQ(0x123, alloc_args[0], "proto_hi");
    ASSERT_EQ(0x123, alloc_args[1], "proto_lo");
    ASSERT_EQ(0x123, alloc_args[2], "queue_hi");
    ASSERT_EQ(0x400, alloc_args[3], "queue_lo is the constant 0x400");
}

/* ============================================================================
 * The channel word store
 * ============================================================================ */

/*
 * 0x00E18992 "move.w (-0x26,A6),(0x8,A2)": a WORD, into the caller's +0x08.
 * The bytes above and below it are untouched, so the low half of the source
 * network the caller supplied survives and its high half does not.
 */
static void test_channel_index_lands_in_the_options_word_at_0x08(void)
{
    opt.channel_ret = 0x1111;
    opt.src_network_lo = 0x2222;

    XNS_IDP_$OPEN(&opt, &st);

    ASSERT_EQ(status_$ok, st, "status");
    ASSERT_EQ(TEST_CHANNEL, (uint16_t)opt.channel_ret, "the channel index");
    ASSERT_EQ(0x2222, opt.src_network_lo, "the next word is untouched");
    ASSERT_EQ(8, offsetof(xns_$idp_open_opt_t, channel_ret),
              "and the cell really is +0x08");
}

/*
 * 0x00E18906 "move.w (0x20,A2),(-0x26,A6)": the whole flag WORD goes to the
 * OS record's +0x02, and 0x00E188F8 / 0x00E188FE fill in the socket and the
 * demux vector.
 */
static void test_os_record_is_built_from_the_caller_record(void)
{
    opt.flags_hi = 0x5A;
    opt.flags = XNS_OPEN_FLAG_BIND_LOCAL;
    opt.network = 0xCAFEBABE;

    XNS_IDP_$OPEN(&opt, &st);

    ASSERT_EQ(0x0451, os_open_seen.socket, "the socket");
    ASSERT_EQ(0x5A02, os_open_seen.flags_channel, "both halves of the flag word");
    ASSERT_EQ((uint32_t)(uintptr_t)&XNS_IDP_$DEMUX, os_open_seen.demux,
              "the demux vector");
    ASSERT_EQ(0xCAFEBABEu, os_open_seen.network,
              "the bind flag copies the network across");
}

/* 0x00E1890C: without the bind flag the network is NOT copied. */
static void test_network_only_copied_when_binding(void)
{
    opt.network = 0xCAFEBABE;

    XNS_IDP_$OPEN(&opt, &st);

    /*
     * 0x00E18914 is the ONLY write to the OS record's +0x08, and it is under
     * the bind flag, so without it the slot keeps whatever the frame held.
     * The test can only say that the caller's network did not get there.
     */
    ASSERT_NE(0xCAFEBABEu, os_open_seen.network, "the network was not copied");
}

/*
 * 0x00E18922: 24 bytes from the caller's +0x08 to the OS record's +0x0C -
 * source address first, destination second.
 */
static void test_connect_block_is_copied(void)
{
    uint8_t pattern[24];
    int i;

    for (i = 0; i < 24; i++) {
        pattern[i] = (uint8_t)(0x40 + i);
    }
    opt.flags = XNS_OPEN_FLAG_CONNECT;
    memcpy(&opt.channel_ret, pattern, 24);

    XNS_IDP_$OPEN(&opt, &st);

    ASSERT_EQ(0, memcmp(&os_open_seen.src_network, pattern, 24),
              "the 24 bytes at the caller's +0x08 land at the OS record's +0x0C");
    ASSERT_EQ(0x0C, offsetof(xns_$os_open_opt_t, src_network),
              "and the destination really is +0x0C");
    ASSERT_EQ(24, offsetof(xns_$os_open_opt_t, dest_socket) + 2 -
                  offsetof(xns_$os_open_opt_t, src_network),
              "source address then destination address");
}

/* ============================================================================
 * EC2_$REGISTER_EC1 is unconditional
 * ============================================================================ */

/*
 * 0x00E18998-0x00E189B6 has no test in front of it.  With a socket, the
 * argument is that socket's descriptor and the result lands at the caller's
 * +0x04.
 */
static void test_register_ec1_uses_the_socket_descriptor(void)
{
    XNS_IDP_$OPEN(&opt, &st);

    ASSERT_EQ(1, reg_ec1_calls, "registered once");
    ASSERT_EQ((uintptr_t)&sock_descs[0], (uintptr_t)reg_ec1_arg,
              "SOCK_$DATA.socket_ptr[socket]");
    ASSERT_EQ((uintptr_t)&st, (uintptr_t)reg_ec1_status,
              "the caller's own status cell is the second argument");
    ASSERT_EQ(0x12345678u, opt.network, "the result lands at options + 0x04");
}

/*
 * QUIRK: with XNS_OPEN_FLAG_NO_ALLOC no socket is allocated, user_socket is
 * 0xE1 - and EC2_$REGISTER_EC1 still runs, on SOCK_$DATA.socket_ptr[0xE1],
 * one past the 0xE0 descriptors SOCK_$INIT sets up (the slot that overlays
 * the SOCK block's user-limit word).
 */
static void test_register_ec1_runs_even_with_no_socket(void)
{
    opt.flags = XNS_OPEN_FLAG_NO_ALLOC;
    SOCK_$DATA.socket_ptr[XNS_NO_SOCKET] = (sock_$sock_t *)&sock_descs[1];

    XNS_IDP_$OPEN(&opt, &st);

    ASSERT_EQ(status_$ok, st, "status");
    ASSERT_EQ(0, alloc_calls, "no socket was allocated");
    ASSERT_EQ(1, reg_ec1_calls, "but the eventcount is registered anyway");
    ASSERT_EQ((uintptr_t)&sock_descs[1], (uintptr_t)reg_ec1_arg,
              "the entry one past the end of the descriptor table");
    ASSERT_EQ(XNS_NO_SOCKET,
              channel_base(TEST_CHANNEL)->user_socket,
              "the channel records 'no user socket'");
}

/* 0x00E18944-0x00E18956: a failed OS open closes the socket and returns
 * BEFORE the registration. */
static void test_failed_os_open_closes_the_socket(void)
{
    os_open_status = status_$xns_channel_table_full;

    XNS_IDP_$OPEN(&opt, &st);

    ASSERT_EQ(status_$xns_channel_table_full, st, "status");
    ASSERT_EQ(1, sock_close_calls, "the socket was closed");
    ASSERT_EQ(TEST_USER_SOCKET, sock_close_arg, "the one that was allocated");
    ASSERT_EQ(0, reg_ec1_calls, "no registration");
    ASSERT_EQ(0, set_cleanup_calls, "no cleanup handler");
}

/* 0x00E18946: with NO_ALLOC there is nothing to close. */
static void test_failed_os_open_with_no_socket(void)
{
    opt.flags = XNS_OPEN_FLAG_NO_ALLOC;
    os_open_status = status_$xns_channel_table_full;

    XNS_IDP_$OPEN(&opt, &st);

    ASSERT_EQ(0, sock_close_calls, "nothing to close");
}

/* 0x00E1895A: the cleanup kind is 0x0E, and the user socket goes into the
 * channel under the exclusion lock. */
static void test_success_path_bookkeeping(void)
{
    XNS_IDP_$OPEN(&opt, &st);

    ASSERT_EQ(1, set_cleanup_calls, "PROC2_$SET_CLEANUP ran");
    ASSERT_EQ(0x0E, set_cleanup_arg, "with 0x0E");
    ASSERT_EQ(1, excl_start_calls, "lock taken");
    ASSERT_EQ(1, excl_stop_calls, "lock released");
    ASSERT_EQ(TEST_USER_SOCKET,
              channel_base(TEST_CHANNEL)->user_socket,
              "the channel's user socket");
}

/* ============================================================================
 * Validation
 * ============================================================================ */

static void test_version_must_be_one(void)
{
    opt.version = 2;
    XNS_IDP_$OPEN(&opt, &st);
    ASSERT_EQ(status_$xns_version_mismatch, st, "status");
    ASSERT_EQ(0, os_open_calls, "nothing else happened");
}

static void test_reserved_sockets_are_refused(void)
{
    const int16_t reserved[4] = { -1, XNS_SOCKET_ROUTER, XNS_SOCKET_ERROR,
                                  XNS_SOCKET_RIP };
    int i;

    for (i = 0; i < 4; i++) {
        setup();
        opt.socket = reserved[i];
        XNS_IDP_$OPEN(&opt, &st);
        ASSERT_EQ(status_$xns_reserved_socket, st, "reserved socket refused");
    }
}

static void test_socket_already_in_use(void)
{
    find_socket_result = -1;
    XNS_IDP_$OPEN(&opt, &st);
    ASSERT_EQ(status_$xns_socket_in_use, st, "status");
}

static void test_flag_conflicts(void)
{
    setup();
    opt.flags = XNS_OPEN_FLAG_BIND_LOCAL | XNS_OPEN_FLAG_NO_ALLOC;
    XNS_IDP_$OPEN(&opt, &st);
    ASSERT_EQ(status_$xns_incompatible_flags, st, "bind + no-alloc");

    setup();
    opt.flags = XNS_OPEN_FLAG_BIND_LOCAL | XNS_OPEN_FLAG_CONNECT;
    XNS_IDP_$OPEN(&opt, &st);
    ASSERT_EQ(status_$xns_connect_bind_conflict, st, "bind + connect");

    setup();
    opt.flags = XNS_OPEN_FLAG_CONNECT | XNS_OPEN_FLAG_NO_ALLOC;
    XNS_IDP_$OPEN(&opt, &st);
    ASSERT_EQ(status_$xns_incompatible_flags2, st, "connect + no-alloc");
}

/* 0x00E18860-0x00E1888E: either host being all ones is refused. */
static void test_connect_to_broadcast(void)
{
    setup();
    opt.flags = XNS_OPEN_FLAG_CONNECT;
    opt.dest_host_hi = opt.dest_host_mid = opt.dest_host_lo = 0xFFFF;
    XNS_IDP_$OPEN(&opt, &st);
    ASSERT_EQ(status_$xns_connect_to_broadcast, st, "broadcast destination");

    setup();
    opt.flags = XNS_OPEN_FLAG_CONNECT;
    opt.src_host_hi = opt.src_host_mid = opt.src_host_lo = 0xFFFF;
    XNS_IDP_$OPEN(&opt, &st);
    ASSERT_EQ(status_$xns_connect_to_broadcast, st, "broadcast source");
}

static void test_zero_buffer_size(void)
{
    opt.buffer_size = 0;
    XNS_IDP_$OPEN(&opt, &st);
    ASSERT_EQ(status_$xns_no_buffer_size, st, "status");
    ASSERT_EQ(0, alloc_calls, "nothing allocated");
}

static void test_no_os_sockets(void)
{
    alloc_result = 0;                   /* SOCK_$ALLOCATE_USER failed */
    XNS_IDP_$OPEN(&opt, &st);
    ASSERT_EQ(status_$xns_no_os_sockets, st, "status");
    ASSERT_EQ(0, os_open_calls, "the OS open never ran");
}

int main(void)
{
    printf("XNS_IDP_$OPEN (0x00E187AC) tests\n");

    RUN_TEST(test_allocated_socket_gets_its_top_flag_cleared);
    RUN_TEST(test_allocate_user_arguments);
    RUN_TEST(test_channel_index_lands_in_the_options_word_at_0x08);
    RUN_TEST(test_os_record_is_built_from_the_caller_record);
    RUN_TEST(test_network_only_copied_when_binding);
    RUN_TEST(test_connect_block_is_copied);
    RUN_TEST(test_register_ec1_uses_the_socket_descriptor);
    RUN_TEST(test_register_ec1_runs_even_with_no_socket);
    RUN_TEST(test_failed_os_open_closes_the_socket);
    RUN_TEST(test_failed_os_open_with_no_socket);
    RUN_TEST(test_success_path_bookkeeping);
    RUN_TEST(test_version_must_be_one);
    RUN_TEST(test_reserved_sockets_are_refused);
    RUN_TEST(test_socket_already_in_use);
    RUN_TEST(test_flag_conflicts);
    RUN_TEST(test_connect_to_broadcast);
    RUN_TEST(test_zero_buffer_size);
    RUN_TEST(test_no_os_sockets);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
