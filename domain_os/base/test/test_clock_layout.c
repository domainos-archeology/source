/*
 * base/test/test_clock_layout.c - the 48-bit clock_t record's layout.
 *
 * clock_t (base/base.h) is the {uint32 high, uint16 low} pair every Domain/OS
 * timestamp uses.  It is SIX bytes wide in the image: TIME_$CLOCK writes it
 * as a longword followed by a word, and every record that embeds it puts the
 * next field six bytes on -- tpad_$globals_t.touchpad_max at +0x0A follows
 * last_clock at +0x04 (0x00E6969C `move.l (0x4,A1),(0x164,A5)` and
 * 0x00E696A2 `move.w (0x8,A1),(0x168,A5)`), and netbuf_globals_t.delay_time
 * at 0x300 is followed by the two-byte gap at 0x306 and the spin lock at
 * 0x308 (0x00E0EE18 `pea (0x300,A5)`).
 *
 * m68k-elf-gcc already gives the unadorned struct size 6 / alignment 2, but a
 * 64-bit host would pad it to 8 with alignment 4 and shift every embedded
 * copy, which is why base/base.h states the packed spelling explicitly.
 * These checks fail loudly if that spelling is ever dropped.  (source-no75)
 */

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "base/base.h"
#include "netbuf/netbuf_internal.h"
#include "tpad/tpad.h"

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define RUN_TEST(name)                                                        \
    do {                                                                      \
        printf("  %-46s", #name);                                             \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        if (current_failed) { tests_failed++; } else { printf("PASSED\n"); }   \
    } while (0)

#define CHECK_EQ(expected, actual)                                            \
    do {                                                                      \
        long _e = (long)(expected);                                           \
        long _a = (long)(actual);                                             \
        if (_e != _a) {                                                       \
            if (!current_failed) printf("FAILED\n");                          \
            current_failed = 1;                                               \
            printf("      %s:%d: %s: expected %ld, got %ld\n", __FILE__,      \
                   __LINE__, #actual, _e, _a);                                \
        }                                                                     \
    } while (0)

/* ---------------------------------------------------------------------- */

/* The record itself: six bytes, word aligned, high half first. */
static void test_clock_is_six_bytes(void)
{
    CHECK_EQ(6, sizeof(clock_t));
    CHECK_EQ(2, _Alignof(clock_t));
    CHECK_EQ(0, offsetof(clock_t, high));
    CHECK_EQ(4, offsetof(clock_t, low));
}

/* An array of clocks strides by six, which is what a queue of them assumes. */
static void test_clock_array_strides_by_six(void)
{
    clock_t two[2];

    CHECK_EQ(12, sizeof(two));
    CHECK_EQ(6, (long)((char *)&two[1] - (char *)&two[0]));
}

/*
 * The 48 bits are laid out big-endian on the target, so writing the two
 * halves and reading the six bytes back must give high-then-low with no
 * padding in between.  The byte order of each half is the host's, so the
 * check is on the placement, not on the byte values.
 */
static void test_clock_halves_are_adjacent(void)
{
    clock_t c;

    memset(&c, 0xA5, sizeof(c));
    c.high = 0x11223344u;
    c.low  = 0x5566u;

    CHECK_EQ(0, memcmp((char *)&c + 0, &c.high, 4));
    CHECK_EQ(0, memcmp((char *)&c + 4, &c.low, 2));
}

/* tpad_$globals_t embeds one at +0x04 and stays 0x20 bytes wide. */
static void test_tpad_globals_layout(void)
{
    CHECK_EQ(0x20, sizeof(tpad_$globals_t));
    CHECK_EQ(0x04, offsetof(tpad_$globals_t, last_clock));
    CHECK_EQ(0x0A, offsetof(tpad_$globals_t, touchpad_max));
}

/*
 * netbuf_globals_t embeds one at 0x300; the SAU2 map's "NETBUF_ size = 338"
 * only holds if it is six bytes and the explicit pad at 0x306 closes the gap.
 */
static void test_netbuf_globals_layout(void)
{
    CHECK_EQ(0x300, offsetof(netbuf_globals_t, delay_time));
    CHECK_EQ(0x306, offsetof(netbuf_globals_t, pad_306));
    CHECK_EQ(0x308, offsetof(netbuf_globals_t, spin_lock));
    CHECK_EQ(0x338, sizeof(netbuf_globals_t));
}

int main(void)
{
    printf("clock_t layout tests:\n");
    RUN_TEST(clock_is_six_bytes);
    RUN_TEST(clock_array_strides_by_six);
    RUN_TEST(clock_halves_are_adjacent);
    RUN_TEST(tpad_globals_layout);
    RUN_TEST(netbuf_globals_layout);
    printf("%d run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
