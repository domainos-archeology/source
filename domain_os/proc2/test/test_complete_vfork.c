/*
 * Tests for PROC2_$COMPLETE_VFORK (0x00E73638).
 *
 * The real proc2/complete_vfork.c is #included and driven through mocked
 * callees over a mock P2 info table.  Points pinned by the disassembly:
 *   - a caller without flag 0x0800 gets status_$proc2_process_wasnt_vforked
 *     and nothing else happens (0x00E73696..0x00E736B6);
 *   - the ASID swap at 0x00E736BA..0x00E736C4 (asid <- asid_alt, alt <- 0);
 *   - `move.l D3,(-0x7c,A3)` at 0x00E736C8 stores user_data at entry+0x68
 *     (cr_rec), while the creation record pointer is read from entry+0x6C
 *     (cr_rec_2) at 0x00E737E0;
 *   - PROC2_$UID[new asid] = entry uid, PROC2_$UID[old asid] = parent uid
 *     (0x00E736E2..0x00E73712);
 *   - the user FIM address copy and the conditional quit-inhibit clear
 *     (0x00E73724..0x00E7374E);
 *   - MST_$MAP_INITIAL_AREA is called with area kind 7 and touch TRUE
 *     (0x00E73752..0x00E73774);
 *   - the stack descriptor comes from AS_$STACK_FILE_LOW /
 *     AS_$INIT_STACK_FILE_SIZE (0x00E737E4 / 0x00E737EC) and MST_$MAP_AREA_AT
 *     gets the shared 0x4000 cell and the TRUE byte;
 *   - a non-zero cr_rec status calls PROC2_$DELETE before the startup call
 *     (0x00E73822..0x00E73828), and the startup call falls into the error
 *     tail (unlock + PROC2_$DELETE again) since nothing branches past it.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

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
proc2_ec_entry_t PROC2_$EC[PROC2_EC_ENTRIES];
uid_t PROC2_$UID[PROC2_UID_TABLE_SIZE];
uid_t UID_$NIL = { 0, 0 };
uint16_t PROC1_$CURRENT;
#include "fim/fim.h"
MODULE_DATA_DEFINE(fim_$data_t, FIM_$DATA, 0x00E2126C);
MODULE_DATA_DEFINE(fim_$wired_data_t, FIM_$WIRED_DATA, 0x00E21FE6);
as_$info_t AS_$INFO;
const uint32_t proc2_$map_area_size_00e735f4 = 0x00004000;   /* set_valid.c */

int __host_intr_disable_count = 0;

/*
 * Creation-record arena: entry+0x6C holds a 32-bit VA, so the host maps
 * the arena through ARCH_HOST_VA_BASE and stores an offset in the field.
 */
static uint8_t mock_cr_arena[0x400];
#define MOCK_CR_REC_VA 0x100u
static cr_rec_t *mock_cr_rec(void) { return (cr_rec_t *)(mock_cr_arena + MOCK_CR_REC_VA); }

static status_$t mock_map_status, mock_name_status, mock_map_area_status;
static int n_lock, n_unlock, n_fp_init, n_map, n_name_init, n_advance;
static int n_set_asid, n_map_area, n_delete, n_startup;
static int16_t last_fp_init_asid, last_map_kind; static boolean last_map_touch;
static uint16_t last_map_asid, last_set_asid;
static uint32_t last_map_code_desc, last_map_param;
static uid_t last_map_uid;
static void *last_advance_ec;
static void *last_area_arg1, *last_area_arg2, *last_area_arg3, *last_area_arg4, *last_area_arg5;
static status_$t *last_area_status;
static int32_t last_startup_ctx[2];

static void reset_mocks(void)
{
    memset(mock_entries, 0, sizeof(mock_entries));
    memset(mock_pid_to_index, 0, sizeof(mock_pid_to_index));
    memset(PROC2_$EC, 0, sizeof(PROC2_$EC));
    memset(PROC2_$UID, 0, sizeof(PROC2_$UID));
    memset(FIM_$DATA.user_fim_addr, 0, sizeof(FIM_$DATA.user_fim_addr));
    memset(FIM_$WIRED_DATA.quit_inh, 0, sizeof(FIM_$WIRED_DATA.quit_inh));
    memset(mock_cr_arena, 0, sizeof(mock_cr_arena));
    memset(&AS_$INFO, 0, sizeof(AS_$INFO));
    ARCH_HOST_VA_BASE = (uintptr_t)mock_cr_arena;
    PROC1_$CURRENT = 5;
    mock_map_status = mock_name_status = mock_map_area_status = status_$ok;
    n_lock = n_unlock = n_fp_init = n_map = n_name_init = n_advance = 0;
    n_set_asid = n_map_area = n_delete = n_startup = 0;
    last_fp_init_asid = -1; last_map_kind = -1; last_map_touch = 0x55;
    last_map_asid = 0xFFFF; last_set_asid = 0xFFFF;
    last_advance_ec = NULL;
    last_area_arg1 = last_area_arg2 = last_area_arg3 = last_area_arg4 = last_area_arg5 = NULL;
    last_area_status = NULL;
    last_startup_ctx[0] = last_startup_ctx[1] = 0;
}

void ML_$LOCK(int16_t id)   { (void)id; n_lock++; }
void ML_$UNLOCK(int16_t id) { (void)id; n_unlock++; }
void FIM_$FP_INIT(int16_t asid) { n_fp_init++; last_fp_init_asid = asid; }
void MST_$MAP_INITIAL_AREA(uint32_t code_desc, uint16_t asid, uid_t *parent_uid,
                           uint32_t map_param, int16_t area_kind, boolean touch,
                           status_$t *status)
{
    n_map++;
    last_map_code_desc = code_desc; last_map_asid = asid; last_map_uid = *parent_uid;
    last_map_param = map_param; last_map_kind = area_kind; last_map_touch = touch;
    *status = mock_map_status;
}
void NAME_$INIT_ASID(int16_t *a, status_$t *s) { (void)a; n_name_init++; *s = mock_name_status; }
void EC_$ADVANCE(ec_$eventcount_t *ec) { n_advance++; last_advance_ec = ec; }
void PROC1_$SET_ASID(uint16_t asid) { n_set_asid++; last_set_asid = asid; }
void MST_$MAP_AREA_AT(void *a1, void *a2, void *a3, void *a4, void *a5, status_$t *status)
{
    n_map_area++;
    last_area_arg1 = a1; last_area_arg2 = a2; last_area_arg3 = a3;
    last_area_arg4 = a4; last_area_arg5 = a5; last_area_status = status;
    /* the callee fills the stack UID it is handed */
    ((uid_t *)a5)->high = 0x57AC0000u; ((uid_t *)a5)->low = 0x0000C0DEu;
    *status = mock_map_area_status;
}
void PROC2_$DELETE(void) { n_delete++; }
void FIM_$PROC2_STARTUP(void *context)
{
    n_startup++;
    memcpy(last_startup_ctx, context, sizeof(last_startup_ctx));
}

#include "proc2/complete_vfork.c"

/* ------------------------------------------------------------------ */

static int tests_run, tests_failed;
#define TEST(name) static void name(void)
#define RUN_TEST(name) do { tests_run++; reset_mocks(); name(); } while (0)
#define ASSERT_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        printf("  FAIL %s:%d: %s == %lld, expected %s == %lld\n", \
               __FILE__, __LINE__, #a, _a, #b, _b); \
        tests_failed++; return; } } while (0)

#define PARENT_IDX 2
#define CHILD_IDX  4
#define OLD_ASID   7
#define NEW_ASID   9
static proc2_info_t *parent(void) { return P2_INFO_ENTRY(PARENT_IDX); }
static proc2_info_t *child(void)  { return P2_INFO_ENTRY(CHILD_IDX); }

static void setup_table(void)
{
    mock_pid_to_index[5] = CHILD_IDX;
    child()->self_index = CHILD_IDX;
    child()->parent_pgroup_idx = PARENT_IDX;
    child()->flags = 0x0800 | 0x0100;
    child()->asid = OLD_ASID;
    child()->asid_alt = NEW_ASID;
    child()->uid.high = 0x0C0C0C0Cu; child()->uid.low = 0x0D0D0D0Du;
    child()->cr_rec = 0x11111111u;
    child()->cr_rec_2 = MOCK_CR_REC_VA;
    child()->pad_18[0] = 0x1234;
    parent()->uid.high = 0x0A0A0A0Au; parent()->uid.low = 0x0B0B0B0Bu;
    FIM_$DATA.user_fim_addr[OLD_ASID] = (void *)0x00F1F1F1u;
    FIM_$WIRED_DATA.quit_inh[NEW_ASID] = (int8_t)0xFF;
    AS_$INFO.stack_file_low = 0x00A00000u;
    AS_$INFO.init_stack_file_size = 0x00004000u;
}

static status_$t st_out;
static void run_complete_vfork(void)
{
    uid_t puid = { 0xAAAA1111u, 0xBBBB2222u };
    uint32_t code_desc = 0x00C0DE00u, map_param = 0x4D4D0000u;
    int32_t entry_point = 0x00ABCDEF, user_data = 0x00777700;
    st_out = 0x5A5A5A5A;
    PROC2_$COMPLETE_VFORK(&puid, &code_desc, &map_param, &entry_point, &user_data,
                          0, 0, &st_out);
}

TEST(not_vforked)
{
    setup_table();
    child()->flags = 0x0100;
    run_complete_vfork();
    ASSERT_EQ(st_out, status_$proc2_process_wasnt_vforked);
    ASSERT_EQ(n_lock, 1); ASSERT_EQ(n_unlock, 1);
    ASSERT_EQ(n_fp_init, 0); ASSERT_EQ(n_map, 0); ASSERT_EQ(n_delete, 0);
    ASSERT_EQ(child()->asid, OLD_ASID);
}

TEST(success_path_entry_layout)
{
    setup_table();
    run_complete_vfork();

    /* 0x00E736BA..0x00E736C4: the ASID swap */
    ASSERT_EQ(child()->asid, NEW_ASID);
    ASSERT_EQ(child()->asid_alt, 0);

    /* 0x00E736C8: entry+0x68 = user_data; entry+0x6C untouched */
    ASSERT_EQ(child()->cr_rec, 0x00777700u);
    ASSERT_EQ(child()->cr_rec_2, MOCK_CR_REC_VA);

    /* 0x00E736CC..0x00E736DE */
    ASSERT_EQ(child()->parent_uid.high, 0xAAAA1111u);
    ASSERT_EQ(child()->parent_uid.low, 0xBBBB2222u);
    ASSERT_EQ(child()->flags & 0x0800, 0);
    ASSERT_EQ(child()->flags & 0x0100, 0x0100);
    ASSERT_EQ(child()->pad_18[0], 0);

    /* 0x00E736E2..0x00E73712: the two UID-table slots */
    ASSERT_EQ(PROC2_$UID[NEW_ASID].high, 0x0C0C0C0Cu);
    ASSERT_EQ(PROC2_$UID[NEW_ASID].low, 0x0D0D0D0Du);
    ASSERT_EQ(PROC2_$UID[OLD_ASID].high, 0x0A0A0A0Au);
    ASSERT_EQ(PROC2_$UID[OLD_ASID].low, 0x0B0B0B0Bu);

    /* 0x00E73716..0x00E7374E */
    ASSERT_EQ(n_fp_init, 1); ASSERT_EQ(last_fp_init_asid, NEW_ASID);
    ASSERT_EQ((uintptr_t)FIM_$DATA.user_fim_addr[NEW_ASID], 0x00F1F1F1u);
    ASSERT_EQ(FIM_$WIRED_DATA.quit_inh[NEW_ASID], 0);

    /* 0x00E73752..0x00E73774 */
    ASSERT_EQ(n_map, 1);
    ASSERT_EQ(last_map_code_desc, 0x00C0DE00u);
    ASSERT_EQ(last_map_asid, NEW_ASID);
    ASSERT_EQ(last_map_uid.high, 0xAAAA1111u);
    ASSERT_EQ(last_map_param, 0x4D4D0000u);
    ASSERT_EQ(last_map_kind, 7);
    ASSERT_EQ((int8_t)last_map_touch, -1);

    /* 0x00E7378E..0x00E737DE */
    ASSERT_EQ(n_name_init, 1);
    ASSERT_EQ(n_advance, 1);
    ASSERT_EQ((uintptr_t)last_advance_ec, (uintptr_t)PROC_FORK_EC(CHILD_IDX));
    ASSERT_EQ(n_set_asid, 1); ASSERT_EQ(last_set_asid, NEW_ASID);

    /* 0x00E737E0..0x00E7381E: the creation record via entry+0x6C */
    ASSERT_EQ(mock_cr_rec()->addr_lo, 0x00A00000u);
    ASSERT_EQ(mock_cr_rec()->size, 0x00004000u);
    ASSERT_EQ(n_map_area, 1);
    ASSERT_EQ((uintptr_t)last_area_arg1, (uintptr_t)&mock_cr_rec()->addr_lo);
    ASSERT_EQ((uintptr_t)last_area_arg2, (uintptr_t)&mock_cr_rec()->size);
    ASSERT_EQ(*(const uint32_t *)last_area_arg3, 0x4000u);
    ASSERT_EQ(*(const int8_t *)last_area_arg4, -1);
    ASSERT_EQ((uintptr_t)last_area_arg5, (uintptr_t)&child()->stack_uid);
    ASSERT_EQ((uintptr_t)last_area_status, (uintptr_t)&mock_cr_rec()->status);
    ASSERT_EQ(mock_cr_rec()->stack_uid.high, 0x57AC0000u);
    ASSERT_EQ(mock_cr_rec()->stack_uid.low, 0x0000C0DEu);

    /* 0x00E7382E..0x00E73840: the startup context, then the error tail */
    ASSERT_EQ(n_startup, 1);
    ASSERT_EQ(last_startup_ctx[0], 0x00777700);
    ASSERT_EQ(last_startup_ctx[1], 0x00ABCDEF);
    ASSERT_EQ(n_delete, 1);
    ASSERT_EQ(n_lock, 1); ASSERT_EQ(n_unlock, 2);

    /* (0x24,A6) is written only on the "wasn't vforked" exit */
    ASSERT_EQ(st_out, 0x5A5A5A5A);
}

TEST(user_fim_addr_zero_keeps_quit_inh)
{
    setup_table();
    FIM_$DATA.user_fim_addr[OLD_ASID] = NULL;
    run_complete_vfork();
    ASSERT_EQ((uintptr_t)FIM_$DATA.user_fim_addr[NEW_ASID], 0);
    ASSERT_EQ(FIM_$WIRED_DATA.quit_inh[NEW_ASID], -1);
}

TEST(map_initial_area_failure)
{
    setup_table();
    mock_map_status = 0x00040001;
    run_complete_vfork();
    ASSERT_EQ(n_map, 1);
    ASSERT_EQ(n_name_init, 0); ASSERT_EQ(n_advance, 0);
    ASSERT_EQ(n_set_asid, 0); ASSERT_EQ(n_map_area, 0); ASSERT_EQ(n_startup, 0);
    ASSERT_EQ(n_unlock, 1); ASSERT_EQ(n_delete, 1);
    /* the swap and the entry+0x68 store already happened */
    ASSERT_EQ(child()->asid, NEW_ASID);
    ASSERT_EQ(child()->cr_rec, 0x00777700u);
}

TEST(name_init_failure)
{
    setup_table();
    mock_name_status = 0x00040002;
    run_complete_vfork();
    ASSERT_EQ(n_name_init, 1);
    ASSERT_EQ(n_advance, 0); ASSERT_EQ(n_set_asid, 0); ASSERT_EQ(n_startup, 0);
    ASSERT_EQ(n_unlock, 1); ASSERT_EQ(n_delete, 1);
    ASSERT_EQ(child()->stack_uid.high, 0); ASSERT_EQ(child()->stack_uid.low, 0);
}

TEST(map_area_at_failure_deletes_twice)
{
    setup_table();
    mock_map_area_status = 0x00040003;
    run_complete_vfork();
    ASSERT_EQ(n_map_area, 1);
    ASSERT_EQ(mock_cr_rec()->status, 0x00040003);
    /* 0x00E73828 (the tst.l path) and 0x00E73850 (the tail) */
    ASSERT_EQ(n_delete, 2);
    ASSERT_EQ(n_startup, 1);
    ASSERT_EQ(n_unlock, 2);
}

int main(void)
{
    RUN_TEST(not_vforked);
    RUN_TEST(success_path_entry_layout);
    RUN_TEST(user_fim_addr_zero_keeps_quit_inh);
    RUN_TEST(map_initial_area_failure);
    RUN_TEST(name_init_failure);
    RUN_TEST(map_area_at_failure_deletes_twice);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
