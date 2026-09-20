/*
 * Tests for PROC2_$INIT (0x00E303D8) table initialisation.
 *
 * The real proc2/init.c is #included below and driven through mocked
 * UID_$ / PROC1_$ / EC_$ / MST_$ / OS_$ dependencies over a mock P2 info
 * table.  OS_$BOOT_ERRCHK is made to return a non-negative value so the
 * routine returns at the first boot check (0x00E30612 `tst.b D0b` /
 * `bpl.w 0x00e30888`), which is past every table it builds.
 *
 * Headline case is bead source-nm9e: the free-list loop at
 * 0x00E304B0..0x00E304D4 runs 69 iterations over i = 2..70 and stores
 * next_index = i + 1 UNCONDITIONALLY, so slot 70 briefly points at the
 * non-existent index 71; the list is terminated afterwards by
 * 0x00E304D8 `clr.w (0x00ea92a2).l`, which is entry(70)->next_index.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "base/base.h"
#include "proc2/proc2_internal.h"

/* ------------------------------------------------------------------ */
/* Mock storage                                                        */
/* ------------------------------------------------------------------ */

/*
 * Slot 0 is the out-of-band "index 0" entry the table is biased against;
 * slot P2_INFO_TABLE_SIZE + 1 is a canary standing in for the index-71
 * storage the image does not have (there the PID-to-index table starts).
 */
#define MOCK_SLOTS (P2_INFO_TABLE_SIZE + 2)

#define MOCK_PID_ENTRIES 66   /* pids 0..65; the image clears 2..64 */

static proc2_info_t mock_entries[MOCK_SLOTS];
static uint16_t mock_pid_to_index[MOCK_PID_ENTRIES];
static pgroup_entry_t mock_pgroups[PGROUP_TABLE_SIZE];

proc2_info_t *P2_INFO_TABLE = &mock_entries[1];
uint16_t P2_INFO_ALLOC_PTR;
uint16_t P2_FREE_LIST_HEAD;
uint16_t *PROC2_$PID_TO_INDEX = mock_pid_to_index;
pgroup_entry_t *PGROUP_TABLE = mock_pgroups;
proc2_ec_entry_t PROC2_$EC[PROC2_EC_ENTRIES];
uint16_t PROC2_$NEXT_UPID = P2_UPID_WRAP_TO;

uid_t PROC2_$UID[PROC2_UID_TABLE_SIZE];
uid_t proc2_system_uid;
uid_t proc2_proc_dir_uid;
int16_t proc2_boot_flags;
status_$t PROC2_Internal_Error = status_$proc2_internal_error;

uid_t UID_$NIL = { 0xAAAABBBBu, 0xCCCCDDDDu };

uint16_t PROC1_$CURRENT = 1;
int8_t DTTY_$USE_DTTY;
as_$info_t AS_$INFO;

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

static int n_uid_gen, n_ec_init, n_map_area_at, n_boot_errchk;

void UID_$GEN(uid_t *uid_ret)
{
    n_uid_gen++;
    uid_ret->high = 0x01020000u + (uint32_t)n_uid_gen;
    uid_ret->low = 0x03040000u + (uint32_t)n_uid_gen;
}

void PROC1_$SET_PRIORITY(uint16_t pid, int8_t mode, uint16_t *min_priority,
                         uint16_t *max_priority)
{
    (void)pid; (void)mode; (void)min_priority; (void)max_priority;
}

void EC_$INIT(ec_$eventcount_t *ec) { (void)ec; n_ec_init++; }

void MST_$MAP_AREA_AT(void *addr_ptr, void *size_ptr, void *param1,
                      void *param2, void *param3, status_$t *status)
{
    (void)addr_ptr; (void)size_ptr; (void)param1; (void)param2; (void)param3;
    n_map_area_at++;
    *status = status_$ok;
}

/*
 * Returns a value whose sign bit is CLEAR, which is the "stop here" answer
 * for the `tst.b D0b` / `bpl` pairs in PROC2_$INIT.
 */
char OS_$BOOT_ERRCHK(const char *format_str, const char *arg_str,
                     short *line_ptr, status_$t *status_ret)
{
    (void)format_str; (void)arg_str; (void)line_ptr; (void)status_ret;
    n_boot_errchk++;
    return 0;
}

int8_t MMU_$NORMAL_MODE(void) { return 0; }

int8_t TAPE_$BOOT(uint32_t *entry_point, status_$t *status_ret)
{
    (void)entry_point; (void)status_ret;
    return 0;
}

int8_t FLOP_$BOOT(uint32_t *entry_point, status_$t *status_ret)
{
    (void)entry_point; (void)status_ret;
    return 0;
}

void NAME_$RESOLVE(char *path, int16_t *path_len, uid_t *resolved_uid,
                   status_$t *status_ret)
{
    (void)path; (void)path_len;
    resolved_uid->high = 0;
    resolved_uid->low = 0;
    *status_ret = status_$ok;
}

void FILE_$LOCK(uid_t *file_uid, const uint16_t *lock_index,
                const uint16_t *lock_mode, const uint8_t *rights,
                void *lock_info, status_$t *status_ret)
{
    (void)file_uid; (void)lock_index; (void)lock_mode; (void)rights;
    (void)lock_info;
    *status_ret = status_$ok;
}

void *MST_$MAP(uid_t *uid, uint32_t *start_ptr, uint32_t *length_ptr,
               uint16_t *mode_ptr, uint32_t *extend_ptr,
               uint8_t *concur_ptr, void *map_info, status_$t *status_ret)
{
    (void)uid; (void)start_ptr; (void)length_ptr; (void)mode_ptr;
    (void)extend_ptr; (void)concur_ptr; (void)map_info;
    *status_ret = status_$ok;
    return NULL;
}

void MST_$UNMAP(uid_t *uid, uint32_t *map_info, uint32_t *result,
                status_$t *status_ret)
{
    (void)uid; (void)map_info; (void)result;
    *status_ret = status_$ok;
}

void MST_$MAP_AT(void *start, uid_t *uid, void *param1, void *param2,
                 void *param3, void *param4, void *param5, void *result,
                 status_$t *status)
{
    (void)start; (void)uid; (void)param1; (void)param2; (void)param3;
    (void)param4; (void)param5; (void)result;
    *status = status_$ok;
}

/* ------------------------------------------------------------------ */
/* Code under test                                                     */
/* ------------------------------------------------------------------ */

#include "../init.c"

/* ------------------------------------------------------------------ */
/* Tiny test harness                                                   */
/* ------------------------------------------------------------------ */

static int tests_run, tests_failed, current_failed;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        printf("  Running %s... ", #name);                                    \
        test_##name();                                                        \
        if (current_failed) { tests_failed++; printf("FAILED\n"); }           \
        else printf("PASSED\n");                                              \
    } while (0)

#define ASSERT_EQ(actual, expected, what)                                     \
    do {                                                                      \
        long long a_ = (long long)(actual);                                   \
        long long e_ = (long long)(expected);                                 \
        if (a_ != e_) {                                                       \
            if (!current_failed) printf("\n");                                \
            printf("    %s: expected %lld, got %lld\n", (what), e_, a_);      \
            current_failed = 1;                                               \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */

/* Byte pattern the canary slot and the un-cleared PID slots start from. */
#define CANARY 0x5A

static uint16_t boot_flags_word;
static status_$t status_out;

/* Value stamped into every slot's flags word before PROC2_$INIT runs. */
static uint16_t flags_preset = (CANARY << 8) | CANARY;

static void run_init(void)
{
    size_t slot;

    memset(mock_entries, CANARY, sizeof(mock_entries));
    memset(mock_pid_to_index, CANARY, sizeof(mock_pid_to_index));
    memset(mock_pgroups, 0, sizeof(mock_pgroups));
    memset(PROC2_$UID, 0, sizeof(PROC2_$UID));

    P2_INFO_ALLOC_PTR = 0;
    P2_FREE_LIST_HEAD = 0;
    proc2_boot_flags = 0;
    n_uid_gen = n_ec_init = n_map_area_at = n_boot_errchk = 0;

    AS_$INFO.stack_high = 0;

    for (slot = 1; slot < MOCK_SLOTS - 1; slot++) {
        mock_entries[slot].flags = flags_preset;
    }

    boot_flags_word = 0;
    status_out = status_$ok;
    PROC2_$INIT(&boot_flags_word, &status_out);
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/* 0x00E30494  move.w #0x2,(0x1e2,A1) */
TEST(free_list_head_is_slot_2)
{
    run_init();
    ASSERT_EQ(P2_FREE_LIST_HEAD, P2_FIRST_FREE_ENTRY, "P2_FREE_LIST_HEAD");
}

/*
 * 0x00E304B4 / 0x00E304B6: next_index = i + 1 for every slot the loop
 * touches, i = 2..70.
 */
TEST(free_list_links_run_2_through_70)
{
    int i;

    run_init();
    for (i = P2_FIRST_FREE_ENTRY; i < P2_LAST_ENTRY; i++) {
        ASSERT_EQ(P2_INFO_ENTRY(i)->next_index, i + 1, "next_index");
    }
}

/*
 * 0x00E304D8  clr.w (0x00ea92a2).l -- 0xEA551C + 69*0xE4 + 0x12 is
 * entry(70)->next_index, so the tail link is 0 and NOT 71.
 */
TEST(tail_link_of_slot_70_is_zero)
{
    run_init();
    ASSERT_EQ(P2_LAST_ENTRY, 70, "P2_LAST_ENTRY");
    ASSERT_EQ(P2_INFO_ENTRY(P2_LAST_ENTRY)->next_index, 0,
              "entry(70)->next_index");
}

/* Walking the free list from the head must visit 69 slots and terminate. */
TEST(free_list_walks_69_slots_and_terminates)
{
    int n = 0;
    uint16_t idx;

    run_init();
    idx = P2_FREE_LIST_HEAD;
    while (idx != 0 && n <= P2_INFO_TABLE_SIZE) {
        ASSERT_EQ(idx >= P2_FIRST_FREE_ENTRY && idx <= P2_LAST_ENTRY, 1,
                  "free list index in range");
        idx = P2_INFO_ENTRY(idx)->next_index;
        n++;
    }
    ASSERT_EQ(n, P2_LAST_ENTRY - P2_FIRST_FREE_ENTRY + 1, "free list length");
    ASSERT_EQ(idx, 0, "free list terminator");
}

/*
 * There is no slot 71: entry 70 ends at 0xEA9374 and the PID-to-index table
 * begins at 0xEA93D2, so nothing may be written past the last entry.
 */
TEST(nothing_is_written_past_slot_70)
{
    const uint8_t *canary = (const uint8_t *)&mock_entries[MOCK_SLOTS - 1];
    size_t i;

    run_init();
    for (i = 0; i < sizeof(proc2_info_t); i++) {
        ASSERT_EQ(canary[i], CANARY, "byte past the last entry");
    }
}

/* 0x00E304CA move.w D1w,(-0xc8,A0) and 0x00E30502 move.w #0x1,(0x1c,A2) */
TEST(self_index_is_stamped_on_every_slot)
{
    int i;

    run_init();
    ASSERT_EQ(P2_INFO_ENTRY(1)->self_index, 1, "entry(1)->self_index");
    for (i = P2_FIRST_FREE_ENTRY; i <= P2_LAST_ENTRY; i++) {
        ASSERT_EQ(P2_INFO_ENTRY(i)->self_index, i, "self_index");
    }
}

/* 0x00E304C4 andi.w #-0x181,(-0xba,A0): exactly PROC2_FLAG_VALID clears. */
TEST(only_the_valid_flag_is_cleared_in_free_slots)
{
    int i;

    flags_preset = 0xFFFF;
    run_init();
    flags_preset = (CANARY << 8) | CANARY;

    for (i = P2_FIRST_FREE_ENTRY; i <= P2_LAST_ENTRY; i++) {
        ASSERT_EQ(P2_INFO_ENTRY(i)->flags, (uint16_t)0xFE7F, "flags");
    }
}

/* 0x00E304BC / 0x00E304C0: both longwords of UID_$NIL land at entry+0x08. */
TEST(free_slots_get_nil_parent_uid)
{
    int i;

    run_init();
    for (i = P2_FIRST_FREE_ENTRY; i <= P2_LAST_ENTRY; i++) {
        ASSERT_EQ(P2_INFO_ENTRY(i)->parent_uid.high, UID_$NIL.high,
                  "parent_uid.high");
        ASSERT_EQ(P2_INFO_ENTRY(i)->parent_uid.low, UID_$NIL.low,
                  "parent_uid.low");
    }
}

/*
 * 0x00E30470 clr.w (0x3eb6,A0) with A0 = 0xEA5520 clears pids 2..64;
 * 0x00E304EA move.w #0x1,(0x00ea93d4).l sets pid 1.
 */
TEST(pid_map_covers_pid_1_through_64)
{
    int i;

    run_init();
    ASSERT_EQ(P2_PID_TO_INDEX(1), 1, "P2_PID_TO_INDEX(1)");
    for (i = 2; i <= 64; i++) {
        ASSERT_EQ(P2_PID_TO_INDEX(i), 0, "cleared pid slot");
    }
    ASSERT_EQ(mock_pid_to_index[65], (uint16_t)0x5A5A, "pid slot 65 untouched");
}

/* 0x00E304E4 move.w #0x1,(0x1e0,A3) */
TEST(alloc_pointer_is_slot_1)
{
    run_init();
    ASSERT_EQ(P2_INFO_ALLOC_PTR, 1, "P2_INFO_ALLOC_PTR");
    ASSERT_EQ(P2_INFO_ENTRY(1)->next_index, 0, "entry(1)->next_index");
}

/*
 * The eventcount pair for the last slot must exist: PROC_FORK_EC is indexed
 * by self_index, which runs to P2_INFO_TABLE_SIZE.
 */
TEST(eventcount_table_covers_every_slot)
{
    ASSERT_EQ(PROC2_EC_ENTRIES, P2_INFO_TABLE_SIZE, "PROC2_EC_ENTRIES");
    ASSERT_EQ(PROC_FORK_EC(P2_INFO_TABLE_SIZE) ==
                  &PROC2_$EC[P2_INFO_TABLE_SIZE - 1].fork_ec,
              1, "last fork EC");
}

int main(void)
{
    printf("test_init:\n");

    RUN_TEST(free_list_head_is_slot_2);
    RUN_TEST(free_list_links_run_2_through_70);
    RUN_TEST(tail_link_of_slot_70_is_zero);
    RUN_TEST(free_list_walks_69_slots_and_terminates);
    RUN_TEST(nothing_is_written_past_slot_70);
    RUN_TEST(self_index_is_stamped_on_every_slot);
    RUN_TEST(only_the_valid_flag_is_cleared_in_free_slots);
    RUN_TEST(free_slots_get_nil_parent_uid);
    RUN_TEST(pid_map_covers_pid_1_through_64);
    RUN_TEST(alloc_pointer_is_slot_1);
    RUN_TEST(eventcount_table_covers_every_slot);

    printf("\n  Results: %d passed, %d failed\n",
           tests_run - tests_failed, tests_failed);
    return tests_failed ? 1 : 0;
}
