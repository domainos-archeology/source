/*
 * Tests for PROC2_$INFO (0x00E407B4) and PROC2_$GET_INFO (0x00E4086E).
 *
 * Pinned by the disassembly:
 *   - PROC2_$INFO scans the allocated list by ASID (0x00E407E4), before
 *     the lock, and falls back to index 0;
 *   - both cap the copy at 0xE4 bytes and copy exactly min(len, 0xE4);
 *   - PROC2_$GET_INFO builds for status ok OR status_$proc2_zombie
 *     (0x00E408B2), passing pid 0 for a zombie (0x00E408CE), and exits
 *     without building on any other status (0x00E4092E).
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
uint16_t *PROC2_$PID_TO_INDEX = mock_pid_to_index;
pgroup_entry_t *PGROUP_TABLE = mock_pgroups;
int __host_intr_disable_count = 0;

static int n_lock, n_unlock, n_build;
static int16_t last_build_idx, last_build_pid;
static status_$t mock_build_status;
static int16_t mock_find_index; static status_$t mock_find_status;

void ML_$LOCK(int16_t id)   { (void)id; n_lock++; }
void ML_$UNLOCK(int16_t id) { (void)id; n_unlock++; }
int16_t PROC2_$FIND_INDEX(uid_t *u, status_$t *s) { (void)u; *s = mock_find_status; return mock_find_index; }
void PROC2_$BUILD_INFO_INTERNAL(int16_t idx, int16_t pid, void *info, status_$t *s)
{
    n_build++; last_build_idx = idx; last_build_pid = pid;
    for (int i = 0; i < 0xE4; i++) ((uint8_t *)info)[i] = (uint8_t)i;
    *s = mock_build_status;
}

#include "proc2/info.c"
#include "proc2/get_info.c"

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
static uint8_t out[0x100];
static void reset(void)
{
    memset(mock_entries, 0, sizeof(mock_entries));
    memset(out, 0xEE, sizeof(out));
    n_lock = n_unlock = n_build = 0; last_build_idx = last_build_pid = -1;
    mock_build_status = 0x77; mock_find_index = 3; mock_find_status = status_$ok;
    P2_INFO_ALLOC_PTR = 2; E(2)->next_index = 3; E(3)->next_index = 0;
    E(2)->asid = 10; E(3)->asid = 11; E(3)->level1_pid = 42;
}

TEST(info_scan_finds_asid_and_copies_capped)
{
    int16_t key = 11, pid = 9; uint16_t len = 0x200; status_$t st = 0;
    PROC2_$INFO(&key, &pid, out, &len, &st);
    ASSERT_EQ(last_build_idx, 3); ASSERT_EQ(last_build_pid, 9);
    ASSERT_EQ(st, 0x77);
    ASSERT_EQ(out[0xE3], 0xE3); ASSERT_EQ(out[0xE4], 0xEE);
    ASSERT_EQ(n_lock, 1); ASSERT_EQ(n_unlock, 1);
}

TEST(info_zero_key_and_short_len)
{
    int16_t key = 0, pid = 1; uint16_t len = 3; status_$t st;
    PROC2_$INFO(&key, &pid, out, &len, &st);
    ASSERT_EQ(last_build_idx, 0);
    ASSERT_EQ(out[2], 2); ASSERT_EQ(out[3], 0xEE);
}

TEST(info_unmatched_asid_yields_index_0)
{
    int16_t key = 99, pid = 1; uint16_t len = 0; status_$t st;
    PROC2_$INFO(&key, &pid, out, &len, &st);
    ASSERT_EQ(last_build_idx, 0);
    ASSERT_EQ(out[0], 0xEE);
}

TEST(get_info_found_uses_level1_pid)
{
    uid_t u = { 1, 2 }; uint16_t len = 0xE4; status_$t st;
    PROC2_$GET_INFO(&u, out, &len, &st);
    ASSERT_EQ(n_build, 1); ASSERT_EQ(last_build_idx, 3); ASSERT_EQ(last_build_pid, 42);
    ASSERT_EQ(st, 0x77); ASSERT_EQ(out[0xE3], 0xE3);
}

TEST(get_info_zombie_passes_pid_0)
{
    uid_t u = { 1, 2 }; uint16_t len = 0xE4; status_$t st;
    mock_find_status = status_$proc2_zombie;
    PROC2_$GET_INFO(&u, out, &len, &st);
    ASSERT_EQ(n_build, 1); ASSERT_EQ(last_build_pid, 0);
}

TEST(get_info_other_error_skips_build)
{
    uid_t u = { 1, 2 }; uint16_t len = 0xE4; status_$t st = 0;
    mock_find_status = status_$proc2_uid_not_found;
    PROC2_$GET_INFO(&u, out, &len, &st);
    ASSERT_EQ(n_build, 0); ASSERT_EQ(st, status_$proc2_uid_not_found);
    ASSERT_EQ(out[0], 0xEE); ASSERT_EQ(n_unlock, 1);
}

int main(void)
{
    RUN_TEST(info_scan_finds_asid_and_copies_capped);
    RUN_TEST(info_zero_key_and_short_len);
    RUN_TEST(info_unmatched_asid_yields_index_0);
    RUN_TEST(get_info_found_uses_level1_pid);
    RUN_TEST(get_info_zombie_passes_pid_0);
    RUN_TEST(get_info_other_error_skips_build);
    printf("%s: %d tests, %d failed\n", __FILE__, tests_run, tests_failed);
    return tests_failed != 0;
}
