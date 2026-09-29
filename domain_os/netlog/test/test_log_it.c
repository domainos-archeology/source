/*
 * netlog/test/test_log_it.c - Unit tests for NETLOG_$LOG_IT (0x00E71B38).
 *
 * Compiles the real netlog/log_it.c and supplies the globals and callees it
 * reaches, so the three things the audit of this file turned up are checked
 * against behaviour rather than restated:
 *   - page_counts[] is Pascal [1..2] (bead source-8vau), declared at its
 *     bias slot, so the counter for buffer 1 is element 1 at block +0x70
 *     (0x00E71B9C `(0x6e,A0)` with A0 = A5 + idx*2),
 *   - the record address is `buf + count*26 - 26`, because 0x00E71BCE..
 *     0x00E71C02 write at displacements -0x1A..-0x02,
 *   - the timestamp longword is the MIDDLE 32 bits of the 48-bit clock
 *     (0x00E71BDA reads `(-0xe,A6)`, two bytes into the clock record).
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

#include "netlog/netlog_internal.h"

/* Globals the code under test links against. */
MODULE_DATA_DEFINE(netlog_$data_t, NETLOG_$DATA, 0x00E85684);
uint32_t NETLOG_$KINDS;
ec_$eventcount_t NETLOG_$EC;
uint16_t PROC1_$CURRENT;

/* Mocked callees. */
static int spin_locks;
static int spin_unlocks;

ml_$spin_token_t ML_$SPIN_LOCK(void *lockp)
{
    (void)lockp;
    spin_locks++;
    return 0x1234;
}

void ML_$SPIN_UNLOCK(void *lockp, ml_$spin_token_t token)
{
    (void)lockp;
    spin_unlocks++;
    /* the token must survive the whole critical section */
    if (token != 0x1234) {
        printf("BAD TOKEN ");
        tests_failed++;
    }
}

static clock_t mock_clock;

void TIME_$CLOCK(clock_t *clock)
{
    *clock = mock_clock;
}

static int ec_advances;

void EC_$ADVANCE(ec_$eventcount_t *ec)
{
    if (ec == &NETLOG_$EC) {
        ec_advances++;
    }
}

#include "../log_it.c"

/*
 * The buffers are addressed through the uint32_t target VA in
 * current_buf_ptr, so the test points ARCH_HOST_VA_BASE at an arena and uses
 * arena offsets as VAs.  Offset 0 is left unused: ARCH_VA_TO_PTR(0) is NULL.
 */
static uint8_t arena[3 * 1024];
#define PAGE1_VA 1024u
#define PAGE2_VA 2048u
#define page1 (arena + PAGE1_VA)
#define page2 (arena + PAGE2_VA)

static void setup(void)
{
    memset(&NETLOG_$DATA, 0, sizeof(NETLOG_$DATA));
    memset(arena, 0, sizeof(arena));
    ARCH_HOST_VA_BASE = (uintptr_t)arena;
    NETLOG_$DATA.buffer_va[1] = PAGE1_VA;
    NETLOG_$DATA.buffer_va[2] = PAGE2_VA;
    NETLOG_$DATA.current_buf_index = 1;
    NETLOG_$DATA.current_buf_ptr = NETLOG_$DATA.buffer_va[1];
    NETLOG_$KINDS = 0xFFFFFFFFu;
    PROC1_$CURRENT = 0x0207;   /* the entry keeps the low byte only */
    mock_clock.high = 0x11223344;
    mock_clock.low = 0x5566;
    spin_locks = spin_unlocks = ec_advances = 0;
}

static uint32_t a_uid[2] = { 0xAABBCCDD, 0x01020304 };

/* A kind whose bit is clear in NETLOG_$KINDS logs nothing and takes no lock. */
TEST(disabled_kind_is_dropped)
{
    setup();
    NETLOG_$KINDS = ~(1u << 5);

    NETLOG_$LOG_IT(5, a_uid, 0, 0, 0, 0, 0, 0);

    ASSERT_EQ(0, spin_locks);
    ASSERT_EQ(0, NETLOG_$DATA.page_counts[1]);
}

/* btst on a longword numbers bits mod 32, so kind 37 is bit 5. */
TEST(kind_bit_is_taken_mod_32)
{
    setup();
    NETLOG_$KINDS = 1u << 5;

    NETLOG_$LOG_IT(37, a_uid, 0, 0, 0, 0, 0, 0);

    ASSERT_EQ(1, spin_locks);
    ASSERT_EQ(1, NETLOG_$DATA.page_counts[1]);
}

/* The first entry of buffer 1 lands at offset 0 and bumps page_counts[1]. */
TEST(first_entry_is_at_offset_zero)
{
    netlog_entry_t *e = (netlog_entry_t *)page1;

    setup();

    NETLOG_$LOG_IT(3, a_uid, 0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666);

    ASSERT_EQ(1, NETLOG_$DATA.page_counts[1]);
    ASSERT_EQ(0, NETLOG_$DATA.page_counts[2]);
    ASSERT_EQ(1, spin_locks);
    ASSERT_EQ(1, spin_unlocks);
    ASSERT_EQ(0, ec_advances);

    ASSERT_EQ(3, e->kind);
    ASSERT_EQ(0x07, e->process_id);
    /* the middle 32 bits of {0x11223344, 0x5566} */
    ASSERT_EQ(0x33445566u, e->timestamp);
    ASSERT_EQ(0xAABBCCDDu, e->uid_high);
    ASSERT_EQ(0x01020304u, e->uid_low);
    ASSERT_EQ(0x1111, e->param3);
    ASSERT_EQ(0x22, e->param4);     /* low byte only */
    ASSERT_EQ(0x3333, e->param5);
    ASSERT_EQ(0x4444, e->param6);
    ASSERT_EQ(0x5555, e->param7);
    ASSERT_EQ(0x6666, e->param8);
}

TEST(second_entry_is_one_record_on)
{
    netlog_entry_t *e1 = (netlog_entry_t *)page1;
    netlog_entry_t *e2 = (netlog_entry_t *)(page1 + NETLOG_ENTRY_SIZE);

    setup();

    NETLOG_$LOG_IT(1, a_uid, 0, 0, 0, 0, 0, 0);
    NETLOG_$LOG_IT(2, a_uid, 0, 0, 0, 0, 0, 0);

    ASSERT_EQ(2, NETLOG_$DATA.page_counts[1]);
    ASSERT_EQ(1, e1->kind);
    ASSERT_EQ(2, e2->kind);
}

/*
 * 39 entries fill the page: the buffer flips to 2, done_cnt goes up, the new
 * buffer's counter is cleared and the eventcount is advanced once, AFTER the
 * spin lock is released (0x00E71C4C then 0x00E71C62).
 */
TEST(full_page_flips_the_buffer)
{
    int i;

    setup();

    for (i = 0; i < NETLOG_ENTRIES_PER_PAGE; i++) {
        NETLOG_$LOG_IT(1, a_uid, 0, 0, 0, 0, 0, 0);
    }

    ASSERT_EQ(2, NETLOG_$DATA.current_buf_index);
    ASSERT_EQ(1, NETLOG_$DATA.send_page_index);
    ASSERT_EQ(1, NETLOG_$DATA.done_cnt);
    ASSERT_EQ(NETLOG_ENTRIES_PER_PAGE, NETLOG_$DATA.page_counts[1]);
    ASSERT_EQ(0, NETLOG_$DATA.page_counts[2]);
    ASSERT_EQ(PAGE2_VA, NETLOG_$DATA.current_buf_ptr);
    ASSERT_EQ(1, ec_advances);
    ASSERT_EQ(NETLOG_ENTRIES_PER_PAGE, spin_unlocks);

    /* the last record must still be inside the page */
    ASSERT_EQ(1, ((netlog_entry_t *)(page1 + 38 * NETLOG_ENTRY_SIZE))->kind);
    /* and nothing may have been written past it */
    ASSERT_EQ(0, page1[39 * NETLOG_ENTRY_SIZE]);
}

/* After the flip, entries go to element 1 of the counter array and to page2. */
TEST(entries_after_the_flip_use_the_other_counter)
{
    int i;

    setup();
    for (i = 0; i < NETLOG_ENTRIES_PER_PAGE; i++) {
        NETLOG_$LOG_IT(1, a_uid, 0, 0, 0, 0, 0, 0);
    }

    NETLOG_$LOG_IT(9, a_uid, 0, 0, 0, 0, 0, 0);

    ASSERT_EQ(1, NETLOG_$DATA.page_counts[2]);
    ASSERT_EQ(NETLOG_ENTRIES_PER_PAGE, NETLOG_$DATA.page_counts[1]);
    ASSERT_EQ(9, ((netlog_entry_t *)page2)->kind);
}

int main(void)
{
    printf("=== NETLOG_$LOG_IT tests ===\n");

    RUN_TEST(disabled_kind_is_dropped);
    RUN_TEST(kind_bit_is_taken_mod_32);
    RUN_TEST(first_entry_is_at_offset_zero);
    RUN_TEST(second_entry_is_one_record_on);
    RUN_TEST(full_page_flips_the_buffer);
    RUN_TEST(entries_after_the_flip_use_the_other_counter);

    printf("\n%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed != 0;
}
