/*
 * ring/test/test_open_internal.c - ring_$open_internal (0x00E76B7C) through
 * RING_$SVC_OPEN (0x00E76DF2) and RING_$OPEN_OS (0x00E77BA0), and
 * ring_$copy_to_user (0x00E77382)
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

#include "ring/ring_internal.h"
#include "sock/sock.h"
#include "proc1/proc1.h"

MODULE_DATA_DEFINE(ring_global_t, RING_$CTL, 0x00E86400);
MODULE_DATA_DEFINE(sock_$data_t, SOCK_$DATA, 0x00E27510);
uint16_t PROC1_$AS_ID;

static char trace[64];
static int ntrace;

void ML_$EXCLUSION_START(ml_$exclusion_t *e)
{
    trace[ntrace++] = (e == &RING_$CTL.units[1].rx_exclusion ||
                       e == &RING_$CTL.units[0].rx_exclusion) ? 'R' : 'T';
}
void ML_$EXCLUSION_STOP(ml_$exclusion_t *e)
{
    trace[ntrace++] = (e == &RING_$CTL.units[1].rx_exclusion ||
                       e == &RING_$CTL.units[0].rx_exclusion) ? 'r' : 't';
}
void ring_$disable_interrupts(void) { trace[ntrace++] = 'D'; }
static status_$t do_start_status;
void ring_$do_start(uint16_t unit, ring_unit_t *u, status_$t *st)
{
    (void)unit; (void)u;
    trace[ntrace++] = 'S';
    *st = do_start_status;
}
static uint16_t hw_mask;
void ring_$set_hw_mask(uint16_t unit, uint16_t mask)
{
    (void)unit;
    trace[ntrace++] = 'M';
    hw_mask = mask;
}
static int8_t alloc_ok;
static uint16_t alloc_args[4];
int8_t SOCK_$ALLOCATE_USER(uint16_t *sock_ret, uint16_t a, uint16_t b,
                           uint16_t c, uint16_t d)
{
    alloc_args[0] = a; alloc_args[1] = b; alloc_args[2] = c; alloc_args[3] = d;
    *sock_ret = 7;
    return alloc_ok;
}
static ec_$eventcount_t *reg_ec;
void *EC2_$REGISTER_EC1(ec_$eventcount_t *ec, status_$t *st)
{
    reg_ec = ec;
    (void)st;
    return (void *)(uintptr_t)0x00ABCDEF;
}
static uint32_t copy_src[8], copy_dst[8], copy_len[8];
static int ncopy;
void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    copy_src[ncopy] = (uint32_t)(uintptr_t)src;
    copy_dst[ncopy] = (uint32_t)(uintptr_t)dst;
    copy_len[ncopy] = len;
    ncopy++;
}

#include "../pkt_type_overlaps.c"
#include "../open_internal.c"
#include "../svc_open.c"
#include "../open_os.c"
#include "../copy_to_user.c"

static ring_unit_t *u;
static sock_$sock_t sock7, sock_os;

static void setup(void)
{
    memset(&RING_$CTL, 0, sizeof(RING_$CTL));
    memset(&SOCK_$DATA, 0, sizeof(SOCK_$DATA));
    memset(trace, 0, sizeof(trace));
    ntrace = 0;
    u = &RING_$CTL.units[1];
    u->state_flags = RING_UNIT_STARTED;
    u->tmask = 1;
    PROC1_$AS_ID = 9;
    sock7.flags = 0x8007;
    SOCK_$DATA.socket_ptr[7] = &sock7;
    SOCK_$DATA.socket_ptr[RING_OS_SOCKET_ID] = &sock_os;
    alloc_ok = -1;
    do_start_status = 0;
    reg_ec = NULL;
    ncopy = 0;
}

static ring_$open_args_t req(uint16_t n)
{
    ring_$open_args_t a;
    memset(&a, 0, sizeof(a));
    a.hdr.in.version = 1;
    a.hdr.in.count = n;
    a.body.types[0] = (ring_$pkt_range_t){ 0x100, 0x1FF };
    a.body.types[1] = (ring_$pkt_range_t){ 0x300, 0x300 };
    return a;
}

TEST(user_open_takes_first_free_channel)
{
    uint16_t unit = 1;
    status_$t st = 1;
    ring_$open_args_t a;
    setup();
    RING_UNIT_CHANNEL(u, 1).flags = -1;
    u->open_count = 2;
    u->pkt_type_cnt = 1;
    RING_UNIT_PKT_TYPE(u, 1) = (ring_pkt_type_t){ 0x10, 0x20, 1, 0 };
    a = req(2);
    RING_$SVC_OPEN(&unit, &a, 0, 0, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, strcmp(trace, "DTtRr"));
    ASSERT_EQ(3, u->open_count);
    ASSERT_EQ(3, u->pkt_type_cnt);
    ASSERT_EQ(0x100, RING_UNIT_PKT_TYPE(u, 2).low);
    ASSERT_EQ(0x1FF, RING_UNIT_PKT_TYPE(u, 2).high);
    ASSERT_EQ(2, RING_UNIT_PKT_TYPE(u, 2).channel);
    ASSERT_EQ(0x300, RING_UNIT_PKT_TYPE(u, 3).low);
    ASSERT_EQ(0xFF, (uint8_t)RING_UNIT_CHANNEL(u, 2).flags);
    ASSERT_EQ(9, RING_UNIT_CHANNEL(u, 2).asid);
    ASSERT_EQ(7, RING_UNIT_CHANNEL(u, 2).socket_id);
    ASSERT_EQ(1, RING_UNIT_CHANNEL(u, 2).open_version);
    ASSERT_EQ(0x0007, sock7.flags);             /* bit 15 cleared */
    ASSERT_EQ(8, alloc_args[0]);
    ASSERT_EQ(0x400, alloc_args[3]);
    ASSERT_EQ(2, a.body.channel);
    ASSERT_EQ(0x00ABCDEF, a.hdr.ec2_handle);
    ASSERT_EQ((uintptr_t)&sock7.ec, (uintptr_t)reg_ec);
}

TEST(first_open_starts_unit_and_sets_mask)
{
    uint16_t unit = 1;
    status_$t st = 1;
    ring_$open_args_t a;
    setup();
    u->state_flags = 0;
    u->tmask = 0x0200;
    a = req(0);
    RING_$SVC_OPEN(&unit, &a, 0, 0, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, strcmp(trace, "DTSMtRr"));
    ASSERT_EQ(0x0281, hw_mask);
    ASSERT_EQ(1, u->open_count);
    ASSERT_EQ(1, a.body.channel);
}

TEST(start_failure_returns_before_channel_table)
{
    uint16_t unit = 0;
    status_$t st = 0;
    ring_$open_args_t a;
    setup();
    u = &RING_$CTL.units[0];
    do_start_status = status_$ring_controller_hardware_error;
    u->tmask = 1;
    a = req(1);
    RING_$SVC_OPEN(&unit, &a, 0, 0, &st);
    ASSERT_EQ(status_$ring_controller_hardware_error, st);
    ASSERT_EQ(0, strcmp(trace, "DTSt"));
    ASSERT_EQ(1, u->open_count);                /* not undone */
}

TEST(bad_unit_and_version)
{
    uint16_t unit = 2;
    status_$t st = 0;
    ring_$open_args_t a;
    setup();
    a = req(1);
    RING_$SVC_OPEN(&unit, &a, 0, 0, &st);
    ASSERT_EQ(status_$ring_invalid_unit_num, st);
    unit = 1;
    a.hdr.in.version = 2;
    RING_$SVC_OPEN(&unit, &a, 0, 0, &st);
    ASSERT_EQ(status_$ring_driver_version_mismatch, st);
    ASSERT_EQ(0, ntrace);
}

TEST(no_free_channel)
{
    uint16_t unit = 1, i;
    status_$t st = 0;
    ring_$open_args_t a;
    setup();
    for (i = 1; i <= 10; i++) RING_UNIT_CHANNEL(u, i).flags = -1;
    u->open_count = 4;
    a = req(1);
    RING_$SVC_OPEN(&unit, &a, 0, 0, &st);
    ASSERT_EQ(status_$ring_no_channels, st);
    ASSERT_EQ(0, strcmp(trace, "DTtRrTt"));
    ASSERT_EQ(4, u->open_count);
}

TEST(too_many_types_and_bad_range_and_overlap)
{
    uint16_t unit = 1;
    status_$t st = 0;
    ring_$open_args_t a;
    setup();
    u->pkt_type_cnt = 0x1F;
    a = req(2);
    RING_$SVC_OPEN(&unit, &a, 0, 0, &st);
    ASSERT_EQ(status_$ring_no_channels, st);

    setup();
    a = req(2);
    a.body.types[1] = (ring_$pkt_range_t){ 0x400, 0x3FF };
    RING_$SVC_OPEN(&unit, &a, 0, 0, &st);
    ASSERT_EQ(status_$ring_invalid_svc_packet_type, st);
    /* the first range was already written, the count was not advanced */
    ASSERT_EQ(0x100, RING_UNIT_PKT_TYPE(u, 1).low);
    ASSERT_EQ(0, u->pkt_type_cnt);
    ASSERT_EQ(0, (uint8_t)RING_UNIT_CHANNEL(u, 1).flags);

    setup();
    u->pkt_type_cnt = 1;
    RING_UNIT_PKT_TYPE(u, 1) = (ring_pkt_type_t){ 0x1F0, 0x200, 3, 0 };
    a = req(1);
    RING_$SVC_OPEN(&unit, &a, 0, 0, &st);
    ASSERT_EQ(status_$ring_pkt_type_in_use, st);
}

TEST(socket_allocation_failure)
{
    uint16_t unit = 1;
    status_$t st = 0;
    ring_$open_args_t a;
    setup();
    alloc_ok = 0;
    u->open_count = 1;
    a = req(1);
    RING_$SVC_OPEN(&unit, &a, 0, 0, &st);
    ASSERT_EQ(status_$ring_no_channels, st);
    ASSERT_EQ(1, u->open_count);
    ASSERT_EQ(0, u->pkt_type_cnt);
}

TEST(open_os_uses_os_socket_and_copies_back)
{
    status_$t st = 1;
    ring_$open_os_args_t a;
    setup();
    memset(&a, 0, sizeof(a));
    a.u.types[0] = (ring_$pkt_range_t){ 0x500, 0x5FF };
    a.u.in.count = 1;
    RING_$OPEN_OS(1, &a, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(RING_OS_SOCKET_ID, RING_UNIT_CHANNEL(u, 1).socket_id);
    ASSERT_EQ(1, a.u.out.channel);
    ASSERT_EQ((uintptr_t)&sock_os.ec, (uintptr_t)reg_ec);
    ASSERT_EQ(0x500, RING_UNIT_PKT_TYPE(u, 1).low);
}

TEST(open_os_rejects_more_than_16)
{
    status_$t st = 0;
    ring_$open_os_args_t a;
    setup();
    memset(&a, 0, sizeof(a));
    a.u.in.count = 0x11;
    RING_$OPEN_OS(1, &a, &st);
    ASSERT_EQ(status_$ring_invalid_svc_packet_type, st);
    ASSERT_EQ(0, ntrace);
}

/* ------------------------------------------------- ring_$copy_to_user */

TEST(copy_spans_elements_and_keeps_offset_quirk)
{
    ring_$iov_t iov[3] = { { 0x1000, 10, 0 }, { 0x2000, 4, 0 }, { 0x3000, 100, 0 } };
    uint32_t src = 0x9000;
    int16_t idx = 1, off = 6;
    setup();
    ring_$copy_to_user(&src, 12, iov, 3, &idx, &off);
    ASSERT_EQ(3, ncopy);
    ASSERT_EQ(0x9000, copy_src[0]); ASSERT_EQ(0x1006, copy_dst[0]); ASSERT_EQ(4, copy_len[0]);
    ASSERT_EQ(0x9004, copy_src[1]); ASSERT_EQ(0x2000, copy_dst[1]); ASSERT_EQ(4, copy_len[1]);
    ASSERT_EQ(0x9008, copy_src[2]); ASSERT_EQ(0x3000, copy_dst[2]); ASSERT_EQ(4, copy_len[2]);
    ASSERT_EQ(3, idx);
    ASSERT_EQ(4, off);
    ASSERT_EQ(0x9000, src);                     /* read once, not updated */

    /* resumes at offset n (4), not at the end of what it wrote */
    ncopy = 0;
    ring_$copy_to_user(&src, 2, iov, 3, &idx, &off);
    ASSERT_EQ(0x3004, copy_dst[0]);
    ASSERT_EQ(2, off);
}

TEST(copy_stops_when_iov_exhausted)
{
    ring_$iov_t iov[1] = { { 0x1000, 4, 0 } };
    uint32_t src = 0x9000;
    int16_t idx = 1, off = 0;
    setup();
    ring_$copy_to_user(&src, 10, iov, 1, &idx, &off);
    ASSERT_EQ(1, ncopy);
    ASSERT_EQ(2, idx);
    ASSERT_EQ(0, off);
    ncopy = 0;
    ring_$copy_to_user(&src, 0, iov, 1, &idx, &off);
    ASSERT_EQ(0, ncopy);
}

int main(void)
{
    printf("ring_$open_internal / RING_$SVC_OPEN / RING_$OPEN_OS / ring_$copy_to_user\n");
    RUN_TEST(user_open_takes_first_free_channel);
    RUN_TEST(first_open_starts_unit_and_sets_mask);
    RUN_TEST(start_failure_returns_before_channel_table);
    RUN_TEST(bad_unit_and_version);
    RUN_TEST(no_free_channel);
    RUN_TEST(too_many_types_and_bad_range_and_overlap);
    RUN_TEST(socket_allocation_failure);
    RUN_TEST(open_os_uses_os_socket_and_copies_back);
    RUN_TEST(open_os_rejects_more_than_16);
    RUN_TEST(copy_spans_elements_and_keeps_offset_quirk);
    RUN_TEST(copy_stops_when_iov_exhausted);
    printf("%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed ? 1 : 0;
}
