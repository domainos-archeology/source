/*
 * Tests for PROC2_$PGUID_TO_UPGID (0x00E41072 + helper 0x00E422CC),
 * PROC2_$RESUME (0x00E413DA) and PROC2_$QUIT (0x00E3F130).
 *
 * Pinned by the disassembly: a UID whose first byte is zero yields the
 * low word of uid.high; otherwise the process's own group upgid (0 when
 * not found or in no group).  RESUME tests the LOW word of the status
 * and maps 0xA0003 to 0x190006, everything else gets bit 31.  QUIT hands
 * PROC2_$SIGNAL the 0x00E3F158/0x00E3F15C cells (3 and 0x00120010).
 */

#include <stdio.h>
#include <string.h>

#include "base/base.h"
#include "proc2/proc2_internal.h"

MODULE_DATA_DEFINE(proc2_$data_t, PROC2_$DATA, 0x00EA551C);
int __host_intr_disable_count = 0;

static int n_lock, n_unlock, n_resume, n_signal;
static int16_t mock_find_index; static status_$t mock_find_status, mock_resume_status;
static int16_t last_sig; static uint32_t last_param; static uint16_t last_resume_pid;
void ML_$LOCK(int16_t id)   { (void)id; n_lock++; }
void ML_$UNLOCK(int16_t id) { (void)id; n_unlock++; }
int16_t PROC2_$FIND_INDEX(uid_t *u, status_$t *s) { (void)u; *s = mock_find_status; return mock_find_index; }
void PROC1_$RESUME(uint16_t pid, status_$t *s) { n_resume++; last_resume_pid = pid; *s = mock_resume_status; }
void PROC2_$SIGNAL(uid_t *u, int16_t *sig, uint32_t *p, status_$t *s) { (void)u; n_signal++; last_sig = *sig; last_param = *p; *s = 0x77; }

#include "proc2/pguid_to_upgid.c"
#include "proc2/resume.c"
#include "proc2/quit.c"

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
    memset(PROC2_$DATA.info, 0, sizeof(PROC2_$DATA.info));
    memset(PROC2_$DATA.pgroup, 0, sizeof(PROC2_$DATA.pgroup));
    n_lock = n_unlock = n_resume = n_signal = 0;
    mock_find_index = 3; mock_find_status = status_$ok; mock_resume_status = status_$ok;
}

TEST(pguid_synthetic_uid)
{
    uid_t u = { 0x00001234u, 0xFFFFFFFFu }; uint16_t r = 0; status_$t st = 1;
    PROC2_$PGUID_TO_UPGID(&u, &r, &st);
    ASSERT_EQ(r, 0x1234); ASSERT_EQ(st, status_$ok); ASSERT_EQ(n_lock, 1); ASSERT_EQ(n_unlock, 1);
}

TEST(pguid_real_uid_paths)
{
    uid_t u = { 0x11001234u, 1 }; uint16_t r = 9; status_$t st;
    E(3)->pgroup_table_idx = 4; PROC2_$DATA.pgroup[4].upgid = 400;
    PROC2_$PGUID_TO_UPGID(&u, &r, &st);
    ASSERT_EQ(r, 400);
    E(3)->pgroup_table_idx = 0;
    PROC2_$PGUID_TO_UPGID(&u, &r, &st);
    ASSERT_EQ(r, 0);
    E(3)->pgroup_table_idx = 4; mock_find_status = status_$proc2_uid_not_found;
    PROC2_$PGUID_TO_UPGID(&u, &r, &st);
    ASSERT_EQ(r, 0); ASSERT_EQ(st, status_$ok);
}

TEST(resume_paths)
{
    uid_t u = { 1, 2 }; status_$t st = 1;
    E(3)->level1_pid = 17;
    PROC2_$RESUME(&u, &st);
    ASSERT_EQ(st, status_$ok); ASSERT_EQ(n_resume, 1); ASSERT_EQ(last_resume_pid, 17);
    mock_resume_status = status_$process_not_suspended;
    PROC2_$RESUME(&u, &st);
    ASSERT_EQ(st, status_$proc2_not_suspended);
    mock_resume_status = 0x000A0001;
    PROC2_$RESUME(&u, &st);
    ASSERT_EQ((uint32_t)st, 0x800A0001u);
    mock_resume_status = 0x00010000;          /* low word zero: passes as ok */
    PROC2_$RESUME(&u, &st);
    ASSERT_EQ(st, 0x00010000);
    mock_find_status = status_$proc2_uid_not_found;
    PROC2_$RESUME(&u, &st);
    ASSERT_EQ(st, status_$proc2_uid_not_found); ASSERT_EQ(n_resume, 4);
    ASSERT_EQ(n_lock, n_unlock);
}

TEST(quit_cells)
{
    uid_t u = { 1, 2 }; status_$t st = 0;
    PROC2_$QUIT(&u, &st);
    ASSERT_EQ(n_signal, 1); ASSERT_EQ(last_sig, 3); ASSERT_EQ(last_param, 0x00120010u); ASSERT_EQ(st, 0x77);
}

int main(void)
{
    RUN_TEST(pguid_synthetic_uid);
    RUN_TEST(pguid_real_uid_paths);
    RUN_TEST(resume_paths);
    RUN_TEST(quit_cells);
    printf("%s: %d tests, %d failed\n", __FILE__, tests_run, tests_failed);
    return tests_failed != 0;
}
