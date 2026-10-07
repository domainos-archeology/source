/*
 * sio/test/test_set_break.c - sio_$set_break (0x00E67E86)
 *
 * Pins the state-word bits the routine edits (bits 0 and 3 of the low
 * byte, the rest of the word untouched), the lock/unlock bracket around
 * them with the token handed back, and the driver call through the +0x48
 * cell with (context, enable).
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

int __host_intr_disable_count = 0;

#include "sio/sio_internal.h"

uint32_t SIO_$SPIN_LOCK;

static int lock_calls, unlock_calls; static void *lock_arg, *unlock_arg; static ml_$spin_token_t unlock_token;
ml_$spin_token_t ML_$SPIN_LOCK(void *lock) { lock_calls++; lock_arg = lock; return 0x4321; }
void (ML_$SPIN_UNLOCK)(void *lock, uint32_t token_slot) { ml_$spin_token_t token = (ml_$spin_token_t)ARCH_PASCAL_SLOT_WORD(token_slot); (void)token; unlock_calls++; unlock_arg = lock; unlock_token = token; }

static int sb_calls; static m68k_ptr_t sb_ctx; static int8_t sb_enable; static int sb_unlocks_seen;
static void mock_set_break(m68k_ptr_t ctx, int8_t enable)
{ sb_calls++; sb_ctx = ctx; sb_enable = enable; sb_unlocks_seen = unlock_calls; }

#include "../set_break.c"

static int tests_passed = 0;
static int tests_failed = 0;
#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { printf("  Running %-44s ", #name); test_##name(); \
    tests_passed++; printf("PASSED\n"); } while (0)
#define ASSERT_EQ(expected, actual) do { \
    unsigned long long _e = (unsigned long long)(expected); \
    unsigned long long _a = (unsigned long long)(actual); \
    if (_e != _a) { printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n", \
        _e, _a, __LINE__); tests_failed++; return; } } while (0)
#define ASSERT_PTR_EQ(expected, actual) do { \
    const void *_e = (const void *)(expected); const void *_a = (const void *)(actual); \
    if (_e != _a) { printf("FAILED\n    Expected: %p, Got: %p at line %d\n", _e, _a, __LINE__); \
        tests_failed++; return; } } while (0)

static sio_desc_t desc;

static void reset(void)
{
    memset(&desc, 0, sizeof desc);
    desc.context = 0x0000C0DE;
    ARCH_HOST_VA_BASE = (uintptr_t)mock_set_break & ~(uintptr_t)0xFFFFFFFFu;
    desc.set_break = (m68k_ptr_t)((uintptr_t)mock_set_break - ARCH_HOST_VA_BASE);
    lock_calls = unlock_calls = sb_calls = 0;
}

TEST(raise_clears_active_and_sets_break)
{
    reset();
    /* every other bit of the word survives: 0xF7F6 = all but bits 0 and 3 */
    desc.state = 0xF7F6 | SIO_XMIT_ACTIVE;
    sio_$set_break(&desc, true);
    ASSERT_EQ(0xF7F6 | SIO_STATE_BREAK_ACTIVE, desc.state);
    ASSERT_EQ(0x08, SIO_STATE_BREAK_ACTIVE);      /* bset.b #3,(0x75,A2) */
}

TEST(drop_clears_only_break)
{
    reset();
    desc.state = 0xFFFF;
    sio_$set_break(&desc, 0);
    ASSERT_EQ(0xFFFF & ~SIO_STATE_BREAK_ACTIVE, desc.state);
    ASSERT_EQ(0xFFF7, desc.state);
}

TEST(lock_bracket_then_driver_call)
{
    reset();
    sio_$set_break(&desc, true);
    ASSERT_EQ(1, lock_calls);
    ASSERT_PTR_EQ(&SIO_$SPIN_LOCK, lock_arg);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_PTR_EQ(&SIO_$SPIN_LOCK, unlock_arg);
    ASSERT_EQ(0x4321, unlock_token);
    /* the driver entry runs after the unlock, with (context, enable) */
    ASSERT_EQ(1, sb_calls);
    ASSERT_EQ(1, sb_unlocks_seen);
    ASSERT_EQ(0x0000C0DE, sb_ctx);
    ASSERT_EQ(0xFF, (uint8_t)sb_enable);

    sio_$set_break(&desc, 0);
    ASSERT_EQ(2, sb_calls);
    ASSERT_EQ(0, sb_enable);
}

int main(void)
{
    printf("sio_$set_break tests\n");
    RUN_TEST(raise_clears_active_and_sets_break);
    RUN_TEST(drop_clears_only_break);
    RUN_TEST(lock_bracket_then_driver_call);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
