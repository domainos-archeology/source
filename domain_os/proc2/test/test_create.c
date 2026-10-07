/*
 * Tests for PROC2_$CREATE (0x00E726EC) process-table manipulation.
 *
 * The real proc2/create.c is #included and driven through mocked
 * callees over a mock P2 info table.  Points pinned by the disassembly:
 *   - the "table full" exit (0x00E72748) does not touch *status_ret
 *     beyond status_$proc2_table_full;
 *   - MST_$ALLOC_ASID writes the CALLER's status (0x00E727B8) and the
 *     failure exit sets its bit 31 (0x00E727D2) and skips the ASID/stack
 *     teardown (bra.w 0x00E72B24);
 *   - entry+0x6C takes *code_desc and entry+0x68 takes *user_data
 *     (0x00E7280C / 0x00E72810);
 *   - the high-byte flag merge at 0x00E727E6..0x00E727F2 is
 *     (flags & 0x7FFF) | ((arg & 0x80) << 8);
 *   - the startup context is 14 bytes, 0x10 below the reserve
 *     (0x00E7286A..0x00E7288C);
 *   - the child link uses parent+0x1C (self_index) at 0x00E72928;
 *   - both back-link writes (0x00E727A0, 0x00E72B64) are unconditional;
 *   - the cleanup tail's status test is "code == 0x19" (0x00E72ADA).
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "base/base.h"
#include "proc2/proc2_internal.h"


MODULE_DATA_DEFINE(proc2_$wired_data_t, PROC2_$WIRED_DATA, 0x00E2B978);
MODULE_DATA_DEFINE_INIT(proc2_$unwired_data_t, PROC2_$UNWIRED_DATA, 0x00E7BE84, {
    .system_uid = { 0x11112222u, 0x33334444u },
});
MODULE_DATA_DEFINE(proc2_$data_t, PROC2_$DATA, 0x00EA551C);

uid_t UID_$NIL = { 0, 0 };
uint16_t PROC1_$CURRENT;
uint32_t FIM_$INITIAL_STACK_SIZE = 0x100;

int __host_intr_disable_count = 0;

static uint8_t mock_stack_area[512];
static uint16_t mock_alloc_asid_result;
static status_$t mock_alloc_asid_status;
static status_$t mock_map_status, mock_alloc_stack_status, mock_bind_status;
static status_$t mock_register_status, mock_name_status;
static uint16_t mock_bind_pid;
static int8_t mock_inherit_ptrace, mock_tst_lock;
static void *mock_registered_ec;

static int n_lock, n_unlock, n_init_entry, n_fp_init, n_map, n_bind;
static int n_debug_setup, n_ec_init, n_unbind, n_free_stack, n_free_asid;
static int n_cleanup_handlers, n_pgroup_cleanup, n_set_type, n_set_priority;
static int16_t last_map_kind; static boolean last_map_touch;
static uint16_t last_map_asid;
static void *last_bind_entry, *last_bind_ctx, *last_bind_stack;
static int16_t last_debug_target, last_debug_debugger; static int8_t last_debug_flag;
static uint16_t last_set_type_pid, last_set_type_type;
static int8_t last_pri_modes[2]; static uint16_t last_pri_pids[2];
static uint16_t last_pri_min, last_pri_max;

static void reset_mocks(void)
{
    memset(PROC2_$DATA.info, 0, sizeof(PROC2_$DATA.info));
    memset(PROC2_$DATA.pid_to_index, 0, sizeof(PROC2_$DATA.pid_to_index));
    memset(PROC2_$WIRED_DATA.ec, 0, sizeof(PROC2_$WIRED_DATA.ec));
    memset(PROC2_$UNWIRED_DATA.uid, 0, sizeof(PROC2_$UNWIRED_DATA.uid));
    PROC2_$UNWIRED_DATA.info_alloc_ptr = 0; PROC2_$UNWIRED_DATA.free_list_head = 0;
    PROC1_$CURRENT = 5;
    mock_alloc_asid_result = 7; mock_alloc_asid_status = status_$ok;
    mock_map_status = mock_alloc_stack_status = mock_bind_status = status_$ok;
    mock_register_status = mock_name_status = status_$ok;
    mock_bind_pid = 9; mock_inherit_ptrace = 0; mock_tst_lock = -1;
    mock_registered_ec = (void *)0x12345678u;
    n_lock = n_unlock = n_init_entry = n_fp_init = n_map = n_bind = 0;
    n_debug_setup = n_ec_init = n_unbind = n_free_stack = n_free_asid = 0;
    n_cleanup_handlers = n_pgroup_cleanup = n_set_type = n_set_priority = 0;
    last_map_kind = -1; last_map_touch = 0x55; last_map_asid = 0xFFFF;
    last_bind_entry = last_bind_ctx = last_bind_stack = NULL;
    last_debug_target = last_debug_debugger = -1; last_debug_flag = 0x55;
    last_pri_modes[0] = last_pri_modes[1] = 0x55;
}

void ML_$LOCK(int16_t id)   { (void)id; n_lock++; }
void ML_$UNLOCK(int16_t id) { (void)id; n_unlock++; }
void TIME_$CLOCK(clock_t *c) { c->high = 0xDEADBEEFu; c->low = 0xCAFE; }
uint16_t MST_$ALLOC_ASID(status_$t *s) { *s = mock_alloc_asid_status; return mock_alloc_asid_result; }
void MST_$FREE_ASID(uint16_t asid, status_$t *s) { (void)asid; *s = status_$ok; n_free_asid++; }
void (FIM_$FP_INIT)(uint32_t asid_slot) { int16_t asid = (int16_t)ARCH_PASCAL_SLOT_WORD(asid_slot); (void)asid; (void)asid; n_fp_init++; }
void PROC2_$INIT_ENTRY_INTERNAL(proc2_info_t *e) { (void)e; n_init_entry++; }
void MST_$MAP_INITIAL_AREA(uint32_t code_desc, uint16_t asid, uid_t *parent_uid,
                           uint32_t map_param, int16_t area_kind, boolean touch,
                           status_$t *status)
{
    (void)code_desc; (void)parent_uid; (void)map_param;
    n_map++; last_map_asid = asid; last_map_kind = area_kind; last_map_touch = touch;
    *status = mock_map_status;
}
void *PROC1_$ALLOC_STACK(uint16_t type, status_$t *s)
{
    (void)type; *s = mock_alloc_stack_status;
    return mock_stack_area + sizeof(mock_stack_area);
}
void PROC1_$FREE_STACK(void *stack) { (void)stack; n_free_stack++; }
uint16_t PROC1_$BIND(void *entry, void *initial_sp, void *stack_base,
                     uint16_t ws_param, status_$t *s)
{
    (void)ws_param; n_bind++;
    last_bind_entry = entry; last_bind_ctx = initial_sp; last_bind_stack = stack_base;
    *s = mock_bind_status; return mock_bind_pid;
}
void PROC1_$UNBIND(uint16_t pid, status_$t *s) { (void)pid; *s = status_$ok; n_unbind++; }
int8_t PROC1_$TST_LOCK(uint16_t id) { (void)id; return mock_tst_lock; }
void PROC1_$SET_PRIORITY(uint16_t pid, int8_t mode, uint16_t *min_p, uint16_t *max_p)
{
    if (n_set_priority < 2) { last_pri_pids[n_set_priority] = pid; last_pri_modes[n_set_priority] = mode; }
    n_set_priority++;
    if (mode == PROC1_SET_PRIORITY_GET) { *min_p = 4; *max_p = 12; }
    else { last_pri_min = *min_p; last_pri_max = *max_p; }
}
void PROC1_$SET_TYPE(uint16_t pid, uint16_t type) { n_set_type++; last_set_type_pid = pid; last_set_type_type = type; }
void PROC2_$STARTUP(void *ctx) { (void)ctx; }
int8_t XPD_$INHERIT_PTRACE_OPTIONS(xpd_$ptrace_opts_t *o) { (void)o; return mock_inherit_ptrace; }
void DEBUG_SETUP_INTERNAL(int16_t t, int16_t d, int8_t f)
{ n_debug_setup++; last_debug_target = t; last_debug_debugger = d; last_debug_flag = f; }
void EC_$INIT(ec_$eventcount_t *ec) { n_ec_init++; ec->value = 0; }
void *EC2_$REGISTER_EC1(ec_$eventcount_t *ec, status_$t *s) { (void)ec; *s = mock_register_status; return mock_registered_ec; }
void ACL_$ALLOC_ASID(int16_t a, status_$t *s) { (void)a; *s = status_$ok; }
void AUDIT_$INHERIT_AUDIT(int16_t *p, status_$t *s) { (void)p; *s = status_$ok; }
void NAME_$INIT_ASID(int16_t *a, status_$t *s) { (void)a; *s = mock_name_status; }
void PROC2_$CLEANUP_HANDLERS_INTERNAL(proc2_info_t *e) { (void)e; n_cleanup_handlers++; }
void PGROUP_CLEANUP_INTERNAL(proc2_info_t *e, int16_t m) { (void)e; (void)m; n_pgroup_cleanup++; }

#include "proc2/create.c"

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
static proc2_info_t *parent(void) { return P2_INFO_ENTRY(PARENT_IDX); }
static proc2_info_t *child(void)  { return P2_INFO_ENTRY(CHILD_IDX); }

static void setup_table(void)
{
    PROC2_$DATA.pid_to_index[5] = PARENT_IDX;
    PROC2_$UNWIRED_DATA.free_list_head = CHILD_IDX;
    child()->next_index = 0;
    child()->self_index = CHILD_IDX;
    child()->flags = 0x8000;
    PROC2_$UNWIRED_DATA.info_alloc_ptr = PARENT_IDX;
    parent()->self_index = PARENT_IDX;
    parent()->first_child_idx = 6;
    parent()->pad_18[0] = 0x1234;
    parent()->tty_uid.high = 0xCCCC0000u; parent()->tty_uid.low = 0xDDDD0000u;
    memset(parent()->ptrace_opts, 0x77, 14);
}

static uid_t uid_out; static void *ec_out; static status_$t st_out;
static void run_create(uint8_t flags_byte)
{
    uid_t puid = { 0xAAAA1111u, 0xBBBB2222u };
    uint32_t code_desc = 0x00C0DE00u, map_param = 0x4D4D0000u;
    int32_t entry_point = 0x00ABCDEF, user_data = 0x00777700;
    uint8_t flags = flags_byte;
    uid_out.high = uid_out.low = 0; ec_out = NULL; st_out = 0x5A5A5A5A;
    PROC2_$CREATE(&puid, &code_desc, &map_param, &entry_point, &user_data,
                  0, 0, &flags, &uid_out, &ec_out, &st_out);
}

TEST(table_full)
{
    PROC2_$UNWIRED_DATA.free_list_head = 0;
    run_create(0);
    ASSERT_EQ(st_out, status_$proc2_table_full);
    ASSERT_EQ(n_lock, 1); ASSERT_EQ(n_unlock, 1);
    ASSERT_EQ(n_init_entry, 0);
}

TEST(success_path_layout)
{
    setup_table();
    child()->uid.high = 0x0C0C0C0Cu; child()->uid.low = 0x0D0D0D0Du;
    run_create(0);
    ASSERT_EQ(st_out, status_$ok);
    /* list surgery */
    ASSERT_EQ(PROC2_$UNWIRED_DATA.free_list_head, 0);
    ASSERT_EQ(PROC2_$UNWIRED_DATA.info_alloc_ptr, CHILD_IDX);
    ASSERT_EQ(child()->next_index, PARENT_IDX);
    ASSERT_EQ(parent()->pad_14, CHILD_IDX);
    ASSERT_EQ(child()->pad_14, 0);
    /* asid + records */
    ASSERT_EQ(child()->asid, 7);
    ASSERT_EQ(n_fp_init, 1); ASSERT_EQ(n_init_entry, 1);
    ASSERT_EQ(child()->cr_rec_2, 0x00C0DE00u);   /* *code_desc  -> +0x6C */
    ASSERT_EQ(child()->cr_rec, 0x00777700u);     /* *user_data  -> +0x68 */
    ASSERT_EQ(child()->parent_uid.high, 0xAAAA1111u);
    ASSERT_EQ(last_map_asid, 7); ASSERT_EQ(last_map_kind, 7); ASSERT_EQ(last_map_touch, 0);
    /* bind + context */
    ASSERT_EQ(n_bind, 1);
    ASSERT_EQ(last_bind_entry == (void *)PROC2_$STARTUP, 1);
    ASSERT_EQ(last_bind_stack == (void *)(mock_stack_area + sizeof(mock_stack_area)), 1);
    {
        startup_context_t *ctx = (startup_context_t *)last_bind_ctx;
        ASSERT_EQ((uint8_t *)ctx == mock_stack_area + sizeof(mock_stack_area) - 0x100 - 0x10, 1);
        ASSERT_EQ(ctx->user_data, 0x00777700);
        ASSERT_EQ(ctx->entry_point, 0x00ABCDEF);
        ASSERT_EQ(ctx->asid, 7);
        ASSERT_EQ(ctx->self_ptr == &ctx->user_data, 1);
    }
    ASSERT_EQ(child()->level1_pid, 9);
    ASSERT_EQ(PROC2_$DATA.pid_to_index[9], CHILD_IDX);
    /* flags: andi.b #0x7f clears 0x8000, then BOUND set, andi 0xE3FB */
    ASSERT_EQ(child()->flags, 0x0100);
    ASSERT_EQ(child()->pad_18[1], 0x1234);
    ASSERT_EQ(child()->acct_uid.high, 0xAAAA1111u);
    ASSERT_EQ(child()->acct_info_len, 0);
    ASSERT_EQ(child()->creation_time_high, 0xDEADBEEFu);
    ASSERT_EQ(child()->creation_time_low, 0xCAFE);
    ASSERT_EQ(child()->tty_uid.low, 0xDDDD0000u);
    /* child link via parent's self_index */
    ASSERT_EQ(child()->parent_pgroup_idx, PARENT_IDX);
    ASSERT_EQ(child()->next_child_sibling, 6);
    ASSERT_EQ(parent()->first_child_idx, CHILD_IDX);
    ASSERT_EQ(n_debug_setup, 0);
    ASSERT_EQ(n_ec_init, 2);
    ASSERT_EQ(ec_out == (void *)0x12345678u, 1);
    ASSERT_EQ(uid_out.high, 0x0C0C0C0Cu); ASSERT_EQ(uid_out.low, 0x0D0D0D0Du);
    /* priorities: GET on caller, SET on child, type 2 */
    ASSERT_EQ(n_set_priority, 2);
    ASSERT_EQ(last_pri_pids[0], 5); ASSERT_EQ(last_pri_modes[0], PROC1_SET_PRIORITY_GET);
    ASSERT_EQ(last_pri_pids[1], 9); ASSERT_EQ(last_pri_modes[1], PROC1_SET_PRIORITY_SET);
    ASSERT_EQ(last_pri_min, 4); ASSERT_EQ(last_pri_max, 12);
    ASSERT_EQ(last_set_type_pid, 9); ASSERT_EQ(last_set_type_type, 2);
    ASSERT_EQ(n_unlock, 1);
}

TEST(init_process_uses_fixed_priorities)
{
    setup_table();
    PROC1_$CURRENT = 1; PROC2_$DATA.pid_to_index[1] = PARENT_IDX;
    run_create(0);
    ASSERT_EQ(n_set_priority, 1);
    ASSERT_EQ(last_pri_min, 3); ASSERT_EQ(last_pri_max, 0xE);
}

TEST(flag_bit7_new_group_and_high_byte_merge)
{
    setup_table();
    child()->flags = 0x00FF;
    run_create(0x80);
    ASSERT_EQ(child()->parent_pgroup_idx, 0);
    ASSERT_EQ(child()->next_child_sibling, 0);
    ASSERT_EQ(parent()->first_child_idx, 6);
    /* 0x00FF -> |0x8000 -> |0x0100 -> &0xE3FB -> &~0x0008 */
    ASSERT_EQ(child()->flags, ((0x80FF | 0x0100) & 0xE3FB) & ~0x0008);
}

TEST(debugger_inheritance)
{
    setup_table();
    parent()->debugger_idx = 3;
    mock_inherit_ptrace = (int8_t)0xFF;
    run_create(0);
    ASSERT_EQ(n_debug_setup, 1);
    ASSERT_EQ(last_debug_target, CHILD_IDX);
    ASSERT_EQ(last_debug_debugger, 3);
    ASSERT_EQ(last_debug_flag, 0);
    ASSERT_EQ(child()->ptrace_opts[13], 0x77);
}

TEST(alloc_asid_failure_sets_bit31_and_skips_teardown)
{
    setup_table();
    mock_alloc_asid_status = 0x00040006;
    run_create(0);
    /* *status_ret gets bit 31 at 0x00E727D2 and is then overwritten at
     * 0x00E72BC0 with the never-written A6-0x18 local (original bug), so
     * its final value is indeterminate and deliberately not asserted. */
    ASSERT_EQ(n_free_asid, 0); ASSERT_EQ(n_unbind, 0); ASSERT_EQ(n_free_stack, 0);
    ASSERT_EQ(n_pgroup_cleanup, 1);
    ASSERT_EQ(PROC2_$UNWIRED_DATA.free_list_head, CHILD_IDX);
    ASSERT_EQ(PROC2_$UNWIRED_DATA.info_alloc_ptr, PARENT_IDX);
    ASSERT_EQ(child()->uid.high, 0x11112222u);
    ASSERT_EQ(n_unlock, 1);
}

TEST(bind_failure_frees_stack_not_unbind)
{
    setup_table();
    mock_bind_status = 0x00120003;
    run_create(0);
    ASSERT_EQ(n_unbind, 0);
    ASSERT_EQ(n_free_stack, 1);
    ASSERT_EQ(n_free_asid, 1);
    ASSERT_EQ((uint32_t)st_out, 0x80120003u);
    ASSERT_EQ(PROC2_$UNWIRED_DATA.uid[7].high, 0x11112222u);
    ASSERT_EQ(n_lock, 1);            /* tst_lock says held: no re-lock */
}

TEST(name_init_failure_relocks_and_unbinds)
{
    setup_table();
    mock_name_status = 0x00190019;   /* low word 0x19: no bit 31 */
    mock_tst_lock = 0;
    child()->cleanup_flags = 1;
    run_create(0);
    ASSERT_EQ(n_lock, 2);
    ASSERT_EQ(n_unbind, 1); ASSERT_EQ(n_free_stack, 0);
    ASSERT_EQ((uint32_t)st_out, 0x00190019u);
    ASSERT_EQ(n_cleanup_handlers, 1);
    ASSERT_EQ(parent()->first_child_idx, 6);   /* child unlinked from parent */
    ASSERT_EQ((child()->flags & 0x0100), 0);
}

int main(void)
{
    RUN_TEST(table_full);
    RUN_TEST(success_path_layout);
    RUN_TEST(init_process_uses_fixed_priorities);
    RUN_TEST(flag_bit7_new_group_and_high_byte_merge);
    RUN_TEST(debugger_inheritance);
    RUN_TEST(alloc_asid_failure_sets_bit31_and_skips_teardown);
    RUN_TEST(bind_failure_frees_stack_not_unbind);
    RUN_TEST(name_init_failure_relocks_and_unbinds);
    printf("%s: %d tests, %d failed\n", __FILE__, tests_run, tests_failed);
    return tests_failed != 0;
}
