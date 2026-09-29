/*
 * Tests for PROC2_$LIST (0x00E402F0), PROC2_$LIST2 (0x00E40402),
 * PROC2_$LIST_PGROUP (0x00E401EA) and PROC2_$NAME_TO_UID (0x00E3EAD0).
 *
 * Pinned by the disassembly:
 *   - LIST's filter is flags bit 0x0080 (tst.b low byte / bpl) and
 *     asid != 1, and the count starts at 1 for entry 1's UID;
 *   - LIST2 scans slots 1..57 for flags 0x0100 & 0x0080, applies
 *     *start_index, sets *more_flag = 0xFF and *last_index = last copied
 *     slot + 1 only when a qualifying slot follows the last copied one;
 *   - LIST_PGROUP filters on 0x0080 and entry+0x10;
 *   - every count is clipped to min(found, min(*max, 57));
 *   - the FIM failure path pops, unlocks and reports 0;
 *   - NAME_TO_UID rejects len < 0 or > 32 before locking, matches an empty
 *     name, and reports uid_not_found otherwise.
 */

#include <stdio.h>
#include <string.h>

#include "base/base.h"
#include "proc2/proc2_internal.h"

#define MOCK_ENTRIES 60
MODULE_DATA_DEFINE(proc2_$unwired_data_t, PROC2_$UNWIRED_DATA, 0x00E7BE84);
MODULE_DATA_DEFINE(proc2_$data_t, PROC2_$DATA, 0x00EA551C);
int __host_intr_disable_count = 0;

static int n_lock, n_unlock, n_rls, n_pop;
static status_$t mock_cleanup_status;
static int16_t mock_pgroup_idx;

void ML_$LOCK(int16_t id)   { (void)id; n_lock++; }
void ML_$UNLOCK(int16_t id) { (void)id; n_unlock++; }
status_$t FIM_$CLEANUP(void *h) { (void)h; return mock_cleanup_status; }
void FIM_$RLS_CLEANUP(void *h) { (void)h; n_rls++; }
void FIM_$POP_SIGNAL(void *h) { (void)h; n_pop++; }
int16_t PROC2_$UID_TO_PGROUP_INDEX(uid_t *u) { (void)u; return mock_pgroup_idx; }

#include "proc2/list.c"
#include "proc2/list2.c"
#include "proc2/list_pgroup.c"
#include "proc2/name_to_uid.c"

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
static uid_t out[64];
static void reset(void)
{
    memset(PROC2_$DATA.info, 0, sizeof(PROC2_$DATA.info));
    memset(PROC2_$DATA.pgroup, 0, sizeof(PROC2_$DATA.pgroup));
    memset(out, 0xEE, sizeof(out));
    n_lock = n_unlock = n_rls = n_pop = 0;
    mock_cleanup_status = status_$cleanup_handler_set;
    mock_pgroup_idx = 0;
    for (int i = 1; i <= MOCK_ENTRIES; i++) { E(i)->uid.high = 0x100 + i; E(i)->uid.low = i; }
    /* allocated list: 1 -> 3 -> 5 -> 7 */
    PROC2_$UNWIRED_DATA.info_alloc_ptr = 1; E(1)->next_index = 3; E(3)->next_index = 5; E(5)->next_index = 7; E(7)->next_index = 0;
    E(1)->asid = 1; E(1)->flags = 0x0080;
    E(3)->asid = 3; E(3)->flags = 0x0080;
    E(5)->asid = 5; E(5)->flags = 0x8000;    /* high bit only: NOT listed */
    E(7)->asid = 7; E(7)->flags = 0x0180;
}

TEST(list_filters_low_byte_bit7_and_asid1)
{
    uint16_t max = 10, cnt = 0;
    PROC2_$LIST(out, &max, &cnt);
    ASSERT_EQ(cnt, 3);
    ASSERT_EQ(out[0].high, 0x101); ASSERT_EQ(out[1].high, 0x103); ASSERT_EQ(out[2].high, 0x107);
    ASSERT_EQ(out[3].high, 0xEEEEEEEEu);
    ASSERT_EQ(n_lock, 1); ASSERT_EQ(n_unlock, 1); ASSERT_EQ(n_rls, 1);
}

TEST(list_clips_count_to_max)
{
    uint16_t max = 2, cnt = 0;
    PROC2_$LIST(out, &max, &cnt);
    ASSERT_EQ(cnt, 2);
    ASSERT_EQ(out[1].high, 0x103); ASSERT_EQ(out[2].high, 0xEEEEEEEEu);
}

TEST(list_max_zero_writes_nothing)
{
    uint16_t max = 0, cnt = 0;
    PROC2_$LIST(out, &max, &cnt);
    ASSERT_EQ(cnt, 0);
    ASSERT_EQ(out[0].high, 0xEEEEEEEEu);
}

TEST(list_cleanup_failure)
{
    uint16_t max = 10, cnt = 5;
    mock_cleanup_status = 0x00120001;
    PROC2_$LIST(out, &max, &cnt);
    ASSERT_EQ(cnt, 0); ASSERT_EQ(n_pop, 1); ASSERT_EQ(n_lock, 0); ASSERT_EQ(n_unlock, 1);
}

TEST(list2_scans_slots_with_start_and_more)
{
    uint16_t max = 2, cnt = 0; int32_t start = 3, last = 99; int8_t more = 0x55;
    E(1)->flags = 0x0180; E(3)->flags = 0x0180; E(5)->flags = 0x0180; E(7)->flags = 0x0180;
    E(57)->flags = 0x0180; E(58)->flags = 0x0180;   /* 58 is beyond the scan */
    PROC2_$LIST2(out, &max, &cnt, &start, &more, &last);
    ASSERT_EQ(cnt, 2);
    ASSERT_EQ(out[0].high, 0x103); ASSERT_EQ(out[1].high, 0x105); ASSERT_EQ(out[2].high, 0xEEEEEEEEu);
    ASSERT_EQ(more, (int8_t)0xFF);
    ASSERT_EQ(last, 6);              /* last copied slot 5, + 1 */
}

TEST(list2_no_more_when_everything_fits)
{
    uint16_t max = 10, cnt = 0; int32_t start = 1, last = 99; int8_t more = 0x55;
    E(1)->flags = 0x0180; E(3)->flags = 0x0180;   /* plus E(7) from reset() */
    PROC2_$LIST2(out, &max, &cnt, &start, &more, &last);
    ASSERT_EQ(cnt, 3); ASSERT_EQ(more, 0); ASSERT_EQ(last, 0);
}

TEST(list2_start_beyond_all_still_reports_more)
{
    uint16_t max = 10, cnt = 0; int32_t start = 50, last = 99; int8_t more = 0;
    E(3)->flags = 0x0180;
    PROC2_$LIST2(out, &max, &cnt, &start, &more, &last);
    ASSERT_EQ(cnt, 0); ASSERT_EQ(more, (int8_t)0xFF); ASSERT_EQ(last, 1);
}

TEST(list_pgroup_members)
{
    uint16_t max = 10, cnt = 0; uid_t g = { 1, 2 };
    mock_pgroup_idx = 4;
    E(3)->pgroup_table_idx = 4; E(5)->pgroup_table_idx = 4; E(7)->pgroup_table_idx = 4;
    PROC2_$LIST_PGROUP(&g, out, &max, &cnt);
    ASSERT_EQ(cnt, 2);                                  /* 5 lacks 0x0080 */
    ASSERT_EQ(out[0].high, 0x103); ASSERT_EQ(out[1].high, 0x107);
}

TEST(list_pgroup_unknown_group)
{
    uint16_t max = 10, cnt = 7; uid_t g = { 1, 2 };
    PROC2_$LIST_PGROUP(&g, out, &max, &cnt);
    ASSERT_EQ(cnt, 0); ASSERT_EQ(n_unlock, 1); ASSERT_EQ(n_rls, 1);
}

TEST(name_to_uid_bounds_match_and_miss)
{
    uid_t u = { 0, 0 }; status_$t st = 0; int16_t len;
    len = 33; PROC2_$NAME_TO_UID("x", &len, &u, &st);
    ASSERT_EQ(st, status_$proc2_invalid_process_name); ASSERT_EQ(n_lock, 0);
    len = -1; PROC2_$NAME_TO_UID("x", &len, &u, &st);
    ASSERT_EQ(st, status_$proc2_invalid_process_name);
    memcpy(E(5)->name, "shell", 5); E(5)->name_len = 5;
    len = 5; st = 1; PROC2_$NAME_TO_UID("shell", &len, &u, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(u.high, 0x105);
    len = 5; PROC2_$NAME_TO_UID("shelx", &len, &u, &st);
    ASSERT_EQ(st, status_$proc2_uid_not_found);
    /* an empty name matches the first allocated entry with name_len 0 (entry 1) */
    len = 0; PROC2_$NAME_TO_UID("", &len, &u, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(u.high, 0x101);
    ASSERT_EQ(n_lock, n_unlock);
}

int main(void)
{
    RUN_TEST(list_filters_low_byte_bit7_and_asid1);
    RUN_TEST(list_clips_count_to_max);
    RUN_TEST(list_max_zero_writes_nothing);
    RUN_TEST(list_cleanup_failure);
    RUN_TEST(list2_scans_slots_with_start_and_more);
    RUN_TEST(list2_no_more_when_everything_fits);
    RUN_TEST(list2_start_beyond_all_still_reports_more);
    RUN_TEST(list_pgroup_members);
    RUN_TEST(list_pgroup_unknown_group);
    RUN_TEST(name_to_uid_bounds_match_and_miss);
    printf("%s: %d tests, %d failed\n", __FILE__, tests_run, tests_failed);
    return tests_failed != 0;
}
