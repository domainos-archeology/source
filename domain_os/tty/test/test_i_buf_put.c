/*
 * tty/test/test_i_buf_put.c - Unit tests for tty_$i_buf_put (0x00E1AF42).
 * Pins the lock/unlock bracket around every call (including the drop path),
 * the data[tail-1] placement, the 0x100 -> 1 wrap and the full-buffer drop.
 */
#include "tty/tty_internal.h"
#include "test_harness.h"

uint32_t TTY_$SPIN_LOCK;

static int lock_depth;
static int lock_calls;
static void *lock_ptr_seen;
static void *unlock_ptr_seen;
static unsigned long unlock_token_seen;
ml_$spin_token_t ML_$SPIN_LOCK(void *lockp)
{
    lock_ptr_seen = lockp;
    lock_depth++;
    lock_calls++;
    return (ml_$spin_token_t)0x2700;
}
void (ML_$SPIN_UNLOCK)(void *lockp, uint32_t token_slot)
{
    ml_$spin_token_t token = (ml_$spin_token_t)ARCH_PASCAL_SLOT_WORD(token_slot); (void)token;
    unlock_ptr_seen = lockp;
    unlock_token_seen = (unsigned long)token;
    lock_depth--;
}

#include "../i_buf_put.c"

static struct { uint16_t head, tail, size; uint8_t data[256]; } __attribute__((packed)) buf;

static void reset(uint16_t head, uint16_t tail)
{
    memset(&buf, 0, sizeof(buf));
    buf.head = head; buf.tail = tail; buf.size = 0x100;
    lock_depth = 0; lock_calls = 0;
}

TEST(stores_at_tail_minus_one)
{
    reset(1, 1);
    tty_$i_buf_put('A', &buf);
    ASSERT_EQ('A', buf.data[0]);
    ASSERT_EQ(2, buf.tail);
    ASSERT_EQ(1, buf.head);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(0, lock_depth);
    ASSERT_EQ((unsigned long)&TTY_$SPIN_LOCK, (unsigned long)lock_ptr_seen);
    ASSERT_EQ((unsigned long)&TTY_$SPIN_LOCK, (unsigned long)unlock_ptr_seen);
    ASSERT_EQ(0x2700, unlock_token_seen);
}

TEST(wraps_at_256)
{
    reset(5, 0x100);
    tty_$i_buf_put('Z', &buf);
    ASSERT_EQ('Z', buf.data[0xFF]);
    ASSERT_EQ(1, buf.tail);
}

TEST(drops_when_full)
{
    reset(3, 2);
    tty_$i_buf_put('Q', &buf);
    ASSERT_EQ(0, buf.data[1]);
    ASSERT_EQ(2, buf.tail);
    ASSERT_EQ(1, lock_calls);
    ASSERT_EQ(0, lock_depth);

    reset(1, 0x100);           /* wrap lands on head */
    tty_$i_buf_put('Q', &buf);
    ASSERT_EQ(0, buf.data[0xFF]);
    ASSERT_EQ(0x100, buf.tail);
}

int main(void)
{
    printf("tty_$i_buf_put tests\n");
    RUN_TEST(stores_at_tail_minus_one);
    RUN_TEST(wraps_at_256);
    RUN_TEST(drops_when_full);
    TEST_SUMMARY();
}
