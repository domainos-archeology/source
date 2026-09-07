/*
 * Tests for NET_IO_$CREATE_PORT (0x00E5A4A4).
 *
 * The test drives the real body: it supplies the port table, the boot-device
 * block, the ROUTE_$USER_STAT array and stubs for the four routines the
 * function calls, then checks the port record, the socket allocation and the
 * ROUTE_$USER_STAT bookkeeping.
 *
 * The last test is the counterpart of
 * route/test/test_user_stat.c:test_clear_loop_overruns_by_one.  That one pins
 * the arithmetic of the clear loop in isolation; this one runs the loop
 * through NET_IO_$CREATE_PORT itself and shows the byte it destroys in the
 * following record (bead source-2km0).
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include "net_io/net_io_internal.h"

static int tests_run = 0;
static int tests_failed = 0;

#define RUN_TEST(fn)                                                          \
    do {                                                                      \
        tests_run++;                                                          \
        printf("  Running %-34s ", #fn);                                      \
        if (fn() == 0) {                                                      \
            printf("PASSED\n");                                               \
        } else {                                                              \
            tests_failed++;                                                   \
        }                                                                     \
    } while (0)

#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long _e = (long)(expected), _a = (long)(actual);                      \
        if (_e != _a) {                                                       \
            printf("FAILED\n    %s:%d: %s == 0x%lx, expected 0x%lx\n",        \
                   __FILE__, __LINE__, #actual, _a, _e);                      \
            return 1;                                                         \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------------ */
/* Kernel storage the function under test reaches                      */
/* ------------------------------------------------------------------ */

net_io_unwired_t     NET_IO_UNWIRED;
route_$port_t       *ROUTE_$PORTP[ROUTE_$MAX_PORTS];
route_$port_t        ROUTE_$PORT_ARRAY[ROUTE_$MAX_PORTS];
route_$user_stat_t   ROUTE_$USER_STAT[ROUTE_$MAX_USER_STATS];
uint8_t              sock_table_base[SOCK_TABLE_SIZE];
uint32_t             TIME_$CURRENT_CLOCKH;
uint16_t             PROC1_$AS_ID;

/* ------------------------------------------------------------------ */
/* Stubs for the four routines NET_IO_$CREATE_PORT calls               */
/* ------------------------------------------------------------------ */

#define TEST_SOCKET     5
#define TEST_CLOCKH     0x12345678u
#define TEST_ASID       0x0007

static route_$port_t *find_portp_result;
static int            sock_allocate_calls;
static uint32_t       sock_allocate_arg2;
static uint32_t       sock_allocate_arg3;
static int8_t         sock_allocate_result;
static ec_$eventcount_t *ec_init_arg;
static int            set_cleanup_calls;
static uint16_t       set_cleanup_arg;
static sock_$sock_t   test_sock;

route_$port_t *ROUTE_$FIND_PORTP(uint16_t network, int32_t socket)
{
    (void)network;
    (void)socket;
    return find_portp_result;
}

int8_t SOCK_$ALLOCATE(uint16_t *sock_ret, uint32_t proto_bufpages,
                      uint32_t max_queue)
{
    sock_allocate_calls++;
    sock_allocate_arg2 = proto_bufpages;
    sock_allocate_arg3 = max_queue;
    if (sock_allocate_result < 0) {
        *sock_ret = TEST_SOCKET;
    }
    return sock_allocate_result;
}

void EC_$INIT(ec_$eventcount_t *ec)
{
    ec_init_arg = ec;
}

void PROC2_$SET_CLEANUP(uint16_t bit_num)
{
    set_cleanup_calls++;
    set_cleanup_arg = bit_num;
}

#include "../create_port.c"

/* ------------------------------------------------------------------ */

static net_io_$driver_t test_driver;

static void reset_world(void)
{
    int i;

    memset(&NET_IO_UNWIRED, 0, sizeof(NET_IO_UNWIRED));
    NET_IO_UNWIRED.boot_unit = NET_IO_$NO_BOOT_UNIT;
    NET_IO_UNWIRED.boot_port_type = 0;

    memset(ROUTE_$PORT_ARRAY, 0, sizeof(ROUTE_$PORT_ARRAY));
    for (i = 0; i < ROUTE_$MAX_PORTS; i++) {
        ROUTE_$PORTP[i] = &ROUTE_$PORT_ARRAY[i];
    }

    /*
     * route_$port_t.driver_stats and .driver_info are 32-bit target virtual
     * addresses, so point the host VA arena just below ROUTE_$USER_STAT.  The
     * 0x1000 bias keeps record 1 off virtual address zero, which
     * ARCH_VA_TO_PTR maps to nil.
     */
    ARCH_HOST_VA_BASE = (uintptr_t)ROUTE_$USER_STAT - 0x1000;

    memset(ROUTE_$USER_STAT, 0, sizeof(ROUTE_$USER_STAT));
    memset(sock_table_base, 0, sizeof(sock_table_base));
    memset(&test_sock, 0, sizeof(test_sock));
    SOCK_$EVENT_COUNTERS[TEST_SOCKET - 1] = (ec_$eventcount_t *)&test_sock;

    TIME_$CURRENT_CLOCKH = TEST_CLOCKH;
    PROC1_$AS_ID = TEST_ASID;

    find_portp_result = NULL;
    sock_allocate_calls = 0;
    sock_allocate_arg2 = 0;
    sock_allocate_arg3 = 0;
    sock_allocate_result = -1;      /* Domain true: allocation succeeded */
    ec_init_arg = NULL;
    set_cleanup_calls = 0;
    set_cleanup_arg = 0;

    memset(&test_driver, 0, sizeof(test_driver));
}

/*
 * 0x00E5A4CE - 0x00E5A4FC: for a port type that is neither 1 nor 2 the
 * function refuses to build a second port for the same (type, unit).
 */
static int test_duplicate_port_rejected(void)
{
    status_$t status = status_$ok;
    int16_t   index;

    reset_world();
    find_portp_result = &ROUTE_$PORT_ARRAY[3];

    index = NET_IO_$CREATE_PORT(0, 1, &test_driver, 10, &status);

    ASSERT_EQ(-1, index);
    ASSERT_EQ(status_$net_io_illegal_op_for_port_type, status);
    ASSERT_EQ(0, set_cleanup_calls);
    return 0;
}

/*
 * 0x00E5A500 - 0x00E5A524: a hardware port matching the recorded boot device
 * becomes port 0.
 */
static int test_boot_device_port_takes_index_zero(void)
{
    status_$t status = status_$ok;
    int16_t   index;

    reset_world();
    NET_IO_UNWIRED.boot_port_type = 4;
    NET_IO_UNWIRED.boot_unit = 2;
    ROUTE_$PORT_ARRAY[0].active = 1;    /* not consulted on this path */

    index = NET_IO_$CREATE_PORT(4, 2, &test_driver, 10, &status);

    ASSERT_EQ(0, index);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(4, ROUTE_$PORT_ARRAY[0].port_type);
    ASSERT_EQ(2, ROUTE_$PORT_ARRAY[0].socket);
    ASSERT_EQ(1, ROUTE_$PORT_ARRAY[0].active);
    ASSERT_EQ(TEST_CLOCKH, *(uint32_t *)((uint8_t *)&ROUTE_$PORT_ARRAY[0] + 0x50));
    ASSERT_EQ(2, *(uint32_t *)((uint8_t *)&ROUTE_$PORT_ARRAY[0] + 0x4C));
    ASSERT_EQ(0, ROUTE_$PORT_ARRAY[0].forward_count);
    /* port type 0 is neither 1 nor 2, so no cleanup handler is registered */
    ASSERT_EQ(0, set_cleanup_calls);
    return 0;
}

/*
 * 0x00E5A52C - 0x00E5A54E: entries 1..7 are scanned in order, entry 0 is
 * never a scan result.
 */
static int test_scan_picks_first_free_entry(void)
{
    status_$t status = status_$ok;
    int16_t   index;

    reset_world();
    NET_IO_UNWIRED.boot_unit = 1;       /* no wildcard, no match for (7, 9) */
    NET_IO_UNWIRED.boot_port_type = 4;
    ROUTE_$PORT_ARRAY[1].active = 1;
    ROUTE_$PORT_ARRAY[2].active = 1;

    index = NET_IO_$CREATE_PORT(7, 9, &test_driver, 10, &status);

    ASSERT_EQ(3, index);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(7, ROUTE_$PORT_ARRAY[3].port_type);
    ASSERT_EQ(9, ROUTE_$PORT_ARRAY[3].socket);
    return 0;
}

/*
 * 0x00E5A526 / 0x00E5A552: every entry 1..7 busy is "max number of ports
 * already open" and leaves the result at -1.
 */
static int test_no_free_entry(void)
{
    status_$t status = status_$ok;
    int16_t   index;
    int       i;

    reset_world();
    NET_IO_UNWIRED.boot_unit = 1;
    NET_IO_UNWIRED.boot_port_type = 4;
    for (i = 0; i < ROUTE_$MAX_PORTS; i++) {
        ROUTE_$PORT_ARRAY[i].active = 1;
    }

    index = NET_IO_$CREATE_PORT(7, 9, &test_driver, 10, &status);

    ASSERT_EQ(-1, index);
    ASSERT_EQ(status_$net_io_max_ports_open, status);
    return 0;
}

/*
 * 0x00E5A5A8 - 0x00E5A5B8: a type 1 port overwrites its socket word with the
 * port index, records the address space and registers cleanup class 10.
 */
static int test_local_port(void)
{
    status_$t status = status_$ok;
    int16_t   index;

    reset_world();
    ROUTE_$PORT_ARRAY[1].active = 1;

    index = NET_IO_$CREATE_PORT(1, 0x1234, &test_driver, 10, &status);

    ASSERT_EQ(2, index);
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, ROUTE_$PORT_ARRAY[2].port_type);
    ASSERT_EQ(2, ROUTE_$PORT_ARRAY[2].socket);      /* not 0x1234 */
    ASSERT_EQ(TEST_ASID, NET_IO_UNWIRED.port_asid[2]);
    ASSERT_EQ(1, set_cleanup_calls);
    ASSERT_EQ(NET_IO_$CLEANUP_CLASS, set_cleanup_arg);
    ASSERT_EQ(0, sock_allocate_calls);
    return 0;
}

/*
 * 0x00E5A5BC - 0x00E5A688: a type 2 port takes the first free
 * ROUTE_$USER_STAT record, allocates a socket, clears bit 15 of the socket's
 * flags word and initialises the port event count.
 */
static int test_user_port(void)
{
    status_$t status = status_$ok;
    int16_t   index;
    uint8_t  *recs = (uint8_t *)ROUTE_$USER_STAT;

    reset_world();
    test_sock.flags = 0xFFFF;

    index = NET_IO_$CREATE_PORT(2, 0, &test_driver, 0x10, &status);

    ASSERT_EQ(1, index);
    ASSERT_EQ(status_$ok, status);

    /* record 1 was free, so it is the one taken and marked in use */
    ASSERT_EQ((long)(uintptr_t)recs,
              (long)(uintptr_t)ARCH_VA_TO_PTR(ROUTE_$PORT_ARRAY[1].driver_stats));
    ASSERT_EQ(0xFF, recs[0]);

    /* 0x00E5A5F6: the longword at port + 0x34 */
    ASSERT_EQ(0x10, *(uint32_t *)((uint8_t *)&ROUTE_$PORT_ARRAY[1] + 0x34));

    /* 0x00E5A5FA - 0x00E5A60E: the two packed longword arguments */
    ASSERT_EQ(1, sock_allocate_calls);
    ASSERT_EQ(0x00100010u, sock_allocate_arg2);
    ASSERT_EQ(0x00100400u, sock_allocate_arg3);
    ASSERT_EQ(TEST_SOCKET, ROUTE_$PORT_ARRAY[1].socket);

    /* 0x00E5A63C: bclr.b #7 on the byte at 0x16 is bit 15 of the word */
    ASSERT_EQ(0x7FFF, test_sock.flags);

    ASSERT_EQ((long)(uintptr_t)&ROUTE_$PORT_ARRAY[1].port_ec[0],
              (long)(uintptr_t)ec_init_arg);
    ASSERT_EQ(TEST_ASID, NET_IO_UNWIRED.port_asid[1]);
    ASSERT_EQ(1, set_cleanup_calls);
    return 0;
}

/*
 * 0x00E5A616 - 0x00E5A61A: SOCK_$ALLOCATE returning Domain false.
 */
static int test_user_port_socket_allocation_fails(void)
{
    status_$t status = status_$ok;
    int16_t   index;

    reset_world();
    sock_allocate_result = 0;

    index = NET_IO_$CREATE_PORT(2, 0, &test_driver, 0x10, &status);

    ASSERT_EQ(-1, index);
    ASSERT_EQ(status_$net_io_no_user_buffer_queues, status);
    ASSERT_EQ(0, set_cleanup_calls);
    return 0;
}

/*
 * 0x00E5A5BC / 0x00E5A5EA: all four records already in use.
 */
static int test_user_port_no_free_stat_record(void)
{
    status_$t status = status_$ok;
    int16_t   index;
    uint8_t  *recs = (uint8_t *)ROUTE_$USER_STAT;
    int       n;

    reset_world();
    for (n = 0; n < ROUTE_$MAX_USER_STATS; n++) {
        recs[n * 0x90] = 0xFF;
    }

    index = NET_IO_$CREATE_PORT(2, 0, &test_driver, 0x10, &status);

    ASSERT_EQ(-1, index);
    ASSERT_EQ(status_$net_io_max_user_ports_open, status);
    ASSERT_EQ(0, sock_allocate_calls);
    return 0;
}

/*
 * ORIGINAL DEFECT (bead source-2km0): the clear loop at
 * 0x00E5A66C - 0x00E5A67E runs 0x91 times over a 0x90-byte record, so it
 * clears the first byte of the record that follows.  Take record 3 (records 1
 * and 2 already in use) and watch record 4's in-use byte go from 0xFF to 0.
 * In the image record 4's neighbour is ROUTE_$PID (0xE88216) instead.
 */
static int test_clear_loop_overruns_into_next_record(void)
{
    status_$t status = status_$ok;
    int16_t   index;
    uint8_t  *recs = (uint8_t *)ROUTE_$USER_STAT;

    reset_world();
    recs[0 * 0x90] = 0xFF;
    recs[1 * 0x90] = 0xFF;
    recs[3 * 0x90] = 0xFF;              /* record 4: in use, must stay so */
    memset(recs + 2 * 0x90, 0xAA, 0x90);
    recs[2 * 0x90] = 0x00;              /* record 3: free */

    index = NET_IO_$CREATE_PORT(2, 0, &test_driver, 0x10, &status);

    ASSERT_EQ(1, index);
    ASSERT_EQ(status_$ok, status);

    /* record 3 is the one allocated: cleared, then marked in use */
    ASSERT_EQ((long)(uintptr_t)(recs + 2 * 0x90),
              (long)(uintptr_t)ARCH_VA_TO_PTR(ROUTE_$PORT_ARRAY[1].driver_stats));
    ASSERT_EQ(0xFF, recs[2 * 0x90]);
    ASSERT_EQ(0x00, recs[2 * 0x90 + 0x8F]);

    /* the overrun: record 4's in-use byte was 0xFF and is now zero */
    ASSERT_EQ(0x00, recs[3 * 0x90]);
    return 0;
}

int main(void)
{
    printf("NET_IO_$CREATE_PORT tests\n");
    RUN_TEST(test_duplicate_port_rejected);
    RUN_TEST(test_boot_device_port_takes_index_zero);
    RUN_TEST(test_scan_picks_first_free_entry);
    RUN_TEST(test_no_free_entry);
    RUN_TEST(test_local_port);
    RUN_TEST(test_user_port);
    RUN_TEST(test_user_port_socket_allocation_fails);
    RUN_TEST(test_user_port_no_free_stat_record);
    RUN_TEST(test_clear_loop_overruns_into_next_record);
    printf("\n%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
