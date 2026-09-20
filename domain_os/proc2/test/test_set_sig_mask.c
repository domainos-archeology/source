/*
 * Tests for PROC2_$SET_SIG_MASK (0x00E3F7DE).
 *
 * Pinned by the disassembly: the six mask updates and which field gets
 * no set half (+0x80); the flag bits driven by BYTES 0 and 1 of
 * clear[7]/set[7] (0x0400 and 0x0004); clear[6] gating the +0x8C store;
 * the +0x18 delta walk detaching children whose +0x1A exceeds the new
 * value; the bit-17 zombie walk; the deliver condition; the two results.
 */

#include <stdio.h>
#include <string.h>

#include "base/base.h"
#include "proc2/proc2_internal.h"

#define MOCK_ENTRIES 8
static proc2_info_t mock_entries[MOCK_ENTRIES + 1];
static uint16_t mock_pid_to_index[64];
static pgroup_entry_t mock_pgroups[PGROUP_TABLE_SIZE];
proc2_info_t *P2_INFO_TABLE = &mock_entries[1];
uint16_t *PROC2_$PID_TO_INDEX = mock_pid_to_index;
pgroup_entry_t *PGROUP_TABLE = mock_pgroups;
uint16_t PROC1_$CURRENT;
int __host_intr_disable_count = 0;

static int n_lock, n_unlock, n_detach, n_deliver;
static int16_t detach_child[4], detach_prev[4], last_deliver;
void ML_$LOCK(int16_t id)   { (void)id; n_lock++; }
void ML_$UNLOCK(int16_t id) { (void)id; n_unlock++; }
void PROC2_$DETACH_FROM_PARENT(int16_t c, int16_t p)
{ if (n_detach < 4) { detach_child[n_detach] = c; detach_prev[n_detach] = p; } n_detach++; }
void PROC2_$DELIVER_PENDING_INTERNAL(int16_t i) { n_deliver++; last_deliver = i; }

#include "proc2/set_sig_mask.c"

static int tests_run, tests_failed;
#define TEST(name) static void name(void)
#define RUN_TEST(name) do { tests_run++; reset(); name(); } while (0)
#define ASSERT_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        printf("  FAIL %s:%d: %s == %lld, expected %s == %lld\n", \
               __FILE__, __LINE__, #a, _a, #b, _b); \
        tests_failed++; return; } } while (0)

static proc2_info_t *E(int i) { return P2_INFO_ENTRY(i); }
static uint32_t clr[8], set[8], res[2];
static int16_t delta;
static void reset(void)
{
    memset(mock_entries, 0, sizeof(mock_entries));
    memset(clr, 0, sizeof(clr)); memset(set, 0, sizeof(set)); memset(res, 0xEE, sizeof(res));
    n_lock = n_unlock = n_detach = n_deliver = 0; delta = 0;
    PROC1_$CURRENT = 5; mock_pid_to_index[5] = 2;
    E(2)->self_index = 2;
}

TEST(mask_updates_and_flag_bytes)
{
    E(2)->sig_blocked_1 = 0xF0F0F0F0u; E(2)->sig_blocked_2 = 0xF0F0F0F0u;
    E(2)->sig_pending = 0xF0F0F0F0u; E(2)->sig_mask_1 = 0xF0F0F0F0u;
    E(2)->sig_mask_2 = 0xF0F0F0F0u; E(2)->sig_mask_3 = 0xF0F0F0F0u; E(2)->sig_mask_4 = 7;
    E(2)->flags = 0x0404;
    for (int i = 0; i < 6; i++) { clr[i] = 0xFF000000u; set[i] = 0x00000001u + (uint32_t)i; }
    clr[6] = 0; set[6] = 0x12345678u;
    clr[7] = 0x80800000u;                       /* bytes 0 and 1 negative */
    PROC2_$SET_SIG_MASK(&delta, clr, set, res);
    ASSERT_EQ(E(2)->sig_blocked_1, 0x00F0F0F1u);
    ASSERT_EQ(E(2)->sig_blocked_2, 0x00F0F0F2u);
    ASSERT_EQ(E(2)->sig_pending,   0x00F0F0F3u);
    ASSERT_EQ(E(2)->sig_mask_1,    0x00F0F0F4u);
    ASSERT_EQ(E(2)->sig_mask_2,    0x00F0F0F0u);     /* clear only */
    ASSERT_EQ(E(2)->sig_mask_3,    0x00F0F0F6u);
    ASSERT_EQ(E(2)->sig_mask_4, 7);                  /* clr[6] == 0: untouched */
    ASSERT_EQ(E(2)->flags, 0x0000);
    ASSERT_EQ(res[0], 0x00F0F0F2u); ASSERT_EQ(res[1], 0);
    set[7] = 0x80800000u; clr[6] = 1;
    PROC2_$SET_SIG_MASK(&delta, clr, set, res);
    ASSERT_EQ(E(2)->flags, 0x0404); ASSERT_EQ(E(2)->sig_mask_4, 0x12345678u);
    ASSERT_EQ(res[1], 1);
    ASSERT_EQ(n_deliver, 0);                         /* +0x80 & ~+0x78 == 0 */
    ASSERT_EQ(n_lock, 2); ASSERT_EQ(n_unlock, 2);
}

TEST(delta_walk_detaches_children)
{
    E(2)->pad_18[0] = 10; E(2)->first_child_idx = 3;
    E(3)->pad_18[1] = 9; E(3)->next_child_sibling = 4;
    E(4)->pad_18[1] = 5; E(4)->next_child_sibling = 6;
    E(6)->pad_18[1] = 8; E(6)->next_child_sibling = 0;
    delta = -3;                                      /* 10 -> 7 */
    PROC2_$SET_SIG_MASK(&delta, clr, set, res);
    ASSERT_EQ(E(2)->pad_18[0], 7);
    ASSERT_EQ(n_detach, 2);
    ASSERT_EQ(detach_child[0], 3); ASSERT_EQ(detach_prev[0], 0);
    ASSERT_EQ(detach_child[1], 6); ASSERT_EQ(detach_prev[1], 4);
    delta = 5;                                       /* increase: no walk */
    PROC2_$SET_SIG_MASK(&delta, clr, set, res);
    ASSERT_EQ(E(2)->pad_18[0], 12); ASSERT_EQ(n_detach, 2);
    delta = -12;                                     /* to 0: no walk */
    PROC2_$SET_SIG_MASK(&delta, clr, set, res);
    ASSERT_EQ(E(2)->pad_18[0], 0); ASSERT_EQ(n_detach, 2);
}

TEST(bit17_zombie_walk_and_deliver)
{
    E(2)->pad_18[0] = 4; E(2)->first_child_idx = 3;
    E(3)->flags = PROC2_FLAG_ZOMBIE; E(3)->pad_18[1] = 4; E(3)->next_child_sibling = 4;
    E(4)->flags = PROC2_FLAG_ZOMBIE; E(4)->pad_18[1] = 4; E(4)->next_child_sibling = 0;
    E(2)->sig_pending = 0x00020000u;                 /* triggers the walk */
    PROC2_$SET_SIG_MASK(&delta, clr, set, res);      /* +0x74 bit 17 clear -> set +0x80, stop */
    ASSERT_EQ(n_detach, 0); ASSERT_EQ(E(2)->sig_mask_2, 0x00020000u);
    ASSERT_EQ(n_deliver, 1); ASSERT_EQ(last_deliver, 2);
    E(2)->sig_mask_2 = 0; E(2)->sig_blocked_1 = 0x00020000u; E(2)->sig_blocked_2 = 0;
    PROC2_$SET_SIG_MASK(&delta, clr, set, res);      /* reap both zombies */
    ASSERT_EQ(n_detach, 2); ASSERT_EQ(detach_child[1], 4); ASSERT_EQ(detach_prev[1], 0);
    ASSERT_EQ(n_deliver, 1);
    E(2)->sig_mask_2 = 0x00020000u; E(2)->sig_blocked_2 = 0x00020000u;
    PROC2_$SET_SIG_MASK(&delta, clr, set, res);      /* bit 17 already set: no walk, no deliver */
    ASSERT_EQ(n_detach, 2); ASSERT_EQ(n_deliver, 1);
}

int main(void)
{
    RUN_TEST(mask_updates_and_flag_bytes);
    RUN_TEST(delta_walk_detaches_children);
    RUN_TEST(bit17_zombie_walk_and_deliver);
    printf("%s: %d tests, %d failed\n", __FILE__, tests_run, tests_failed);
    return tests_failed != 0;
}
