/*
 * net/test/test_net_dispatch.c - Unit tests for the NET_$ dispatchers.
 *
 * The five dispatchers all funnel through NET_$FIND_HANDLER and then call one
 * procedure-variable slot of the port's net_io_$driver_t record.  The tests
 * below compile the real sources and script the slots, so the argument lists
 * the image pushes can be checked directly:
 *
 *   - NET_$OPEN   0x00E5A1E0-0x00E5A1F4: five arguments (port, param3,
 *                 *param4 as one word, param5, status) and a discarded word
 *                 result slot; a NON-pointer word is passed, not a shift of
 *                 param5 or of status_ret
 *   - NET_$CLOSE  0x00E5A250-0x00E5A264 and NET_$IOCTL 0x00E5A2AC-0x00E5A2C0:
 *                 the same five arguments, not the three the earlier C used
 *   - NET_$SEND   0x00E5A370-0x00E5A390 and NET_$RCV 0x00E5A308-0x00E5A328:
 *                 eight arguments with *param4 and *param7 as words
 *   - NET_$FIND_HANDLER 0x00E5A128: the slot offset indexes the record
 *                 route_$port_t.driver_info points at, and a nil slot means
 *                 status_$network_operation_not_defined_on_hardware while an
 *                 absent port means status_$internet_unknown_network_port
 *   - NET_$OPEN registers cleanup bit 10 only after a zero status
 *                 (0x00E5A1FA / 0x00E5A200)
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ==========================================================================
 * Test framework
 * ========================================================================== */

static int tests_failed = 0;
static int tests_run = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  Running %s... ", #name);                                    \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("done\n");                                                     \
    } while (0)

#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long _e = (long long)(expected);                                 \
        long long _a = (long long)(actual);                                   \
        if (_e != _a) {                                                       \
            printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",  \
                   (unsigned long long)_e, (unsigned long long)_a, __LINE__); \
            tests_failed++;                                                   \
            return;                                                           \
        }                                                                     \
    } while (0)

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("FAILED\n    %s at line %d\n", #cond, __LINE__);           \
            tests_failed++;                                                   \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ==========================================================================
 * Globals and mocks the code under test links against
 * ========================================================================== */

#include "net/net_internal.h"

/* --- the port ROUTE_$FIND_PORTP hands back ------------------------------ */

static route_$port_t test_port;
static net_io_$driver_t test_driver;

static int find_portp_calls;
static uint16_t find_portp_network;
static int32_t find_portp_socket;
static route_$port_t *find_portp_result;

route_$port_t *ROUTE_$FIND_PORTP(uint16_t network, int32_t socket)
{
    find_portp_calls++;
    find_portp_network = network;
    find_portp_socket = socket;
    return find_portp_result;
}

static int set_cleanup_calls;
static uint16_t set_cleanup_bit;

void PROC2_$SET_CLEANUP(uint16_t bit_num)
{
    set_cleanup_calls++;
    set_cleanup_bit = bit_num;
}

/* --- what the scripted driver entries saw ------------------------------- */

static int ctl_calls;
static int16_t *ctl_port;
static uint32_t ctl_param3;
static int16_t ctl_param4;
static uint32_t ctl_param5;
static status_$t *ctl_status;
static status_$t ctl_status_to_set;

static int16_t ctl_entry(int16_t *port, uint32_t param3, int16_t param4,
                         uint32_t param5, status_$t *status_ret)
{
    ctl_calls++;
    ctl_port = port;
    ctl_param3 = param3;
    ctl_param4 = param4;
    ctl_param5 = param5;
    ctl_status = status_ret;
    *status_ret = ctl_status_to_set;
    return 0x1234;      /* the image discards the word result slot */
}

static int xfer_calls;
static int16_t *xfer_port;
static uint32_t xfer_param3;
static int16_t xfer_param4;
static uint32_t xfer_param5;
static uint32_t xfer_param6;
static int16_t xfer_param7;
static uint32_t xfer_param8;
static status_$t *xfer_status;

static void xfer_entry(int16_t *port, uint32_t param3, int16_t param4,
                       uint32_t param5, uint32_t param6, int16_t param7,
                       uint32_t param8, status_$t *status_ret)
{
    xfer_calls++;
    xfer_port = port;
    xfer_param3 = param3;
    xfer_param4 = param4;
    xfer_param5 = param5;
    xfer_param6 = param6;
    xfer_param7 = param7;
    xfer_param8 = param8;
    xfer_status = status_ret;
}

/* ==========================================================================
 * Code under test
 * ========================================================================== */

#include "../find_handler.c"
#include "../open.c"
#include "../close.c"
#include "../ioctl.c"
#include "../send.c"
#include "../rcv.c"
#include "../get_info.c"

/* ==========================================================================
 * Fixture
 * ========================================================================== */

static void reset(void)
{
    memset(&test_port, 0, sizeof(test_port));
    memset(&test_driver, 0, sizeof(test_driver));
    /*
     * driver_info is a 32-bit target virtual address; a host pointer does not
     * fit, so point the host arena base just below the driver record and put
     * the resulting non-zero offset in the field (see arch/host/arch.h).
     */
    ARCH_HOST_VA_BASE = (uintptr_t)&test_driver - 0x1000u;
    test_port.driver_info = ARCH_PTR_TO_VA(&test_driver);
    find_portp_result = &test_port;
    find_portp_calls = 0;
    set_cleanup_calls = 0;
    set_cleanup_bit = 0;
    ctl_calls = 0;
    ctl_status_to_set = status_$ok;
    xfer_calls = 0;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * The slot offsets name the five svc_ slots of net_io_$driver_t.  On the
 * target they are 0x28/0x2C/0x30/0x34/0x38, which net/net.h asserts against
 * the differences the image computes; here the record carries host-width
 * procedure variables, so the check is that each constant still selects its
 * own field and that the five are distinct and in the image's order.
 */
TEST(handler_offsets_name_the_svc_slots)
{
    ASSERT_EQ(offsetof(net_io_$driver_t, svc_open),  NET_HANDLER_OFF_OPEN);
    ASSERT_EQ(offsetof(net_io_$driver_t, svc_close), NET_HANDLER_OFF_CLOSE);
    ASSERT_EQ(offsetof(net_io_$driver_t, svc_ioctl), NET_HANDLER_OFF_IOCTL);
    ASSERT_EQ(offsetof(net_io_$driver_t, svc_write), NET_HANDLER_OFF_SEND);
    ASSERT_EQ(offsetof(net_io_$driver_t, svc_read),  NET_HANDLER_OFF_RCV);
    ASSERT_TRUE(NET_HANDLER_OFF_OPEN  < NET_HANDLER_OFF_CLOSE);
    ASSERT_TRUE(NET_HANDLER_OFF_CLOSE < NET_HANDLER_OFF_IOCTL);
    ASSERT_TRUE(NET_HANDLER_OFF_IOCTL < NET_HANDLER_OFF_SEND);
    ASSERT_TRUE(NET_HANDLER_OFF_SEND  < NET_HANDLER_OFF_RCV);
}

TEST(find_handler_no_port)
{
    status_$t st = 0;

    reset();
    find_portp_result = NULL;

    ASSERT_TRUE(NET_$FIND_HANDLER(3, 7, NET_HANDLER_OFF_OPEN, &st) == NULL);
    ASSERT_EQ(0x002B0003, st);              /* unknown network port */
    ASSERT_EQ(1, find_portp_calls);
    ASSERT_EQ(3, find_portp_network);
    ASSERT_EQ(7, find_portp_socket);        /* widened to a longword */
}

TEST(find_handler_nil_slot)
{
    status_$t st = 0;

    reset();
    test_driver.svc_open = NULL;

    ASSERT_TRUE(NET_$FIND_HANDLER(0, 0, NET_HANDLER_OFF_OPEN, &st) == NULL);
    ASSERT_EQ(0x0011001D, st);              /* not defined on hardware */
}

TEST(find_handler_picks_the_named_slot)
{
    status_$t st = 0xdead;

    reset();
    test_driver.svc_read = (net_io_$driver_fn_t)xfer_entry;

    ASSERT_TRUE(NET_$FIND_HANDLER(0, 0, NET_HANDLER_OFF_RCV, &st)
                == (net_io_$driver_fn_t)xfer_entry);
    ASSERT_EQ(0, st);
}

/*
 * NET_$OPEN passes *param4 as a word, param5 as a longword and status_ret by
 * reference.  The earlier C passed (int16_t)(param5 >> 16) and
 * (int16_t)((uintptr_t)status_ret >> 16) and dropped status_ret entirely,
 * which this pins down.
 */
TEST(open_five_arguments)
{
    status_$t st = 0;
    int16_t net_id = 2;
    int16_t port = 5;
    int16_t p4 = (int16_t)0xBEEF;

    reset();
    test_driver.svc_open = (net_io_$driver_fn_t)ctl_entry;

    NET_$OPEN(&net_id, &port, 0x11223344u, &p4, 0x55667788u, &st);

    ASSERT_EQ(1, ctl_calls);
    ASSERT_TRUE(ctl_port == &port);
    ASSERT_EQ(0x11223344u, ctl_param3);
    ASSERT_EQ((int16_t)0xBEEF, ctl_param4);
    ASSERT_EQ(0x55667788u, ctl_param5);
    ASSERT_TRUE(ctl_status == &st);
    ASSERT_EQ(0, st);
    /* 0x00E5A200 move.w #0xa,-(SP) */
    ASSERT_EQ(1, set_cleanup_calls);
    ASSERT_EQ(10, set_cleanup_bit);
}

TEST(open_skips_cleanup_when_the_driver_fails)
{
    status_$t st = 0;
    int16_t net_id = 0;
    int16_t port = 0;
    int16_t p4 = 0;

    reset();
    test_driver.svc_open = (net_io_$driver_fn_t)ctl_entry;
    ctl_status_to_set = 0x00110004;

    NET_$OPEN(&net_id, &port, 0, &p4, 0, &st);

    ASSERT_EQ(1, ctl_calls);
    ASSERT_EQ(0x00110004, st);
    ASSERT_EQ(0, set_cleanup_calls);        /* 0x00E5A1FA bne */
}

TEST(open_skips_the_driver_when_the_lookup_fails)
{
    status_$t st = 0;
    int16_t net_id = 0;
    int16_t port = 0;
    int16_t p4 = 0;

    reset();
    find_portp_result = NULL;

    NET_$OPEN(&net_id, &port, 0, &p4, 0, &st);

    ASSERT_EQ(0, ctl_calls);                /* 0x00E5A1DC bne */
    ASSERT_EQ(0, set_cleanup_calls);
    ASSERT_EQ(0x002B0003, st);
}

/* CLOSE and IOCTL push the same five arguments OPEN does. */
TEST(close_five_arguments)
{
    status_$t st = 0;
    int16_t net_id = 1;
    int16_t port = 4;
    int16_t p4 = 0x0102;

    reset();
    test_driver.svc_close = (net_io_$driver_fn_t)ctl_entry;

    NET_$CLOSE(&net_id, &port, 0xAABBCCDDu, &p4, 0x01020304u, &st);

    ASSERT_EQ(1, ctl_calls);
    ASSERT_TRUE(ctl_port == &port);
    ASSERT_EQ(0xAABBCCDDu, ctl_param3);
    ASSERT_EQ(0x0102, ctl_param4);
    ASSERT_EQ(0x01020304u, ctl_param5);
    ASSERT_TRUE(ctl_status == &st);
    ASSERT_EQ(0, set_cleanup_calls);        /* CLOSE registers no cleanup */
}

TEST(ioctl_five_arguments)
{
    status_$t st = 0;
    int16_t net_id = 1;
    int16_t port = 4;
    int16_t p4 = 0x7FFF;

    reset();
    test_driver.svc_ioctl = (net_io_$driver_fn_t)ctl_entry;

    NET_$IOCTL(&net_id, &port, 9, &p4, 0x0BADF00Du, &st);

    ASSERT_EQ(1, ctl_calls);
    ASSERT_EQ(9u, ctl_param3);
    ASSERT_EQ(0x7FFF, ctl_param4);
    ASSERT_EQ(0x0BADF00Du, ctl_param5);
}

TEST(send_eight_arguments)
{
    status_$t st = 0;
    int16_t net_id = 6;
    int16_t port = 2;
    int16_t p4 = 0x0011;
    int16_t p7 = 0x0022;

    reset();
    test_driver.svc_write = (net_io_$driver_fn_t)xfer_entry;

    NET_$SEND(&net_id, &port, 0x11111111u, &p4, 0x22222222u, 0x33333333u,
              &p7, 0x44444444u, &st);

    ASSERT_EQ(1, xfer_calls);
    ASSERT_TRUE(xfer_port == &port);
    ASSERT_EQ(0x11111111u, xfer_param3);
    ASSERT_EQ(0x0011, xfer_param4);
    ASSERT_EQ(0x22222222u, xfer_param5);
    ASSERT_EQ(0x33333333u, xfer_param6);
    ASSERT_EQ(0x0022, xfer_param7);
    ASSERT_EQ(0x44444444u, xfer_param8);
    ASSERT_TRUE(xfer_status == &st);
}

TEST(rcv_eight_arguments)
{
    status_$t st = 0;
    int16_t net_id = 6;
    int16_t port = 2;
    int16_t p4 = -1;
    int16_t p7 = -2;

    reset();
    test_driver.svc_read = (net_io_$driver_fn_t)xfer_entry;

    NET_$RCV(&net_id, &port, 1, &p4, 2, 3, &p7, 4, &st);

    ASSERT_EQ(1, xfer_calls);
    ASSERT_EQ(-1, xfer_param4);
    ASSERT_EQ(-2, xfer_param7);
    ASSERT_EQ(4u, xfer_param8);
}

TEST(get_info_is_unimplemented)
{
    status_$t st = 0;

    reset();
    NET_$GET_INFO(NULL, NULL, NULL, NULL, &st);
    ASSERT_EQ(0x0011001D, st);
}

int main(void)
{
    printf("NET_$ dispatcher tests\n");
    RUN_TEST(handler_offsets_name_the_svc_slots);
    RUN_TEST(find_handler_no_port);
    RUN_TEST(find_handler_nil_slot);
    RUN_TEST(find_handler_picks_the_named_slot);
    RUN_TEST(open_five_arguments);
    RUN_TEST(open_skips_cleanup_when_the_driver_fails);
    RUN_TEST(open_skips_the_driver_when_the_lookup_fails);
    RUN_TEST(close_five_arguments);
    RUN_TEST(ioctl_five_arguments);
    RUN_TEST(send_eight_arguments);
    RUN_TEST(rcv_eight_arguments);
    RUN_TEST(get_info_is_unimplemented);

    printf("\n%d test(s) run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
