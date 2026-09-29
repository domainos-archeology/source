/*
 * xns/test/test_idp_os_open.c - XNS_IDP_$OS_OPEN (0x00E17F02), bead source-iqgf
 *
 * The four things the bead names, each with the address of the instruction
 * that settles it:
 *
 *   (a) the all-zero test at 0x00E18008-0x00E18024 and the
 *       xns_$is_broadcast_addr call at 0x00E1802A look at the SOURCE address
 *       at options +0x0C, not the destination at +0x18;
 *   (b) 0x00E180DA-0x00E180EE takes the connected channel's source network
 *       from the longword the PORT TABLE entry points at, not from
 *       ROUTE_$PORTP;
 *   (c) 0x00E18130 "cmpi.w #-0x2,(0x536,A5)" / `bls' wraps the socket
 *       allocator only once it has passed 0xFFFE;
 *   (d) the "IDP socket in use" exit at 0x00E17F46 branches INTO the cleanup
 *       arm, so it releases an exclusion lock it never took.
 *
 * xns/idp_os_open.c is #included below so the function under test is the real
 * one; every callee is stubbed here and records what it was handed.
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
 *
 * The code under test works on the module block XNS_IDP_$DATA.  Here that
 * block is the head of a much larger object, because two paths index the
 * channel table out of range: the "IDP socket in use" arm uses an
 * INDETERMINATE index (see quirk (d)), and a full table reads the state
 * word of a seventeenth channel (0x00E17F72).  In the image both land past
 * the 0x53C-byte block; here they land in `spill'.  Any 16-bit index must
 * fit: 0xFFFF host channels of at most 0x60 bytes is under 6 MB.
 * ============================================================================ */

#define ARENA_SPILL 0x600000
static struct {
    xns_$idp_data_t block;
    uint8_t         spill[ARENA_SPILL];
} idp_arena;
#define XNS_IDP_$DATA (idp_arena.block)

uint16_t PROC1_$AS_ID;

/* A second arena for anything the code reaches through a target VA. */
#define TARGET_VA_BASE 0x00100000u
static uint8_t target_arena[0x1000];

static uint32_t ptr_to_va(const void *p)
{
    return (uint32_t)((const uint8_t *)p - target_arena) + TARGET_VA_BASE;
}

/* ============================================================================
 * Stubs
 * ============================================================================ */

static int      find_socket_calls;
static int16_t  find_socket_arg[16];
static int8_t   find_socket_result;     /* what a call past the sequence returns */
static int8_t   find_socket_seq[8];     /* answers for the first calls */
static int      find_socket_seq_len;

int8_t xns_$find_socket(int16_t socket)
{
    int8_t answer = (find_socket_calls < find_socket_seq_len)
                        ? find_socket_seq[find_socket_calls]
                        : find_socket_result;

    if (find_socket_calls < 16) {
        find_socket_arg[find_socket_calls] = socket;
    }
    find_socket_calls++;
    return answer;
}

static int       add_port_calls;
static uint16_t  add_port_channel[16];
static int16_t   add_port_port[16];
static status_$t add_port_status;       /* what every call reports */

void xns_$add_port(uint16_t channel, int16_t port, status_$t *status_ret)
{
    if (add_port_calls < 16) {
        add_port_channel[add_port_calls] = channel;
        add_port_port[add_port_calls] = port;
    }
    add_port_calls++;
    *status_ret = add_port_status;
}

static int    is_bcast_calls;
static void  *is_bcast_arg;
static int8_t is_bcast_result;

int8_t xns_$is_broadcast_addr(void *addr)
{
    is_bcast_calls++;
    is_bcast_arg = addr;
    return is_bcast_result;
}

static int      net_to_port_calls;
static int16_t  net_to_port_result;

void MAC_$NET_TO_PORT_NUM(int32_t *net_id, int16_t *port_ret)
{
    (void)net_id;
    net_to_port_calls++;
    *port_ret = net_to_port_result;
}

static int       arp_calls;
static void     *arp_addr_info;
static int16_t   arp_port;
static uint16_t *arp_mac_addr;

void MAC_OS_$ARP(void *addr_info, int16_t port_num, uint16_t *mac_addr,
                 uint8_t *flags, status_$t *status_ret)
{
    arp_calls++;
    arp_addr_info = addr_info;
    arp_port = port_num;
    arp_mac_addr = mac_addr;
    *flags = 0;
    *status_ret = status_$ok;
}

static int       nexthop_calls;
static void     *nexthop_addr;
static boolean   nexthop_flags;
static int16_t   nexthop_port;
static status_$t nexthop_status;

int16_t RIP_$FIND_NEXTHOP(void *addr_info, boolean flags, int16_t *port_ret,
                          void *nexthop_ret, status_$t *status_ret)
{
    nexthop_calls++;
    nexthop_addr = addr_info;
    nexthop_flags = flags;
    memset(nexthop_ret, 0xA5, 10);
    *port_ret = nexthop_port;
    *status_ret = nexthop_status;
    return 0;
}

static int excl_start_calls;
static int excl_stop_calls;
static void *excl_last;

void ML_$EXCLUSION_START(ml_$exclusion_t *e) { excl_start_calls++; excl_last = e; }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *e)  { excl_stop_calls++;  excl_last = e; }

/* Only the address of XNS_IDP_$DEMUX is ever used here. */
void XNS_IDP_$DEMUX(xns_$pkt_desc_t *rec, uint16_t *port_type,
                    uint16_t *port_socket, boolean *mac_broadcast,
                    status_$t *status_ret)
{
    (void)rec; (void)port_type; (void)port_socket; (void)mac_broadcast;
    (void)status_ret;
}

/* The code under test, for real. */
#include "../idp_os_open.c"

/* xns/idp_os_open.c also carries XNS_IDP_$OS_CLOSE, which needs this one. */
void xns_$delete_port(uint16_t channel, int16_t port, status_$t *status_ret)
{
    (void)channel; (void)port; *status_ret = status_$ok;
}

/* ============================================================================
 * Fixtures
 * ============================================================================ */

static xns_$os_open_opt_t opt;
static status_$t          st;

/* The network number the port table's entry for port 3 points at. */
#define TEST_PORT        3
#define TEST_PORT_NET    0x11223344u
static uint32_t *port_net_cell;

static xns_$channel_t *channel_base(uint16_t idx)
{
    return &XNS_IDP_$DATA.channels[idx];
}

static void setup(void)
{
    memset(&idp_arena, 0, sizeof(idp_arena));
    memset(target_arena, 0, sizeof(target_arena));
    memset(&opt, 0, sizeof(opt));

    find_socket_calls = 0;
    find_socket_result = 0;                 /* "not in use" */
    find_socket_seq_len = 0;
    add_port_calls = 0;
    add_port_status = status_$ok;
    is_bcast_calls = 0;
    is_bcast_arg = NULL;
    is_bcast_result = -1;                   /* "local/broadcast" -> accepted */
    net_to_port_calls = 0;
    net_to_port_result = TEST_PORT;
    arp_calls = 0;
    nexthop_calls = 0;
    nexthop_port = TEST_PORT;
    nexthop_status = status_$ok;
    excl_start_calls = 0;
    excl_stop_calls = 0;

    ARCH_HOST_VA_BASE = (uintptr_t)target_arena - (uintptr_t)TARGET_VA_BASE;

    /* xns_$port_state_t[TEST_PORT].net_addr_ptr -> a cell in the target arena. */
    port_net_cell = (uint32_t *)&target_arena[0x40];
    *port_net_cell = TEST_PORT_NET;
    XNS_IDP_$DATA.ports[TEST_PORT].net_addr_ptr = ptr_to_va(port_net_cell);

    /* The node's first registered host address, state +0x20. */
    memcpy(XNS_IDP_$DATA.addrs[0], "\x08\x00\x1E\x0A\x0B\x0C", 6);

    XNS_IDP_$DATA.next_socket = XNS_FIRST_DYNAMIC_PORT;
    PROC1_$AS_ID = 0x0007;

    st = 0x5A5A5A5A;
}

/* ============================================================================
 * (d) the socket-in-use exit falls into the cleanup arm
 * ============================================================================ */

/*
 * 0x00E17F40 "move.l #0x3b000e,(A2)" then 0x00E17F46 "bra.w 0x00E181A0".
 * 0x00E181A0 is the `bne' of the pair at 0x00E1819E, and the store above
 * leaves Z clear, so it always continues into the cleanup arm at 0x00E181AA
 * and off the end of it into the ML_$EXCLUSION_STOP at 0x00E181C8 - for a
 * lock this path never took.
 */
static void test_socket_in_use_releases_a_lock_it_never_took(void)
{
    opt.socket = 0x1234;
    find_socket_result = -1;                /* in use */

    XNS_IDP_$OS_OPEN(&opt, &st);

    ASSERT_EQ(status_$xns_socket_in_use, st, "status");
    ASSERT_EQ(1, find_socket_calls, "the socket was looked up once");
    ASSERT_EQ(0x1234, find_socket_arg[0], "options->socket was the argument");
    ASSERT_EQ(0, excl_start_calls, "the lock is never taken on this path");
    ASSERT_EQ(1, excl_stop_calls, "but it IS released");
}

/*
 * 0x00E17F1A / 0x00E17F28: the "table full" exit is the only early one that
 * jumps past the cleanup arm, so it does NOT touch the lock.
 */
static void test_table_full_exits_before_the_lock(void)
{
    XNS_IDP_$DATA.open_channels = XNS_MAX_CHANNELS;

    XNS_IDP_$OS_OPEN(&opt, &st);

    ASSERT_EQ(status_$xns_idp_socket_table_full, st, "status");
    ASSERT_EQ(0, excl_start_calls, "no lock taken");
    ASSERT_EQ(0, excl_stop_calls, "and none released");
    ASSERT_EQ(0, find_socket_calls, "the socket is never looked at");
}

/* ============================================================================
 * (a) the connect arm tests the SOURCE address at options +0x0C
 * ============================================================================ */

/*
 * 0x00E18008-0x00E18024 tests the longword +0x0C and the words +0x16, +0x10,
 * +0x12 and +0x14 - twelve bytes of SOURCE.  A non-zero DESTINATION at +0x18
 * must not make any difference.
 */
static void test_zero_source_takes_the_local_arm(void)
{
    opt.flags_channel = XNS_OPEN_FLAG_CONNECT;
    opt.dest_network  = 0xDEADBEEF;         /* +0x18: nothing to do with it */
    opt.dest_host_hi  = 0xFFFF;
    opt.dest_socket   = 0x0451;

    XNS_IDP_$OS_OPEN(&opt, &st);

    ASSERT_EQ(status_$ok, st, "status");
    ASSERT_EQ(0, is_bcast_calls, "an all-zero source skips the check");
}

/*
 * 0x00E1802A "pea (0xc,A1)": the argument is the SOURCE address.  Any of the
 * five cells being non-zero is enough to get there - here it is the SOCKET at
 * +0x16, which is the second cell the image looks at.
 */
static void test_nonzero_source_socket_is_checked(void)
{
    opt.flags_channel = XNS_OPEN_FLAG_CONNECT;
    opt.src_socket    = 0x0451;             /* +0x16 */
    is_bcast_result   = 0;                  /* not one of ours */

    XNS_IDP_$OS_OPEN(&opt, &st);

    ASSERT_EQ(status_$xns_source_must_be_this_node, st, "status");
    ASSERT_EQ(1, is_bcast_calls, "the source was checked");
    ASSERT_EQ((uintptr_t)&opt.src_network, (uintptr_t)is_bcast_arg,
              "the argument is options + 0x0C");
}

/*
 * The same with a non-zero destination and a completely zero source: the
 * check must NOT run, which is what fails if the C reads +0x18.
 */
static void test_nonzero_destination_alone_is_not_checked(void)
{
    opt.flags_channel = XNS_OPEN_FLAG_CONNECT;
    opt.dest_network  = 0x01020304;
    opt.dest_host_hi  = 0x0506;
    opt.dest_host_mid = 0x0708;
    opt.dest_host_lo  = 0x090A;
    opt.dest_socket   = 0x0B0C;
    is_bcast_result   = 0;                  /* would reject if it ran */

    XNS_IDP_$OS_OPEN(&opt, &st);

    ASSERT_EQ(status_$ok, st, "the destination is not the address checked");
    ASSERT_EQ(0, is_bcast_calls, "xns_$is_broadcast_addr never ran");
    ASSERT_EQ(1, nexthop_calls, "but the destination IS routed");
    ASSERT_EQ((uintptr_t)&opt.dest_network, (uintptr_t)nexthop_addr,
              "RIP_$FIND_NEXTHOP gets options + 0x18");
}

/* ============================================================================
 * (b) the local source network comes from the port table
 * ============================================================================ */

/*
 * 0x00E180DA-0x00E180EE: "lea (0,A5,port*0xC),A0 / movea.l (0x44,A0),A3 /
 * move.l (A3),(0xb0,A1)" - state + 0x40 + port*0x0C + 0x04 is
 * xns_$port_state_t.net_addr_ptr, and the network is the longword it POINTS
 * AT.  The three words at 0x00E180F8 then copy the node's first registered
 * host address out of state +0x20.
 */
static void test_local_source_comes_from_the_port_table(void)
{
    xns_$channel_t *chan;

    opt.flags_channel = XNS_OPEN_FLAG_CONNECT;
    opt.dest_network  = 0x01020304;

    XNS_IDP_$OS_OPEN(&opt, &st);
    ASSERT_EQ(status_$ok, st, "status");

    chan = channel_base(0);
    ASSERT_EQ(TEST_PORT_NET, chan->src_network,
              "source network is *port_state[3].net_addr_ptr");
    ASSERT_EQ(0, memcmp(chan->src_host,
                        XNS_IDP_$DATA.addrs[0], 6),
              "source host is the state's first registered address");
    ASSERT_EQ(TEST_PORT, chan->connected_port,
              "the connected port");
    ASSERT_EQ(0x01020304u, chan->dest_network,
              "the destination address was copied in");
}

/*
 * 0x00E1810A "move.w (0xd8,A1),(0xba,A1)" runs BEFORE 0x00E1816A writes the
 * channel's XNS socket, so the source port it copies is whatever the slot
 * held from its last use.  Reproduced as found.
 */
static void test_source_port_copies_the_stale_socket(void)
{
    xns_$channel_t *chan = channel_base(0);

    chan->xns_socket = (int16_t)0xBEEF;   /* leftover */

    opt.flags_channel = XNS_OPEN_FLAG_CONNECT;
    opt.dest_network  = 0x01020304;
    opt.socket        = 0x0777;

    XNS_IDP_$OS_OPEN(&opt, &st);

    ASSERT_EQ(0xBEEF, chan->src_port,
              "the stale value, not the socket being assigned");
    ASSERT_EQ(0x0777, (uint16_t)chan->xns_socket,
              "the socket is only written afterwards");
}

/* 0x00E18112-0x00E18120: a source the caller supplied is copied verbatim. */
static void test_explicit_source_is_copied(void)
{
    xns_$channel_t *chan;

    opt.flags_channel = XNS_OPEN_FLAG_CONNECT;
    opt.src_network   = 0xAABBCCDD;
    opt.src_host_hi   = 0x1122;
    opt.src_host_mid  = 0x3344;
    opt.src_host_lo   = 0x5566;
    opt.src_socket    = 0x7788;
    opt.dest_network  = 0x01020304;
    is_bcast_result   = -1;                 /* accepted */

    XNS_IDP_$OS_OPEN(&opt, &st);
    ASSERT_EQ(status_$ok, st, "status");

    chan = channel_base(0);
    ASSERT_EQ(0, memcmp(&chan->src_network, &opt.src_network, 12),
              "twelve bytes of caller-supplied source");
}

/* ============================================================================
 * (c) the socket allocator wraps only past 0xFFFE
 * ============================================================================ */

/*
 * 0x00E18128 hands out the CURRENT value, then 0x00E1812C-0x00E18138 advance
 * it.  "cmpi.w #-0x2,(0x536,A5)" / `bls' is an unsigned <= 0xFFFE, so 0xFFFE
 * itself survives and only 0xFFFF is replaced by 0xBB9.
 */
static void test_allocator_does_not_wrap_at_0xfffe(void)
{
    XNS_IDP_$DATA.next_socket = 0xFFFD;

    XNS_IDP_$OS_OPEN(&opt, &st);

    ASSERT_EQ(status_$ok, st, "status");
    ASSERT_EQ(0xFFFD, (uint16_t)opt.socket, "the socket handed out");
    ASSERT_EQ(0xFFFE, XNS_IDP_$DATA.next_socket, "0xFFFE is still a legal value");
    ASSERT_EQ(1, find_socket_calls, "the NEW value is the one probed");
    ASSERT_EQ((int16_t)0xFFFE, find_socket_arg[0], "and it was 0xFFFE");
}

static void test_allocator_wraps_past_0xfffe(void)
{
    XNS_IDP_$DATA.next_socket = 0xFFFE;

    XNS_IDP_$OS_OPEN(&opt, &st);

    ASSERT_EQ(status_$ok, st, "status");
    ASSERT_EQ(0xFFFE, (uint16_t)opt.socket, "0xFFFE is still handed out");
    ASSERT_EQ(XNS_FIRST_DYNAMIC_PORT, XNS_IDP_$DATA.next_socket, "then it wraps");
}

/*
 * 0x00E1814C "bmi.b 0x00E1812C": the advance repeats while the candidate is
 * in use, so a busy stretch is skipped - but the socket already handed out is
 * NOT re-examined.
 */
static void test_allocator_skips_sockets_in_use(void)
{
    XNS_IDP_$DATA.next_socket = 0x0100;

    /* 0x0101 and 0x0102 are in use, 0x0103 is free. */
    find_socket_seq[0] = -1;
    find_socket_seq[1] = -1;
    find_socket_seq[2] = 0;
    find_socket_seq_len = 3;

    XNS_IDP_$OS_OPEN(&opt, &st);

    ASSERT_EQ(status_$ok, st, "status");
    ASSERT_EQ(0x0100, (uint16_t)opt.socket,
              "the value handed out is the one from BEFORE the advance");
    ASSERT_EQ(3, find_socket_calls, "three probes");
    ASSERT_EQ(0x0101, find_socket_arg[0], "first candidate");
    ASSERT_EQ(0x0102, find_socket_arg[1], "second candidate");
    ASSERT_EQ(0x0103, find_socket_arg[2], "third candidate");
    ASSERT_EQ(0x0103, XNS_IDP_$DATA.next_socket, "the allocator stops on the free one");
}

/* ============================================================================
 * The rest of the channel slot
 * ============================================================================ */

/*
 * 0x00E18162 / 0x00E1816A / 0x00E1816E / 0x00E18174 and the flag word built
 * at 0x00E1817A-0x00E1819A.  Open-flag bit n lands in word bit n+11 because
 * the three operations are BYTE operations on the word's high half; the
 * AS_ID then goes into bits 5..10.
 */
static void test_channel_slot_is_stamped(void)
{
    xns_$channel_t *chan = channel_base(0);

    opt.socket        = 0x0451;
    opt.flags_channel = XNS_OPEN_FLAG_CONNECT | XNS_OPEN_FLAG_NO_ALLOC;
    opt.dest_network  = 0x01020304;
    opt.demux         = 0x00E00A90;
    /* Bit 0 of the low 5 the andi.w #-0x7e1 keeps must survive. */
    chan->flags = 0xFFFF;

    XNS_IDP_$OS_OPEN(&opt, &st);
    ASSERT_EQ(status_$ok, st, "status");

    ASSERT_EQ(0x8000, (uint16_t)chan->state & 0x8000,
              "the state's bit 15 is set");
    ASSERT_EQ(0x0451, chan->xns_socket, "socket");
    ASSERT_EQ(XNS_NO_SOCKET, chan->user_socket,
              "no user socket yet");
    ASSERT_EQ(0x00E00A90u, (uint32_t)(uintptr_t)chan->demux,
              "the demux vector, one longword");
    ASSERT_EQ(XNS_CHAN_FLAG_CONNECT | XNS_CHAN_FLAG_NO_ALLOC |
              (7 << XNS_CHAN_FLAG_AS_ID_SHIFT) | 0x001F,
              chan->flags,
              "flags << 11, AS_ID in bits 5..10, low five bits kept");
    ASSERT_EQ(1, XNS_IDP_$DATA.open_channels, "the open count went up");
    ASSERT_EQ(0, opt.flags_channel, "the channel index replaced the flags");
    ASSERT_EQ(1, excl_start_calls, "the lock was taken");
    ASSERT_EQ(1, excl_stop_calls, "and released");
}

/*
 * 0x00E17F72 walks the state words at state + n*0x48 + 0xE4 and stops at the
 * first that is not negative.
 */
static void test_first_free_channel_is_used(void)
{
    int n;

    for (n = 0; n < 3; n++) {
        channel_base((uint16_t)n)->state = (int16_t)0x8000;
    }

    XNS_IDP_$OS_OPEN(&opt, &st);

    ASSERT_EQ(status_$ok, st, "status");
    ASSERT_EQ(3, opt.flags_channel, "channel 3 was the first free one");
}

/*
 * 0x00E17F5C: with all sixteen taken the bound check finally fires and the
 * cleanup arm runs, this time with a well-defined index of 0x10.
 */
static void test_full_channel_table(void)
{
    int n;

    for (n = 0; n < 17; n++) {
        channel_base((uint16_t)n)->state = (int16_t)0x8000;
    }

    XNS_IDP_$OS_OPEN(&opt, &st);

    ASSERT_EQ(status_$xns_channel_table_full, st, "status");
    ASSERT_EQ(1, excl_start_calls, "the lock was taken");
    ASSERT_EQ(1, excl_stop_calls, "and released");
}

/*
 * 0x00E17F8E "moveq #0x7,D4" + the `dbf' at 0x00E17FA6 is EIGHT passes, and
 * 0x00E17FB0 / 0x00E17FB8 forgive the two "the port is not there" codes once
 * at least one port took the channel.
 */
static void test_bind_to_every_port(void)
{
    opt.flags_channel = XNS_OPEN_FLAG_BIND_LOCAL;
    opt.network = 0xFFFFFFFFu;

    XNS_IDP_$OS_OPEN(&opt, &st);

    ASSERT_EQ(status_$ok, st, "status");
    ASSERT_EQ(XNS_MAX_PORTS, add_port_calls, "eight passes");
    ASSERT_EQ(0, add_port_port[0], "first port");
    ASSERT_EQ(7, add_port_port[7], "last port");
    ASSERT_EQ(0, net_to_port_calls, "-1 skips the lookup");
}

/* 0x00E17FC4-0x00E17FE4: a real network is looked up, and -1 is refused. */
static void test_bind_to_one_port(void)
{
    opt.flags_channel = XNS_OPEN_FLAG_BIND_LOCAL;
    opt.network = 0x0A0B0C0D;
    net_to_port_result = 5;

    XNS_IDP_$OS_OPEN(&opt, &st);

    ASSERT_EQ(status_$ok, st, "status");
    ASSERT_EQ(1, net_to_port_calls, "the network was looked up");
    ASSERT_EQ(1, add_port_calls, "one port bound");
    ASSERT_EQ(5, add_port_port[0], "the port the lookup returned");
}

static void test_bind_to_unknown_network(void)
{
    opt.flags_channel = XNS_OPEN_FLAG_BIND_LOCAL;
    opt.network = 0x0A0B0C0D;
    net_to_port_result = -1;

    XNS_IDP_$OS_OPEN(&opt, &st);

    ASSERT_EQ(status_$xns_listen_network_not_connected, st, "status");
    ASSERT_EQ(0, add_port_calls, "nothing was bound");
    ASSERT_EQ(1, excl_stop_calls, "the lock was released");
}

/* 0x00E1806E: RIP_$FIND_NEXTHOP reporting port -1 is "network unreachable". */
static void test_unreachable_destination(void)
{
    opt.flags_channel = XNS_OPEN_FLAG_CONNECT;
    opt.dest_network = 0x01020304;
    nexthop_port = -1;

    XNS_IDP_$OS_OPEN(&opt, &st);

    ASSERT_EQ(status_$xns_network_unreachable, st, "status");
    ASSERT_EQ(0, arp_calls, "ARP is never reached");
}

/* 0x00E1804E "st -(SP)": RIP_$FIND_NEXTHOP's boolean argument is TRUE, and
 * 0x00E18096 hands ARP the channel's own MAC info block at +0xBC. */
static void test_nexthop_and_arp_arguments(void)
{
    opt.flags_channel = XNS_OPEN_FLAG_CONNECT;
    opt.dest_network = 0x01020304;

    XNS_IDP_$OS_OPEN(&opt, &st);

    ASSERT_EQ(status_$ok, st, "status");
    ASSERT_EQ((uint8_t)true, (uint8_t)nexthop_flags, "the boolean is TRUE");
    ASSERT_EQ(1, arp_calls, "ARP ran");
    ASSERT_EQ(TEST_PORT, arp_port, "on the next hop's port");
    ASSERT_EQ((uintptr_t)channel_base(0)->mac_info,
              (uintptr_t)arp_mac_addr, "into the channel's MAC info block");
}

int main(void)
{
    printf("XNS_IDP_$OS_OPEN (0x00E17F02) tests\n");

    RUN_TEST(test_socket_in_use_releases_a_lock_it_never_took);
    RUN_TEST(test_table_full_exits_before_the_lock);
    RUN_TEST(test_zero_source_takes_the_local_arm);
    RUN_TEST(test_nonzero_source_socket_is_checked);
    RUN_TEST(test_nonzero_destination_alone_is_not_checked);
    RUN_TEST(test_local_source_comes_from_the_port_table);
    RUN_TEST(test_source_port_copies_the_stale_socket);
    RUN_TEST(test_explicit_source_is_copied);
    RUN_TEST(test_allocator_does_not_wrap_at_0xfffe);
    RUN_TEST(test_allocator_wraps_past_0xfffe);
    RUN_TEST(test_allocator_skips_sockets_in_use);
    RUN_TEST(test_channel_slot_is_stamped);
    RUN_TEST(test_first_free_channel_is_used);
    RUN_TEST(test_full_channel_table);
    RUN_TEST(test_bind_to_every_port);
    RUN_TEST(test_bind_to_one_port);
    RUN_TEST(test_bind_to_unknown_network);
    RUN_TEST(test_unreachable_destination);
    RUN_TEST(test_nexthop_and_arp_arguments);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
