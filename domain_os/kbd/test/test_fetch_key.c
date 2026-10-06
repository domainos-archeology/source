/*
 * kbd/test/test_fetch_key.c - kbd_$fetch_key (0x00E1CAFE), kbd_$state_lookup
 * (0x00E1C9FC), kbd_$get_mode (0x00E1CA62), kbd_$translate_key (0x00E1CC64)
 *
 * Runs the real state machine over kbd/kbd_data.c's image tables.
 */

#include "kbd/kbd_internal.h"

uint8_t SMD_$KTT[0x100];
uint8_t SMD_$GERMAN_KTT[0x100];
uint8_t SMD_$FRENCH_KTT[0x100];
uint8_t SMD_$SWEDISH_KTT[0x100];
uint8_t SMD_$UK_KTT[0x100];
uint8_t SMD_$SWISS_KTT[0x100];

#include "../kbd_data.c"
#include "../state_lookup.c"
#include "../get_mode.c"
#include "../translate_key.c"
#include "../fetch_key.c"

#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("%-48s %s\n", #name, current_failed ? "FAIL" : "ok");          \
    } while (0)
#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long e_ = (long long)(expected), a_ = (long long)(actual);       \
        if (e_ != a_) {                                                       \
            printf("  %s:%d: expected 0x%llx, got 0x%llx\n",                  \
                   __FILE__, __LINE__, (unsigned long long)e_,                \
                   (unsigned long long)a_);                                   \
            if (!current_failed) { current_failed = 1; tests_failed++; }      \
        }                                                                     \
    } while (0)

static kbd_state_t st;
static uint8_t ktt[0x100];

static void reset(void)
{
    int i;
    memset(&st, 0, sizeof(st));
    st.ring_head = st.ring_tail = 1;
    st.ring_size = KBD_RING_SIZE;
    for (i = 0; i < 0x100; i++) {
        ktt[i] = (uint8_t)(i ^ 0x20);
        SMD_$KTT[i] = (uint8_t)(0xff - i);
    }
    st.ktt_ptr = ktt;
}

static void push(uint8_t k)
{
    st.ring_buffer[st.ring_tail - 1] = k;
    st.ring_tail = (uint16_t)(st.ring_tail == KBD_RING_SIZE ? 1 : st.ring_tail + 1);
}

/* ---------------------------------------------------- state_lookup */

TEST(lookup_hits_listed_key)
{
    ASSERT_EQ((long long)(intptr_t)&kbd_$state_table[3],
              (long long)(intptr_t)kbd_$state_lookup(0, 0xFE));
    ASSERT_EQ((long long)(intptr_t)&kbd_$state_table[0],
              (long long)(intptr_t)kbd_$state_lookup(0, 0xFB));
}

TEST(lookup_miss_returns_default_entry)
{
    ASSERT_EQ((long long)(intptr_t)&kbd_$state_table[5],
              (long long)(intptr_t)kbd_$state_lookup(0, 'a'));
    /* state 1: range (6,6), empty: the default entry 6 */
    ASSERT_EQ((long long)(intptr_t)&kbd_$state_table[6],
              (long long)(intptr_t)kbd_$state_lookup(1, 0xFB));
    ASSERT_EQ((long long)(intptr_t)&kbd_$state_table[9],
              (long long)(intptr_t)kbd_$state_lookup(4, 0));
}

/* ---------------------------------------------------- get_mode / translate */

TEST(get_mode_finds_index_or_zero)
{
    ASSERT_EQ(4, kbd_$get_mode(0x12));
    ASSERT_EQ(7, kbd_$get_mode(0x0F));
    ASSERT_EQ(0, kbd_$get_mode(0x00));
    ASSERT_EQ(0, kbd_$get_mode(0x55));
}

TEST(translate_key_table)
{
    ASSERT_EQ(0x16, kbd_$translate_key(0xC0));
    ASSERT_EQ(0x18, kbd_$translate_key(0x91));
    ASSERT_EQ(0x04, kbd_$translate_key(0x92));
    ASSERT_EQ(0x08, kbd_$translate_key(0x95));
    ASSERT_EQ(0x0D, kbd_$translate_key(0x96));
    ASSERT_EQ(0x09, kbd_$translate_key(0x97));
    ASSERT_EQ(0x09, kbd_$translate_key(0x98));
    ASSERT_EQ(0x09, kbd_$translate_key(0x99));
    ASSERT_EQ(0x93, kbd_$translate_key(0x93));
    ASSERT_EQ('a', kbd_$translate_key('a'));
}

/* ---------------------------------------------------- fetch_key */

TEST(fetch_empty_ring_returns_false_and_saves_state)
{
    uint8_t key = 0x77;
    int16_t mode = -1;
    reset();
    st.pending_mode = 3;
    st.sub_state = 2;
    ASSERT_EQ(0, kbd_$fetch_key(&st, &key, &mode));
    ASSERT_EQ(3, mode);
    ASSERT_EQ(0x77, key);
    ASSERT_EQ(1, st.ring_head);
    ASSERT_EQ(2, st.sub_state);
    ASSERT_EQ(3, st.pending_mode);
}

TEST(fetch_plain_key_mode0_translates_through_ktt)
{
    uint8_t key;
    int16_t mode;
    reset();
    push('a');                       /* state 0 default (00,10): action 1 */
    ASSERT_EQ(-1, kbd_$fetch_key(&st, &key, &mode));
    ASSERT_EQ('a' ^ 0x20, key);
    ASSERT_EQ(0, mode);
    ASSERT_EQ(2, st.ring_head);
    ASSERT_EQ(0, st.sub_state);
}

TEST(fetch_nonzero_mode_skips_ktt)
{
    uint8_t key;
    int16_t mode;
    reset();
    st.pending_mode = 2;
    st.sub_state = 0;
    push(0xFF);                      /* (FF,10): action 1, next 0 */
    ASSERT_EQ(-1, kbd_$fetch_key(&st, &key, &mode));
    ASSERT_EQ(0xFF, key);
    ASSERT_EQ(2, mode);
}

TEST(fetch_touchpad_sequence_then_action_12)
{
    uint8_t key;
    int16_t mode;
    reset();
    /* E8 (action 3 -> state 1), x (4 -> 2), y (5 -> 3), z (6 -> escape
     * state[mode 0] = 0): none yields a key, then 0xFE (B4: action 11 ->
     * state 4) and 0x90 in state 4 (00,C0): action 12 */
    push(0xE8); push(1); push(2); push(3); push(0xFE); push(0x90);
    ASSERT_EQ(-1, kbd_$fetch_key(&st, &key, &mode));
    ASSERT_EQ(0xFF - 0x90, key);     /* SMD_$KTT[0x90] */
    ASSERT_EQ(7, mode);              /* D5 set -> *mode_out = 7 */
    ASSERT_EQ(0, st.pending_mode);   /* saved before the 7 */
    ASSERT_EQ(0, st.sub_state);
    ASSERT_EQ(7, st.ring_head);
}

TEST(fetch_action_12_low_key_unchanged)
{
    uint8_t key;
    int16_t mode;
    reset();
    st.sub_state = 4;
    push(0x41);
    ASSERT_EQ(-1, kbd_$fetch_key(&st, &key, &mode));
    ASSERT_EQ(0x41, key);
    ASSERT_EQ(7, mode);
}

TEST(fetch_head_wraps_at_0x40)
{
    uint8_t key;
    int16_t mode;
    reset();
    st.ring_head = st.ring_tail = KBD_RING_SIZE;
    push('b');
    ASSERT_EQ(-1, kbd_$fetch_key(&st, &key, &mode));
    ASSERT_EQ('b' ^ 0x20, key);
    ASSERT_EQ(1, st.ring_head);
}

TEST(fetch_manual_stop_entry_is_a_key)
{
    uint8_t key;
    int16_t mode;
    reset();
    push(0xFB);                      /* (FB,20): action 2 */
    ASSERT_EQ(-1, kbd_$fetch_key(&st, &key, &mode));
    ASSERT_EQ(0xFB ^ 0x20, key);
}

int main(void)
{
    RUN_TEST(lookup_hits_listed_key);
    RUN_TEST(lookup_miss_returns_default_entry);
    RUN_TEST(get_mode_finds_index_or_zero);
    RUN_TEST(translate_key_table);
    RUN_TEST(fetch_empty_ring_returns_false_and_saves_state);
    RUN_TEST(fetch_plain_key_mode0_translates_through_ktt);
    RUN_TEST(fetch_nonzero_mode_skips_ktt);
    RUN_TEST(fetch_touchpad_sequence_then_action_12);
    RUN_TEST(fetch_action_12_low_key_unchanged);
    RUN_TEST(fetch_head_wraps_at_0x40);
    RUN_TEST(fetch_manual_stop_entry_is_a_key);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
