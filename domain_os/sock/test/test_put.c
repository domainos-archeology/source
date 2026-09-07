/*
 * sock/test/test_put.c - Unit tests for the SOCK_$PUT family
 * (0x00E1614E / 0x00E16190 / 0x00E161F8).
 *
 * Compiles the real sock/put.c.  The point of the tests is the argument
 * shape recovered for bead source-bpz8: the packet record travels by value
 * through all three levels (SOCK_$PUT `move.l (0xa,A6),-(SP)` at 0x00E16164,
 * SOCK_$PUT_INT `pea (A2)` at 0x00E161E4, SOCK_$PUT_INT_INT
 * `movea.l (0xc,A6),A2` at 0x00E16206), so there is exactly one level of
 * indirection, and the only place a second dereference happens is
 * 0x00E161D0 `movea.l (A2),A0` - which reads the record's `hdr` field.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %s... ", #name);                                        \
    test_##name();                                                            \
    tests_passed++;                                                           \
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

#include "sock/sock_internal.h"

/* Globals the code under test links against. */
uint8_t sock_table_base[SOCK_TABLE_SIZE];

/* Mocked callees. */
static int spin_locks;
static int spin_unlocks;

ml_$spin_token_t ML_$SPIN_LOCK(void *lockp)
{
    (void)lockp;
    spin_locks++;
    return 0x00AB;
}

void ML_$SPIN_UNLOCK(void *lockp, ml_$spin_token_t token)
{
    (void)lockp;
    spin_unlocks++;
    if (token != 0x00AB) {
        printf("BAD TOKEN ");
        tests_failed++;
    }
}

static int ec_advances;
static ec_$eventcount_t *ec_advanced;

void EC_$ADVANCE(ec_$eventcount_t *ec)
{
    ec_advances++;
    ec_advanced = ec;
}

#include "../put.c"

/*
 * The netbuf page and the header buffer.  put.c masks the header VA down to
 * the 1KB page (0x00E16268 `andi.w #-0x400,D6w`), so the header must live
 * inside the page.  The arena starts one page below the array so that no VA
 * handed out is zero.
 */
#define ARENA_PAGES 4
static uint8_t arena[ARENA_PAGES * 1024] __attribute__((aligned(1024)));
#define NETBUF_VA   2048u
#define HDR_VA      (NETBUF_VA + 0x100u)

static uint8_t *netbuf(void)   { return arena + NETBUF_VA; }
static uint8_t *hdr_buf(void)  { return arena + HDR_VA; }

#define TEST_SOCK 0x21
static sock_$sock_t sock_desc;
static sock_$pkt_info_t pkt;

static void setup(void)
{
    memset(arena, 0, sizeof(arena));
    memset(sock_table_base, 0, sizeof(sock_table_base));
    memset(&sock_desc, 0, sizeof(sock_desc));
    memset(&pkt, 0, sizeof(pkt));
    ARCH_HOST_VA_BASE = (uintptr_t)arena;

    SOCK_GET_VIEW_PTR(TEST_SOCK) = &sock_desc;

    sock_desc.flags = SOCK_FLAG_ALLOCATED | SOCK_FLAG_OPEN | TEST_SOCK;
    sock_desc.max_queue = 4;
    sock_desc.queue_count = 0;
    sock_desc.max_data_len = 0x400;

    pkt.hdr = HDR_VA;
    pkt.src_addr = 0x11223344;
    pkt.src_port = 0x5566;
    pkt.dst_addr = 0x778899AA;
    pkt.flags = 0x0002;
    pkt.n_hops = 0;
    pkt.data_len = 0x100;
    pkt.hdr_len = 0x30;
    pkt.data_pages[0] = 0x1000;
    pkt.data_pages[1] = 0x2000;
    pkt.data_pages[2] = 0x3000;
    pkt.data_pages[3] = 0x4000;

    spin_locks = spin_unlocks = ec_advances = 0;
    ec_advanced = NULL;
}

/* The record is copied field by field into the netbuf header area. */
TEST(put_int_int_fills_the_netbuf_header)
{
    int16_t r;

    setup();
    r = SOCK_$PUT_INT_INT(&sock_desc, &pkt, 0, 0x0077, 0x0088);

    ASSERT_EQ(0, r);
    ASSERT_EQ(1, spin_locks);
    ASSERT_EQ(1, spin_unlocks);
    ASSERT_EQ(1, sock_desc.queue_count);

    ASSERT_EQ(0, *(uint32_t *)(netbuf() + NETBUF_OFFSET_NEXT));
    ASSERT_EQ(HDR_VA, *(uint32_t *)(netbuf() + NETBUF_OFFSET_HDR_PTR));
    ASSERT_EQ(0x11223344u, *(uint32_t *)(netbuf() + NETBUF_OFFSET_SRC_ADDR));
    ASSERT_EQ(0x5566, *(uint16_t *)(netbuf() + NETBUF_OFFSET_SRC_PORT));
    ASSERT_EQ(0x778899AAu, *(uint32_t *)(netbuf() + NETBUF_OFFSET_DST_ADDR));
    /* 0x00E16284: the record's flags word lands in the netbuf DST_PORT slot */
    ASSERT_EQ(0x0002, *(uint16_t *)(netbuf() + NETBUF_OFFSET_DST_PORT));
    ASSERT_EQ(0x100, *(uint16_t *)(netbuf() + NETBUF_OFFSET_DATA_LEN));
    ASSERT_EQ(0x30, *(uint16_t *)(netbuf() + NETBUF_OFFSET_DATA_LEN + 2));
    ASSERT_EQ(0x0077, *(uint16_t *)(netbuf() + NETBUF_OFFSET_EC_PARAM1));
    ASSERT_EQ(0x0088, *(uint16_t *)(netbuf() + NETBUF_OFFSET_EC_PARAM2));
    ASSERT_EQ(0, *(uint16_t *)(netbuf() + NETBUF_OFFSET_HOP_COUNT));

    /* the queue was empty, so head and tail both point at the page */
    ASSERT_EQ(NETBUF_VA, sock_desc.queue_head);
    ASSERT_EQ(NETBUF_VA, sock_desc.queue_tail);
}

/*
 * 0x00E162F0..0x00E1631E: slot n is copied only while data_len > n*0x400,
 * and cleared otherwise.
 */
TEST(data_pages_are_clipped_to_the_payload_length)
{
    setup();
    pkt.data_len = 0x401;                   /* reaches into the second page */
    sock_desc.max_data_len = 0x1000;

    SOCK_$PUT_INT_INT(&sock_desc, &pkt, 0, 0, 0);

    ASSERT_EQ(0x1000, *(uint32_t *)(netbuf() + NETBUF_OFFSET_DATA_PTRS + 0));
    ASSERT_EQ(0x2000, *(uint32_t *)(netbuf() + NETBUF_OFFSET_DATA_PTRS + 4));
    ASSERT_EQ(0, *(uint32_t *)(netbuf() + NETBUF_OFFSET_DATA_PTRS + 8));
    ASSERT_EQ(0, *(uint32_t *)(netbuf() + NETBUF_OFFSET_DATA_PTRS + 12));
}

TEST(zero_length_payload_clears_every_slot)
{
    setup();
    pkt.data_len = 0;

    SOCK_$PUT_INT_INT(&sock_desc, &pkt, 0, 0, 0);

    ASSERT_EQ(0, *(uint32_t *)(netbuf() + NETBUF_OFFSET_DATA_PTRS + 0));
    ASSERT_EQ(0, *(uint32_t *)(netbuf() + NETBUF_OFFSET_DATA_PTRS + 12));
}

/* 0x00E162A2: the loop runs n_hops times and copies from hops[]. */
TEST(hop_words_are_copied)
{
    setup();
    pkt.n_hops = 3;
    pkt.hops[0] = 0xAAAA;
    pkt.hops[1] = 0xBBBB;
    pkt.hops[2] = 0xCCCC;
    pkt.hops[3] = 0xDDDD;

    SOCK_$PUT_INT_INT(&sock_desc, &pkt, 0, 0, 0);

    ASSERT_EQ(3, *(uint16_t *)(netbuf() + NETBUF_OFFSET_HOP_COUNT));
    ASSERT_EQ(0xAAAA, *(uint16_t *)(netbuf() + NETBUF_OFFSET_HOP_ARRAY + 0));
    ASSERT_EQ(0xBBBB, *(uint16_t *)(netbuf() + NETBUF_OFFSET_HOP_ARRAY + 2));
    ASSERT_EQ(0xCCCC, *(uint16_t *)(netbuf() + NETBUF_OFFSET_HOP_ARRAY + 4));
    /* the fourth word must not have been copied */
    ASSERT_EQ(0, *(uint16_t *)(netbuf() + NETBUF_OFFSET_HOP_ARRAY + 6));
}

/* 0x00E1622E: an unallocated socket returns 2. */
TEST(unallocated_socket_returns_2)
{
    setup();
    sock_desc.flags &= (uint16_t)~SOCK_FLAG_ALLOCATED;

    ASSERT_EQ(2, SOCK_$PUT_INT_INT(&sock_desc, &pkt, 0, 0, 0));
    ASSERT_EQ(0, sock_desc.queue_count);
}

/*
 * 0x00E16234: the open bit is only required when the flags boolean is true
 * (tst.b D2b / bpl skips the tst.w).
 */
TEST(open_bit_is_only_required_when_the_boolean_is_true)
{
    setup();
    sock_desc.flags &= (uint16_t)~SOCK_FLAG_OPEN;

    ASSERT_EQ(0, SOCK_$PUT_INT_INT(&sock_desc, &pkt, 0, 0, 0));

    setup();
    sock_desc.flags &= (uint16_t)~SOCK_FLAG_OPEN;
    ASSERT_EQ(2, SOCK_$PUT_INT_INT(&sock_desc, &pkt, (int8_t)-1, 0, 0));
}

/* 0x00E16240: bls, so an exactly-maximum length is accepted. */
TEST(data_length_bound_is_inclusive)
{
    setup();
    pkt.data_len = sock_desc.max_data_len;
    ASSERT_EQ(0, SOCK_$PUT_INT_INT(&sock_desc, &pkt, 0, 0, 0));

    setup();
    pkt.data_len = (uint16_t)(sock_desc.max_data_len + 1);
    ASSERT_EQ(2, SOCK_$PUT_INT_INT(&sock_desc, &pkt, 0, 0, 0));
}

/* 0x00E16258: bcs, so a full queue returns 1. */
TEST(full_queue_returns_1)
{
    setup();
    sock_desc.queue_count = sock_desc.max_queue;

    ASSERT_EQ(1, SOCK_$PUT_INT_INT(&sock_desc, &pkt, 0, 0, 0));
    ASSERT_EQ(sock_desc.max_queue, sock_desc.queue_count);
    ASSERT_EQ(1, spin_unlocks);
}

/* 0x00E162D4: a second packet is appended through the tail's link word. */
TEST(second_packet_is_appended)
{
    uint8_t *second;

    setup();
    SOCK_$PUT_INT_INT(&sock_desc, &pkt, 0, 0, 0);

    pkt.hdr = 3072u + 0x40u;            /* a different netbuf page */
    second = arena + 3072u;
    SOCK_$PUT_INT_INT(&sock_desc, &pkt, 0, 0, 0);

    ASSERT_EQ(2, sock_desc.queue_count);
    ASSERT_EQ(NETBUF_VA, sock_desc.queue_head);
    ASSERT_EQ(3072u, sock_desc.queue_tail);
    ASSERT_EQ(3072u, *(uint32_t *)(netbuf() + NETBUF_OFFSET_NEXT));
    ASSERT_EQ(0, *(uint32_t *)(second + NETBUF_OFFSET_NEXT));
}

/*
 * SOCK_$PUT_INT with a true boolean writes queue_count into the HEADER
 * buffer at +0x0F (0x00E161D0 `movea.l (A2),A0` then `(0xf,A0)`), not into
 * the record.
 */
TEST(put_int_stamps_the_queue_count_into_the_header)
{
    ec_$eventcount_t *ec = NULL;

    setup();
    sock_desc.queue_count = 2;

    ASSERT_EQ((int8_t)-1,
              SOCK_$PUT_INT(TEST_SOCK, &pkt, (int8_t)-1, 0, 0, &ec));

    ASSERT_EQ(2, hdr_buf()[0x0F]);
    ASSERT_EQ(3, sock_desc.queue_count);
    ASSERT_EQ((uintptr_t)&sock_desc.ec, (uintptr_t)ec);
}

TEST(put_int_leaves_the_header_alone_when_the_boolean_is_false)
{
    ec_$eventcount_t *ec = NULL;

    setup();
    sock_desc.queue_count = 2;

    SOCK_$PUT_INT(TEST_SOCK, &pkt, 0, 0, 0, &ec);

    ASSERT_EQ(0, hdr_buf()[0x0F]);
}

/* 0x00E161AA / 0x00E161AE: the accepted range is 1..0xE0. */
TEST(socket_number_bounds)
{
    ec_$eventcount_t *ec = NULL;

    setup();
    ASSERT_EQ(0, SOCK_$PUT_INT(0, &pkt, 0, 0, 0, &ec));
    ASSERT_EQ(0, spin_locks);

    setup();
    ASSERT_EQ(0, SOCK_$PUT_INT(0xE1, &pkt, 0, 0, 0, &ec));
    ASSERT_EQ(0, spin_locks);

    /* 0xE0 is inside the range: it reaches the (null) table slot */
    setup();
    SOCK_GET_VIEW_PTR(0xE0) = &sock_desc;
    ASSERT_EQ((int8_t)-1, SOCK_$PUT_INT(0xE0, &pkt, 0, 0, 0, &ec));
}

/* SOCK_$PUT advances the event count only on success (0x00E16178 bpl). */
TEST(put_advances_the_event_count_on_success)
{
    setup();

    ASSERT_EQ((int8_t)-1, SOCK_$PUT(TEST_SOCK, &pkt, 0, 0, 0));
    ASSERT_EQ(1, ec_advances);
    ASSERT_EQ((uintptr_t)&sock_desc.ec, (uintptr_t)ec_advanced);
}

TEST(put_does_not_advance_on_failure)
{
    setup();
    sock_desc.queue_count = sock_desc.max_queue;

    ASSERT_EQ(0, SOCK_$PUT(TEST_SOCK, &pkt, 0, 0, 0));
    ASSERT_EQ(0, ec_advances);
}

int main(void)
{
    printf("=== SOCK_$PUT tests ===\n");

    RUN_TEST(put_int_int_fills_the_netbuf_header);
    RUN_TEST(data_pages_are_clipped_to_the_payload_length);
    RUN_TEST(zero_length_payload_clears_every_slot);
    RUN_TEST(hop_words_are_copied);
    RUN_TEST(unallocated_socket_returns_2);
    RUN_TEST(open_bit_is_only_required_when_the_boolean_is_true);
    RUN_TEST(data_length_bound_is_inclusive);
    RUN_TEST(full_queue_returns_1);
    RUN_TEST(second_packet_is_appended);
    RUN_TEST(put_int_stamps_the_queue_count_into_the_header);
    RUN_TEST(put_int_leaves_the_header_alone_when_the_boolean_is_false);
    RUN_TEST(socket_number_bounds);
    RUN_TEST(put_advances_the_event_count_on_success);
    RUN_TEST(put_does_not_advance_on_failure);

    printf("\n%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed != 0;
}
