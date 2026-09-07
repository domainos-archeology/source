/*
 * Tests for PROC2_$INIT_ENTRY_INTERNAL (0x00E732E4).
 *
 * The real proc2/init_entry_internal.c and proc2/pgroup_inherit_internal.c
 * are #included below and driven through mocked UID_$ / FIM_$ / XPD_$ /
 * PGROUP_ dependencies over a mock P2 info table.
 *
 * The headline case is bead source-e8c8: entry+0x1C (proc2_info_t.self_index)
 * is the slot's own table index, stamped once by PROC2_$INIT at 0x00E304CA,
 * and this routine must leave it alone when a slot is recycled.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "base/base.h"
#include "proc2/proc2_internal.h"

/* ------------------------------------------------------------------ */
/* Mock storage                                                        */
/* ------------------------------------------------------------------ */

#define MOCK_ENTRIES 8

/* slot 0 is the out-of-band "index 0" entry the table is biased against */
static proc2_info_t mock_entries[MOCK_ENTRIES + 1];
static uint16_t mock_pid_to_index[64];
static pgroup_entry_t mock_pgroups[PGROUP_TABLE_SIZE];

proc2_info_t *P2_INFO_TABLE = &mock_entries[1];
uint16_t P2_INFO_ALLOC_PTR;
uint16_t P2_FREE_LIST_HEAD;
uint16_t *P2_PID_TO_INDEX_TABLE = mock_pid_to_index;
pgroup_entry_t *PGROUP_TABLE = mock_pgroups;
uint16_t PROC2_$NEXT_UPID = P2_UPID_WRAP_TO;

uid_t PROC2_UID[PROC2_UID_TABLE_SIZE];

uint16_t PROC1_$CURRENT;

/* ------------------------------------------------------------------ */
/* Mock control / trace                                                */
/* ------------------------------------------------------------------ */

#define CREATOR_IDX 2
#define NEW_IDX     4

static int n_uid_gen, n_init_pid, n_reset_opts, n_pgroup_set, n_find_by_upgid;
static int16_t *last_init_pid_arg;
static xpd_$ptrace_opts_t *last_reset_opts_arg;
static proc2_info_t *last_pgroup_set_entry;
static uint16_t last_pgroup_set_upgid;
static uint16_t last_find_by_upgid_arg;
static uint16_t mock_conflict_upgid;
static int16_t mock_conflict_pgroup;

static proc2_info_t *creator(void) { return P2_INFO_ENTRY(CREATOR_IDX); }
static proc2_info_t *fresh(void)   { return P2_INFO_ENTRY(NEW_IDX); }

static void reset_mocks(void)
{
    memset(mock_entries, 0, sizeof(mock_entries));
    memset(mock_pid_to_index, 0, sizeof(mock_pid_to_index));
    memset(mock_pgroups, 0, sizeof(mock_pgroups));
    memset(PROC2_UID, 0, sizeof(PROC2_UID));

    P2_INFO_ALLOC_PTR = 0;
    P2_FREE_LIST_HEAD = 0;
    PROC2_$NEXT_UPID = P2_UPID_WRAP_TO;

    PROC1_$CURRENT = 5;
    mock_pid_to_index[5] = CREATOR_IDX;

    n_uid_gen = n_init_pid = n_reset_opts = n_pgroup_set = n_find_by_upgid = 0;
    last_init_pid_arg = NULL;
    last_reset_opts_arg = NULL;
    last_pgroup_set_entry = NULL;
    last_pgroup_set_upgid = 0;
    last_find_by_upgid_arg = 0;
    mock_conflict_upgid = 0;
    mock_conflict_pgroup = 0;
}

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

void UID_$GEN(uid_t *uid_ret)
{
    n_uid_gen++;
    uid_ret->high = 0x11223344u;
    uid_ret->low = 0x55667788u;
}

void FIM_$INIT_PID(int16_t *pid)
{
    n_init_pid++;
    last_init_pid_arg = pid;
}

void XPD_$RESET_PTRACE_OPTS(xpd_$ptrace_opts_t *opts)
{
    int i;
    n_reset_opts++;
    last_reset_opts_arg = opts;
    for (i = 0; i < 14; i++) {
        ((uint8_t *)opts)[i] = (uint8_t)(0xE0 + i);
    }
}

/* Only one nominated candidate maps to an existing group. */
int16_t PGROUP_FIND_BY_UPGID(uint16_t upgid)
{
    n_find_by_upgid++;
    last_find_by_upgid_arg = upgid;
    if (mock_conflict_pgroup != 0 && upgid == mock_conflict_upgid) {
        return mock_conflict_pgroup;
    }
    return 0;
}

void PGROUP_SET_INTERNAL(proc2_info_t *entry, uint16_t new_upgid,
                         status_$t *status_ret)
{
    n_pgroup_set++;
    last_pgroup_set_entry = entry;
    last_pgroup_set_upgid = new_upgid;
    *status_ret = status_$ok;
}

/* ------------------------------------------------------------------ */
/* Code under test                                                     */
/* ------------------------------------------------------------------ */

#include "proc2/pgroup_inherit_internal.c"
#include "proc2/init_entry_internal.c"

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

/* A creator entry plus a recycled slot with plausible stale contents. */
static void setup_table(void)
{
    reset_mocks();

    P2_INFO_ALLOC_PTR = 0;          /* nothing allocated -> no UPID conflicts */

    creator()->self_index = CREATOR_IDX;
    creator()->session_id = 0x0777;
    creator()->pgroup_table_idx = 0;
    creator()->flags = 0;

    fresh()->self_index = NEW_IDX;
    fresh()->asid = 3;
    fresh()->flags = 0;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/*
 * bead source-e8c8.  0x00E732E4..0x00E73452 contains no write to entry+0x1C:
 * the slot's own table index, stamped by PROC2_$INIT at 0x00E304CA, must
 * survive reuse of the slot.
 */
static void test_self_index_is_untouched(void)
{
    setup_table();

    fresh()->self_index = NEW_IDX;
    PROC2_$INIT_ENTRY_INTERNAL(fresh());
    assert(fresh()->self_index == NEW_IDX);

    /* An out-of-band value survives just as well -- nothing writes it. */
    setup_table();
    fresh()->self_index = 0xBEEFu;
    PROC2_$INIT_ENTRY_INTERNAL(fresh());
    assert(fresh()->self_index == 0xBEEFu);

    printf("test_self_index_is_untouched: PASSED\n");
}

/* 0x00E732F0 / 0x00E73308 / 0x00E73310 */
static void test_uid_publication_and_fim_init(void)
{
    setup_table();
    fresh()->asid = 3;

    PROC2_$INIT_ENTRY_INTERNAL(fresh());

    assert(n_uid_gen == 1);
    assert(fresh()->uid.high == 0x11223344u);
    assert(fresh()->uid.low == 0x55667788u);

    /* PROC2_UID is indexed by the ASID, not by the table index */
    assert(PROC2_UID[3].high == 0x11223344u);
    assert(PROC2_UID[3].low == 0x55667788u);
    assert(PROC2_UID[NEW_IDX].high == 0);

    /* 0x00E73310 passes the ADDRESS of entry->asid */
    assert(n_init_pid == 1);
    assert(last_init_pid_arg == (int16_t *)&fresh()->asid);

    printf("test_uid_publication_and_fim_init: PASSED\n");
}

/* 0x00E73330 / 0x00E7333C / 0x00E73344 */
static void test_upid_counter_advance_and_wrap(void)
{
    setup_table();
    PROC2_$NEXT_UPID = 100;

    PROC2_$INIT_ENTRY_INTERNAL(fresh());
    assert(fresh()->upid == 100);
    assert(PROC2_$NEXT_UPID == 101);

    setup_table();
    PROC2_$NEXT_UPID = P2_UPID_WRAP_AT;         /* 30000 */
    PROC2_$INIT_ENTRY_INTERNAL(fresh());
    /* The candidate is the pre-wrap value; the counter resets to 0x41 */
    assert(fresh()->upid == P2_UPID_WRAP_AT);
    assert(PROC2_$NEXT_UPID == P2_UPID_WRAP_TO);

    printf("test_upid_counter_advance_and_wrap: PASSED\n");
}

/* 0x00E7336C: a live entry's UPID (+0x16) rejects the candidate */
static void test_upid_conflicts_with_live_upid(void)
{
    setup_table();
    PROC2_$NEXT_UPID = 100;

    P2_INFO_ALLOC_PTR = CREATOR_IDX;
    creator()->next_index = 0;
    creator()->upid = 100;

    PROC2_$INIT_ENTRY_INTERNAL(fresh());

    assert(fresh()->upid == 101);
    assert(PROC2_$NEXT_UPID == 102);

    printf("test_upid_conflicts_with_live_upid: PASSED\n");
}

/* 0x00E73372: a live entry's session id (+0x5C) rejects the candidate too */
static void test_upid_conflicts_with_session_id(void)
{
    setup_table();
    PROC2_$NEXT_UPID = 200;

    P2_INFO_ALLOC_PTR = CREATOR_IDX;
    creator()->next_index = 0;
    creator()->upid = 0;
    creator()->session_id = 200;

    PROC2_$INIT_ENTRY_INTERNAL(fresh());

    assert(fresh()->upid == 201);
    /* the new entry still inherits the creator's session id */
    assert(fresh()->session_id == 200);

    printf("test_upid_conflicts_with_session_id: PASSED\n");
}

/*
 * 0x00E73378-0x00E73380: when the candidate names an existing process group,
 * an entry carrying that group's index (+0x10) rejects it.  A pgroup index of
 * 0 short-circuits the test.
 */
static void test_upid_conflicts_with_pgroup(void)
{
    setup_table();
    PROC2_$NEXT_UPID = 300;

    P2_INFO_ALLOC_PTR = CREATOR_IDX;
    creator()->next_index = 0;
    creator()->upid = 0;
    creator()->session_id = 0;
    creator()->pgroup_table_idx = 6;

    /* candidate 300 names group 6, which the creator is in -> rejected */
    mock_conflict_upgid = 300;
    mock_conflict_pgroup = 6;

    PROC2_$INIT_ENTRY_INTERNAL(fresh());

    assert(n_find_by_upgid == 2);
    assert(fresh()->upid == 301);

    /*
     * A pgroup index of 0 from the lookup short-circuits the test
     * (0x00E73378 tst.w D0w), so a candidate that names no group is taken
     * even though the creator carries a non-zero pgroup index.
     */
    setup_table();
    PROC2_$NEXT_UPID = 300;
    P2_INFO_ALLOC_PTR = CREATOR_IDX;
    creator()->next_index = 0;
    creator()->upid = 0;
    creator()->session_id = 0;
    creator()->pgroup_table_idx = 6;

    PROC2_$INIT_ENTRY_INTERNAL(fresh());

    assert(n_find_by_upgid == 1);
    assert(fresh()->upid == 300);

    printf("test_upid_conflicts_with_pgroup: PASSED\n");
}

/* 0x00E73394 / 0x00E73398 / 0x00E7339C / 0x00E7331C / 0x00E73418 / 0x00E7341C */
static void test_links_and_scalars_cleared(void)
{
    setup_table();

    fresh()->cleanup_flags = 0x1234;
    fresh()->first_debug_target_idx = 0x1111;
    fresh()->debugger_idx = 0x2222;
    fresh()->next_debug_target_idx = 0x3333;
    fresh()->first_child_idx = 0x4444;
    fresh()->next_child_sibling = 0x5555;
    fresh()->pgroup_table_idx = 0x6666;
    fresh()->pad_94 = 0x7777;
    fresh()->sig_mask_2 = 0x88888888u;

    /* fields the routine must NOT touch */
    fresh()->parent_pgroup_idx = 0x9999;
    fresh()->sig_mask_1 = 0xAAAAAAAAu;
    fresh()->sig_mask_3 = 0xBBBBBBBBu;

    PROC2_$INIT_ENTRY_INTERNAL(fresh());

    assert(fresh()->cleanup_flags == 0);
    assert(fresh()->first_debug_target_idx == 0);
    assert(fresh()->debugger_idx == 0);
    assert(fresh()->next_debug_target_idx == 0);
    assert(fresh()->first_child_idx == 0);
    assert(fresh()->next_child_sibling == 0);
    assert(fresh()->pad_94 == 0);
    assert(fresh()->sig_mask_2 == 0);

    assert(fresh()->parent_pgroup_idx == 0x9999);
    assert(fresh()->sig_mask_1 == 0xAAAAAAAAu);
    assert(fresh()->sig_mask_3 == 0xBBBBBBBBu);

    printf("test_links_and_scalars_cleared: PASSED\n");
}

/* 0x00E733C2 and the 0x00E733E6 inherit branch */
static void test_session_and_pgroup_inheritance(void)
{
    setup_table();

    creator()->session_id = 0x0777;
    creator()->pgroup_table_idx = 5;
    mock_pgroups[5].ref_count = 2;
    fresh()->flags = 0;                 /* bit 15 clear -> inherit branch */

    PROC2_$INIT_ENTRY_INTERNAL(fresh());

    assert(fresh()->session_id == 0x0777);
    assert(fresh()->pgroup_table_idx == 5);
    assert(mock_pgroups[5].ref_count == 3);
    assert(n_pgroup_set == 0);

    printf("test_session_and_pgroup_inheritance: PASSED\n");
}

/* 0x00E733C8 bpl -> 0x00E733CE: bit 15 of the flags word takes the leader path */
static void test_leader_branch_on_flag_bit15(void)
{
    setup_table();

    creator()->pgroup_table_idx = 5;
    mock_pgroups[5].ref_count = 2;
    fresh()->flags = 0x8000u;           /* PROC2_FLAG_INIT */

    PROC2_$INIT_ENTRY_INTERNAL(fresh());

    assert(n_pgroup_set == 1);
    assert(last_pgroup_set_entry == fresh());
    assert(last_pgroup_set_upgid == fresh()->upid);

    /* the inherit branch did not run */
    assert(fresh()->pgroup_table_idx == 0);
    assert(mock_pgroups[5].ref_count == 2);

    printf("test_leader_branch_on_flag_bit15: PASSED\n");
}

/*
 * 0x00E733F4-0x00E73408 and 0x00E73412.  The byte-sized andi/or work on the
 * HIGH byte of the flags word, so the propagated bit is 0x0200, and the final
 * andi.w #-0x6071 clears 0x6070.
 */
static void test_flag_propagation_and_mask(void)
{
    setup_table();

    creator()->flags = 0x0200u;
    fresh()->flags = 0xFFFFu;

    PROC2_$INIT_ENTRY_INTERNAL(fresh());

    /* bit 15 was set going in, so this ran the leader branch */
    assert(n_pgroup_set == 1);

    /* 0xFFFF | 0x0200, then & 0x9F8F */
    assert(fresh()->flags == (uint16_t)(0xFFFFu & 0x9F8Fu));
    assert((fresh()->flags & 0x0200u) != 0);
    assert((fresh()->flags & 0x6070u) == 0);

    /* creator bit clear -> the new entry loses 0x0200 */
    setup_table();
    creator()->flags = 0;
    fresh()->flags = 0x0200u;
    PROC2_$INIT_ENTRY_INTERNAL(fresh());
    assert((fresh()->flags & 0x0200u) == 0);

    printf("test_flag_propagation_and_mask: PASSED\n");
}

/* 0x00E7340C */
static void test_name_len_marks_unnamed(void)
{
    setup_table();
    fresh()->name_len = 0x55;

    PROC2_$INIT_ENTRY_INTERNAL(fresh());

    assert(fresh()->name_len == 0x21);

    printf("test_name_len_marks_unnamed: PASSED\n");
}

/* 0x00E73420-0x00E73448: copy out, reset, copy back -- 14 bytes exactly */
static void test_ptrace_opts_round_trip(void)
{
    int i;

    setup_table();
    for (i = 0; i < 14; i++) {
        fresh()->ptrace_opts[i] = (uint8_t)(0x10 + i);
    }
    fresh()->stack_uid.high = 0xFEEDFACEu;   /* the byte right after +0xCE+14 */

    PROC2_$INIT_ENTRY_INTERNAL(fresh());

    assert(n_reset_opts == 1);
    for (i = 0; i < 14; i++) {
        assert(fresh()->ptrace_opts[i] == (uint8_t)(0xE0 + i));
    }
    /* nothing past the 14-byte record was disturbed */
    assert(fresh()->stack_uid.high == 0xFEEDFACEu);

    printf("test_ptrace_opts_round_trip: PASSED\n");
}

/* 0x00E4216E: no group means no reference count bump */
static void test_pgroup_inherit_zero_index(void)
{
    setup_table();

    creator()->pgroup_table_idx = 0;
    mock_pgroups[0].ref_count = 7;
    fresh()->flags = 0;

    PROC2_$INIT_ENTRY_INTERNAL(fresh());

    assert(fresh()->pgroup_table_idx == 0);
    assert(mock_pgroups[0].ref_count == 7);

    printf("test_pgroup_inherit_zero_index: PASSED\n");
}

int main(void)
{
    printf("Running PROC2_$INIT_ENTRY_INTERNAL tests...\n\n");

    test_self_index_is_untouched();
    test_uid_publication_and_fim_init();
    test_upid_counter_advance_and_wrap();
    test_upid_conflicts_with_live_upid();
    test_upid_conflicts_with_session_id();
    test_upid_conflicts_with_pgroup();
    test_links_and_scalars_cleared();
    test_session_and_pgroup_inheritance();
    test_leader_branch_on_flag_bit15();
    test_flag_propagation_and_mask();
    test_name_len_marks_unnamed();
    test_ptrace_opts_round_trip();
    test_pgroup_inherit_zero_index();

    printf("\nAll tests PASSED!\n");
    return 0;
}
