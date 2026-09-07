/*
 * msg/test/test_data_layout.c - Layout tests for msg_$data_t (source-eq3o)
 *
 * The record's offsets are only asserted at compile time under ARCH_M68K, so
 * this test re-checks them on the host and, more importantly, checks the two
 * spellings the original uses for the ownership bitmaps against each other:
 *
 *   MSG_$SOCK_OWNERS[socket]        base + 0x1D8 + socket*8   (one-based)
 *   MSG_$DATA->ownership[socket-1]  base + 0x1E0 + (socket-1)*8
 *
 * and that the depth table (base + 0x1E + socket*2) stops exactly where the
 * first reachable bitmap starts, which is what makes both tables fit.
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

#include "msg/msg_internal.h"

msg_$data_t MSG_$DATA_STRUCT;

#define OFF(field) ((size_t)((uint8_t *)&(field) - (uint8_t *)MSG_$DATA))

/* ==========================================================================
 * Tests
 * ========================================================================== */

/*
 * 0x00E59276 "move.w (A3),(0x1e,A5,D1w*0x1)" with D1 = socket*2,
 * 0x00E5920A "lea (0x1d8,A0),A1" with A0 = A5 + socket*8,
 * 0x00E5927A "addq.w #0x1,(0x8e0,A5)".
 */
TEST(recovered_offsets)
{
    ASSERT_EQ(0x1E, OFF(MSG_$DATA->depth[0]));
    ASSERT_EQ(0x1E0, OFF(MSG_$DATA->ownership[0][0]));
    ASSERT_EQ(0x8E0, OFF(MSG_$DATA->open_count));
    ASSERT_EQ(0x8E2, sizeof(msg_$data_t));

    /* The depth table ends exactly where the ownership table starts. */
    ASSERT_EQ(0x1E0, OFF(MSG_$DATA->depth[0]) + sizeof(MSG_$DATA->depth));
}

/*
 * The depth table is indexed by the socket number itself, so socket 0xE0 -
 * the largest MSG_$CLOSEI and MSG_$WAITI accept (0x00E593FE / 0x00E59BDA
 * "cmpi.w #0xe0,D0w / ble") - must still land inside it.
 */
TEST(depth_table_covers_every_legal_socket)
{
    int sock;

    for (sock = 1; sock <= MSG_MAX_SOCKET; sock++) {
        ASSERT_EQ(0x1E + sock * 2, OFF(MSG_$DATA->depth[sock]));
    }
    /* the last word ends at 0x1E0, not past it */
    ASSERT_EQ(0x1E0, OFF(MSG_$DATA->depth[MSG_MAX_SOCKET]) + 2);
}

/*
 * MSG_$SOCK_OWNERS[n] and MSG_$DATA->ownership[n - 1] are the same eight
 * bytes for every socket the kernel accepts, and slot 0 of the one-based view
 * is the two padding bytes plus the last depth entry - which is why the
 * original never dereferences it.
 */
TEST(sock_owners_aliases_the_ownership_table)
{
    int sock;

    for (sock = 1; sock <= MSG_MAX_SOCKET; sock++) {
        ASSERT_EQ((uintptr_t)&MSG_$DATA->ownership[sock - 1][0],
                  (uintptr_t)&MSG_$SOCK_OWNERS[sock][0]);
        ASSERT_EQ(0x1D8 + sock * 8, OFF(MSG_$SOCK_OWNERS[sock][0]));
    }

    /* the last bitmap ends exactly at open_count */
    ASSERT_EQ(0x8E0, OFF(MSG_$SOCK_OWNERS[MSG_MAX_SOCKET][0]) + 8);
}

/*
 * A write through one spelling is visible through the other, and writing
 * socket n's bitmap does not disturb its neighbours or the depth table.
 */
TEST(writes_do_not_alias_neighbours)
{
    memset(&MSG_$DATA_STRUCT, 0, sizeof(MSG_$DATA_STRUCT));

    MSG_$DATA->depth[5] = 0x1234;
    MSG_$DATA->depth[MSG_MAX_SOCKET] = 0x5678;

    MSG_$SOCK_OWNERS[5][7] = 0x08;

    ASSERT_EQ(0x08, MSG_$DATA->ownership[4][7]);
    ASSERT_EQ(0x1234, MSG_$DATA->depth[5]);
    ASSERT_EQ(0x5678, MSG_$DATA->depth[MSG_MAX_SOCKET]);
    ASSERT_EQ(0, MSG_$DATA->ownership[3][7]);
    ASSERT_EQ(0, MSG_$DATA->ownership[5][7]);
    ASSERT_EQ(0, MSG_$DATA->open_count);

    /* Socket 1's bitmap must not overlap the last depth word. */
    memset(&MSG_$DATA_STRUCT, 0, sizeof(MSG_$DATA_STRUCT));
    memset(MSG_$SOCK_OWNERS[1], 0xFF, 8);
    ASSERT_EQ(0, MSG_$DATA->depth[MSG_MAX_SOCKET]);
}

/*
 * The bit numbering the four writers share: byte (0x3F - asid) >> 3 computed
 * in word arithmetic with a logical shift, bit asid & 7
 * (0x00E5925C-0x00E59260, 0x00E5942E-0x00E59436).
 */
TEST(ownership_bit_numbering)
{
    uint16_t asid;

    memset(&MSG_$DATA_STRUCT, 0, sizeof(MSG_$DATA_STRUCT));

    for (asid = 0; asid < 64; asid++) {
        int byte_index = (int)((uint16_t)(0x3F - asid) >> 3);
        MSG_$SOCK_OWNERS[9][byte_index] |= (uint8_t)(1 << (asid & 7));
    }

    /* All 64 ASIDs together fill the whole bitmap and nothing else. */
    ASSERT_EQ(0xFF, MSG_$SOCK_OWNERS[9][0]);
    ASSERT_EQ(0xFF, MSG_$SOCK_OWNERS[9][7]);
    ASSERT_EQ(0, MSG_$SOCK_OWNERS[8][7]);
    ASSERT_EQ(0, MSG_$SOCK_OWNERS[10][0]);

    /* asid 0 -> byte 7 bit 0; asid 63 -> byte 0 bit 7 */
    ASSERT_EQ(7, (int)((uint16_t)(0x3F - 0) >> 3));
    ASSERT_EQ(0, (int)((uint16_t)(0x3F - 63) >> 3));
}

int main(void)
{
    printf("msg_$data_t layout tests\n");

    RUN_TEST(recovered_offsets);
    RUN_TEST(depth_table_covers_every_legal_socket);
    RUN_TEST(sock_owners_aliases_the_ownership_table);
    RUN_TEST(writes_do_not_alias_neighbours);
    RUN_TEST(ownership_bit_numbering);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
