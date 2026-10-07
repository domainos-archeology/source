/*
 * tone/test/test_tone.c - Unit tests for TONE_$ENABLE (0x00E1ACE8) and
 * TONE_$TIME (0x00E172FC)
 *
 * The real tone/enable.c and tone/time.c are #included; SIO2681_$TONE,
 * PROC1_$SET_LOCK / CLR_LOCK and TIME_$WAIT are mocked and record the
 * order and arguments of their calls.
 */

#include <stdio.h>
#include <string.h>

#include "tone/tone_internal.h"

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("%-44s %s\n", #name, current_failed ? "FAIL" : "ok");          \
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
#define ASSERT_PTR_EQ(e, a) ASSERT_EQ((uintptr_t)(e), (uintptr_t)(a))

term_data_t TERM_$DATA;

/* event log: 'L' lock, 'U' unlock, 'T' tone, 'W' wait */
static char log_buf[16];
static int log_n;
static void logc(char c) { if (log_n < 15) { log_buf[log_n++] = c; log_buf[log_n] = 0; } }

static uint16_t lock_id;
void (PROC1_$SET_LOCK)(uint32_t id_slot) { uint16_t id = (uint16_t)ARCH_PASCAL_SLOT_WORD(id_slot); (void)id; lock_id = id; logc('L'); }
void (PROC1_$CLR_LOCK)(uint32_t id_slot) { uint16_t id = (uint16_t)ARCH_PASCAL_SLOT_WORD(id_slot); (void)id; lock_id = id; logc('U'); }

static sio2681_channel_t **tone_cell;
static sio2681_channel_t *tone_channel;
static uint8_t tone_values[4];
static int tone_calls;
void SIO2681_$TONE(sio2681_channel_t **channel_cell, uint8_t *enable_ptr,
                   status_$t *status_unused)
{
    (void)status_unused;
    tone_cell = channel_cell;
    tone_channel = *channel_cell;
    if (tone_calls < 4) { tone_values[tone_calls] = *enable_ptr; }
    tone_calls++;
    logc('T');
}

static uint16_t wait_type;
static clock_t wait_delay;
static clock_t *wait_delay_ptr;
void TIME_$WAIT(uint16_t *delay_type, clock_t *delay, status_$t *status)
{
    wait_type = *delay_type;
    wait_delay = *delay;
    wait_delay_ptr = delay;
    *status = status_$ok;
    logc('W');
}

#include "../enable.c"
#include "../time.c"

static void reset(void) { log_n = 0; log_buf[0] = 0; tone_calls = 0; }

TEST(enable_passes_channel_cell)
{
    uint8_t on = 0xFF;
    reset();
    TONE_$ENABLE(&on);
    ASSERT_EQ(1, tone_calls);
    ASSERT_PTR_EQ(&TONE_$CHANNEL, tone_channel);
    ASSERT_PTR_EQ((char *)&TERM_$DATA + 0x1268, tone_channel);
    ASSERT_EQ(1, tone_cell != &tone_channel);   /* a cell of TONE_$ENABLE's own */
    ASSERT_EQ(0xFF, tone_values[0]);
}

TEST(time_sequence)
{
    clock_t d = { 0x12, 0x3456 };
    reset();
    TONE_$TIME(&d);
    ASSERT_EQ(0, strcmp("LTWTU", log_buf));
    ASSERT_EQ(0x0E, lock_id);
    ASSERT_EQ(0xFF, tone_values[0]);
    ASSERT_EQ(0x00, tone_values[1]);
    ASSERT_EQ(0, wait_type);
    ASSERT_EQ(0x12, wait_delay.high);
    ASSERT_EQ(0x3456, wait_delay.low);
    ASSERT_EQ(1, wait_delay_ptr != &d);         /* the frame copy */
}

TEST(constant_cells)
{
    ASSERT_EQ(0xFF, tone_enable_byte);
    ASSERT_EQ(0x00, tone_disable_byte);
    ASSERT_EQ(0, tone_wait_type);
}

int main(void)
{
    RUN_TEST(enable_passes_channel_cell);
    RUN_TEST(time_sequence);
    RUN_TEST(constant_cells);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
