/*
 * Tests for PROC2_$DETACH_FROM_PARENT (0x00E40DF4).
 *
 * Pinned by the disassembly:
 *   - first-child vs. middle-sibling unlink (0x00E40E20..0x00E40E48);
 *   - a missing parent link crashes with the 0x00E40DF0 cell
 *     (status_$proc2_internal_error) and then FALLS THROUGH into the
 *     zombie test;
 *   - a zombie is unlinked from the allocated list (both shapes) and
 *     pushed onto the free list; the back-link write at 0x00E40E9A is
 *     unconditional;
 *   - a live process just gets flags |= 0x8000 (bset.b #7 on the HIGH byte).
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
uint16_t P2_INFO_ALLOC_PTR;
uint16_t P2_FREE_LIST_HEAD;
uint16_t *PROC2_$PID_TO_INDEX = mock_pid_to_index;
pgroup_entry_t *PGROUP_TABLE = mock_pgroups;
status_$t PROC2_Internal_Error = status_$proc2_internal_error;
int __host_intr_disable_count = 0;

static int n_crash, n_pgroup_cleanup;
static const status_$t *last_crash;
static int16_t last_cleanup_mode;
static proc2_info_t *last_cleanup_entry;

void CRASH_SYSTEM(const status_$t *s) { n_crash++; last_crash = s; }
void PGROUP_CLEANUP_INTERNAL(proc2_info_t *e, int16_t mode)
{ n_pgroup_cleanup++; last_cleanup_entry = e; last_cleanup_mode = mode; }

#include "proc2/detach_from_parent.c"

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
static void reset(void)
{
    memset(mock_entries, 0, sizeof(mock_entries));
    n_crash = n_pgroup_cleanup = 0; last_crash = NULL;
    P2_INFO_ALLOC_PTR = 0; P2_FREE_LIST_HEAD = 0;
}

TEST(first_child_live_becomes_orphan)
{
    E(2)->first_child_idx = 3;
    E(3)->parent_pgroup_idx = 2; E(3)->next_child_sibling = 5; E(3)->flags = 0x0100;
    PROC2_$DETACH_FROM_PARENT(3, 0);
    ASSERT_EQ(E(2)->first_child_idx, 5);
    ASSERT_EQ(E(3)->parent_pgroup_idx, 0);
    ASSERT_EQ(E(3)->flags, 0x8100);
    ASSERT_EQ(n_pgroup_cleanup, 0);
    ASSERT_EQ(n_crash, 0);
}

TEST(middle_sibling_unlink)
{
    E(2)->first_child_idx = 4;
    E(4)->next_child_sibling = 3;
    E(3)->parent_pgroup_idx = 2; E(3)->next_child_sibling = 5;
    PROC2_$DETACH_FROM_PARENT(3, 4);
    ASSERT_EQ(E(2)->first_child_idx, 4);
    ASSERT_EQ(E(4)->next_child_sibling, 5);
    ASSERT_EQ(E(3)->parent_pgroup_idx, 0);
}

TEST(no_parent_crashes_then_continues)
{
    E(3)->parent_pgroup_idx = 0; E(3)->flags = 0;
    PROC2_$DETACH_FROM_PARENT(3, 0);
    ASSERT_EQ(n_crash, 1);
    ASSERT_EQ(*last_crash, status_$proc2_internal_error);
    ASSERT_EQ(E(3)->flags, 0x8000);        /* fell through into the live arm */
}

TEST(zombie_head_of_alloc_list_goes_to_free_list)
{
    E(2)->first_child_idx = 3;
    E(3)->parent_pgroup_idx = 2; E(3)->flags = PROC2_FLAG_ZOMBIE;
    P2_INFO_ALLOC_PTR = 3; E(3)->pad_14 = 0; E(3)->next_index = 6; E(6)->pad_14 = 3;
    P2_FREE_LIST_HEAD = 7;
    PROC2_$DETACH_FROM_PARENT(3, 0);
    ASSERT_EQ(n_pgroup_cleanup, 1);
    ASSERT_EQ(last_cleanup_mode, 1);
    ASSERT_EQ(last_cleanup_entry == E(3), 1);
    ASSERT_EQ(P2_INFO_ALLOC_PTR, 6);
    ASSERT_EQ(E(6)->pad_14, 0);
    ASSERT_EQ(E(3)->next_index, 7);
    ASSERT_EQ(P2_FREE_LIST_HEAD, 3);
    ASSERT_EQ(E(3)->flags, PROC2_FLAG_ZOMBIE);   /* orphan bit NOT set */
}

TEST(zombie_middle_of_alloc_list_unconditional_backlink)
{
    E(2)->first_child_idx = 3;
    E(3)->parent_pgroup_idx = 2; E(3)->flags = PROC2_FLAG_ZOMBIE;
    P2_INFO_ALLOC_PTR = 5; E(5)->next_index = 3;
    E(3)->pad_14 = 5; E(3)->next_index = 0;      /* last on the list */
    mock_entries[0].pad_14 = 0x1234;             /* entry(0) canary */
    PROC2_$DETACH_FROM_PARENT(3, 0);
    ASSERT_EQ(E(5)->next_index, 0);
    ASSERT_EQ(P2_INFO_ALLOC_PTR, 5);
    ASSERT_EQ(mock_entries[0].pad_14, 5);        /* entry(0)+0x14 written */
    ASSERT_EQ(P2_FREE_LIST_HEAD, 3);
}

int main(void)
{
    RUN_TEST(first_child_live_becomes_orphan);
    RUN_TEST(middle_sibling_unlink);
    RUN_TEST(no_parent_crashes_then_continues);
    RUN_TEST(zombie_head_of_alloc_list_goes_to_free_list);
    RUN_TEST(zombie_middle_of_alloc_list_unconditional_backlink);
    printf("%s: %d tests, %d failed\n", __FILE__, tests_run, tests_failed);
    return tests_failed != 0;
}
