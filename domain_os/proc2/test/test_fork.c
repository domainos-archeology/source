/*
 * Tests for PROC2_$FORK (0x00E72BCE) process-table manipulation.
 *
 * The real proc2/fork.c is #included below and driven through mocked
 * PROC1_$ / MST_$ / EC_$ dependencies over a mock P2 info table.
 *
 * These cover the defects listed in the 2026-09-06 audit:
 *   - flag bits written with byte operations on the HIGH byte of the
 *     flags word are 0x0800 / 0x1000 / 0x8000 / 0x0100, not 0x08 / 0x10 /
 *     0x80 / 0x01 (0x00E72C8A .. 0x00E72D6C);
 *   - entry+0x68 (cr_rec) comes from *user_data and entry+0x6C
 *     (cr_rec_2) from the parent (0x00E72CA2 / 0x00E72CAA);
 *   - the child-list splice at 0x00E72DDE / 0x00E72DE4;
 *   - the free/allocated list surgery at 0x00E72C52 and 0x00E73252,
 *     including the UNCONDITIONAL back-link writes;
 *   - entry+0x1E is the parent link cleared at 0x00E72C7C;
 *   - the eventcount pair is indexed by entry+0x1C (0x00E72E2C);
 *   - the vfork branch copies entry+0xDC (stack_uid), not tty_uid;
 *   - the signal-mask copy takes 0x8C and skips 0x80 (0x00E72D80..);
 *   - the low-byte flag propagation at 0x00E72DBC;
 *   - the 48-bit creation time (0x00E72DEC / 0x00E72DF2);
 *   - startup_context_t is 14 bytes and sits 0x10 below the reserve.
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
static proc2_ec_entry_t mock_ecs[PROC2_EC_ENTRIES];

proc2_info_t *P2_INFO_TABLE = &mock_entries[1];
uint16_t P2_INFO_ALLOC_PTR;
uint16_t P2_FREE_LIST_HEAD;
uint16_t *PROC2_$PID_TO_INDEX = mock_pid_to_index;
pgroup_entry_t *PGROUP_TABLE = mock_pgroups;
proc2_ec_entry_t PROC2_$EC[PROC2_EC_ENTRIES];

uint16_t PROC1_$CURRENT;
uint16_t PROC1_$AS_ID;

uid_t PROC2_$UID[PROC2_UID_TABLE_SIZE];
uid_t proc2_system_uid = { 0x11112222u, 0x33334444u };
uid_t proc2_proc_dir_uid;
int16_t proc2_boot_flags;
status_$t PROC2_Internal_Error = 0x00190013;

uid_t UID_$NIL = { 0, 0 };

void *FIM_$USER_FIM_ADDR[64];
int8_t FIM_$QUIT_INH[64];
uint32_t FIM_$INITIAL_STACK_SIZE = 0x100;

int __host_intr_disable_count = 0;

/* ------------------------------------------------------------------ */
/* Mock control / trace                                                */
/* ------------------------------------------------------------------ */

static uint16_t mock_alloc_asid_result;
static status_$t mock_alloc_asid_status;
static void *mock_stack;
static status_$t mock_alloc_stack_status;
static uint16_t mock_bind_pid;
static status_$t mock_bind_status;
static void *mock_registered_ec;
static status_$t mock_register_status;
static int8_t mock_inherit_ptrace;
static int8_t mock_tst_lock;
static uint8_t mock_stack_area[512];

static int n_lock, n_unlock, n_init_entry, n_pgroup_cleanup;
static int n_unbind, n_free_stack, n_free_asid, n_cleanup_handlers;
static int n_waitn, n_resume, n_crash, n_debug_setup, n_profil_fork;
static int n_get_va_info;
static int n_mst_fork;
static uint16_t last_mst_fork_asid;
static uint16_t last_mst_fork_pid;
static uint32_t last_mst_fork_flags;
static void *last_bind_ctx;
static void *last_bind_stack;
static void *last_bind_startup;
static uint32_t *last_va_query[2];
static uid_t *last_va_uid_out[2];

static void reset_mocks(void)
{
    memset(mock_entries, 0, sizeof(mock_entries));
    memset(mock_pid_to_index, 0, sizeof(mock_pid_to_index));
    memset(mock_pgroups, 0, sizeof(mock_pgroups));
    memset(mock_ecs, 0, sizeof(mock_ecs));
    memset(PROC2_$EC, 0, sizeof(PROC2_$EC));
    memset(PROC2_$UID, 0, sizeof(PROC2_$UID));
    memset(FIM_$USER_FIM_ADDR, 0, sizeof(FIM_$USER_FIM_ADDR));
    memset(FIM_$QUIT_INH, 0, sizeof(FIM_$QUIT_INH));

    P2_INFO_ALLOC_PTR = 0;
    P2_FREE_LIST_HEAD = 0;
    PROC1_$CURRENT = 5;
    PROC1_$AS_ID = 3;

    mock_alloc_asid_result = 7;
    mock_alloc_asid_status = status_$ok;
    mock_stack = mock_stack_area + sizeof(mock_stack_area);
    mock_alloc_stack_status = status_$ok;
    mock_bind_pid = 9;
    mock_bind_status = status_$ok;
    mock_registered_ec = (void *)0x12345678u;
    mock_register_status = status_$ok;
    mock_inherit_ptrace = 0;
    mock_tst_lock = -1;      /* lock already held: do not re-acquire */

    n_lock = n_unlock = n_init_entry = n_pgroup_cleanup = 0;
    n_unbind = n_free_stack = n_free_asid = n_cleanup_handlers = 0;
    n_mst_fork = 0;
    last_mst_fork_asid = 0xFFFFu;
    last_mst_fork_pid = 0xFFFFu;
    last_mst_fork_flags = 0xFFFFFFFFu;
    n_waitn = n_resume = n_crash = n_debug_setup = n_profil_fork = 0;
    n_get_va_info = 0;
    last_bind_ctx = last_bind_stack = last_bind_startup = NULL;
    last_va_query[0] = last_va_query[1] = NULL;
    last_va_uid_out[0] = last_va_uid_out[1] = NULL;
}

/* ------------------------------------------------------------------ */
/* Mocked callees                                                      */
/* ------------------------------------------------------------------ */

void ML_$LOCK(int16_t id)   { (void)id; n_lock++; }
void ML_$UNLOCK(int16_t id) { (void)id; n_unlock++; }

void TIME_$CLOCK(clock_t *c) { c->high = 0xDEADBEEFu; c->low = 0xCAFE; }

uint16_t MST_$ALLOC_ASID(status_$t *status_ret)
{
    *status_ret = mock_alloc_asid_status;
    return mock_alloc_asid_result;
}

void MST_$FREE_ASID(uint16_t asid, status_$t *status_ret)
{
    (void)asid;
    *status_ret = status_$ok;
    n_free_asid++;
}

/*
 * source-n7i0: the third parameter is a LONGWORD.  PROC2_$FORK pushes
 * *fork_flags with `move.l (A0),-(SP)` at 0x00E72F52 and MST_$FORK reads it
 * with `move.l (0xc,A6),D0` at 0x00E73A0A.
 */
void MST_$FORK(uint16_t asid, uint16_t pid, uint32_t flags, status_$t *status)
{
    (void)asid; (void)pid;
    n_mst_fork++;
    last_mst_fork_asid = asid;
    last_mst_fork_pid = pid;
    last_mst_fork_flags = flags;
    *status = status_$ok;
}

void MST_$GET_VA_INFO(uint16_t *asid_p, uint32_t *va_ptr, uid_t *uid_out,
                      uint32_t *adjusted_va, void *param_5,
                      int8_t *active_flag, int8_t *modified_flag,
                      status_$t *status_ret)
{
    (void)asid_p; (void)adjusted_va; (void)param_5;
    (void)active_flag; (void)modified_flag;
    if (n_get_va_info < 2) {
        last_va_query[n_get_va_info] = va_ptr;
        last_va_uid_out[n_get_va_info] = uid_out;
    }
    n_get_va_info++;
    uid_out->high = 0xA0000000u + (uint32_t)n_get_va_info;
    uid_out->low = 0xB0000000u + (uint32_t)n_get_va_info;
    *status_ret = status_$ok;
}

void PROC2_$INIT_ENTRY_INTERNAL(proc2_info_t *entry)
{
    (void)entry;
    n_init_entry++;
}

void PROC2_$CLEANUP_HANDLERS_INTERNAL(proc2_info_t *entry)
{
    (void)entry;
    n_cleanup_handlers++;
}

void PGROUP_CLEANUP_INTERNAL(proc2_info_t *entry, int16_t mode)
{
    (void)entry; (void)mode;
    n_pgroup_cleanup++;
}

void DEBUG_SETUP_INTERNAL(int16_t target_idx, int16_t debugger_idx, int8_t flag)
{
    (void)target_idx; (void)debugger_idx; (void)flag;
    n_debug_setup++;
}

void PROC2_$STARTUP(void *context) { (void)context; }

void *PROC1_$ALLOC_STACK(uint16_t type, status_$t *status_ret)
{
    (void)type;
    *status_ret = mock_alloc_stack_status;
    return mock_stack;
}

void PROC1_$FREE_STACK(void *stack) { (void)stack; n_free_stack++; }

uint16_t PROC1_$BIND(void *proc_startup, void *stack1, void *stack2,
                     uint16_t ws_param, status_$t *status_p)
{
    (void)ws_param;
    last_bind_startup = proc_startup;
    last_bind_ctx = stack1;
    last_bind_stack = stack2;
    *status_p = mock_bind_status;
    return mock_bind_pid;
}

void PROC1_$UNBIND(uint16_t pid, status_$t *status_ret)
{
    (void)pid;
    *status_ret = status_$ok;
    n_unbind++;
}

int16_t PROC1_$TST_LOCK(uint16_t lock_id) { (void)lock_id; return mock_tst_lock; }

void PROC1_$SET_PRIORITY(uint16_t pid, int16_t mode,
                         uint16_t *min_priority, uint16_t *max_priority)
{
    (void)pid; (void)mode;
    *min_priority = 1;
    *max_priority = 2;
}

void PROC1_$SET_TYPE(uint16_t pid, uint16_t type) { (void)pid; (void)type; }

void PROC1_$RESUME(uint16_t pid, status_$t *status_ret)
{
    (void)pid;
    *status_ret = status_$ok;
    n_resume++;
}

void EC_$INIT(ec_$eventcount_t *ec)
{
    ec->value = 0;
    ec->waiter_list_head = NULL;
    ec->waiter_list_tail = NULL;
}

int32_t EC_$READ(ec_$eventcount_t *ec) { return ec->value; }

uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *wait_val, int16_t num_ecs)
{
    (void)ecs; (void)wait_val; (void)num_ecs;
    n_waitn++;
    return 0;
}

void *EC2_$REGISTER_EC1(ec_$eventcount_t *ec1, status_$t *status_ret)
{
    (void)ec1;
    *status_ret = mock_register_status;
    return mock_registered_ec;
}

void ACL_$ALLOC_ASID(int16_t asid_ret, status_$t *status_ret)
{
    (void)asid_ret;
    *status_ret = status_$ok;
}

void AUDIT_$INHERIT_AUDIT(int16_t *child_pid, status_$t *status_ret)
{
    (void)child_pid;
    *status_ret = status_$ok;
}

void FILE_$FORK_LOCK(uint16_t *new_asid, status_$t *status_ret)
{
    (void)new_asid;
    *status_ret = status_$ok;
}

void FILE_$PRIV_UNLOCK_ALL(uint16_t *asid_ptr) { (void)asid_ptr; }

boolean MSG_$FORK(uint16_t *parent_asid, uint16_t *child_asid)
{
    (void)parent_asid; (void)child_asid;
    return 0;
}

void NAME_$FORK(int16_t *parent_asid, int16_t *child_asid)
{
    (void)parent_asid; (void)child_asid;
}

void PCHIST_$UNIX_PROFIL_FORK(int16_t *child_pid_ptr, uint16_t *child_asid_p)
{
    (void)child_pid_ptr; (void)child_asid_p;
    n_profil_fork++;
}

int8_t XPD_$INHERIT_PTRACE_OPTIONS(xpd_$ptrace_opts_t *opts)
{
    (void)opts;
    return mock_inherit_ptrace;
}

void CRASH_SYSTEM(const status_$t *status_p)
{
    (void)status_p;
    n_crash++;
    assert(!"CRASH_SYSTEM called");
}

/* ------------------------------------------------------------------ */
/* Code under test                                                     */
/* ------------------------------------------------------------------ */

#include "proc2/fork.c"

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

#define PARENT_IDX 2
#define CHILD_IDX  4

static proc2_info_t *parent(void) { return P2_INFO_ENTRY(PARENT_IDX); }
static proc2_info_t *child(void)  { return P2_INFO_ENTRY(CHILD_IDX); }

/* Build a table with one free entry (CHILD_IDX) and a populated parent. */
static void setup_table(void)
{
    reset_mocks();

    PROC1_$CURRENT = 5;
    mock_pid_to_index[5] = PARENT_IDX;

    P2_FREE_LIST_HEAD = CHILD_IDX;
    child()->next_index = 0;          /* only entry on the free list */

    P2_INFO_ALLOC_PTR = PARENT_IDX;
    parent()->next_index = 0;
    parent()->pad_14 = 0;

    parent()->uid.high = 0x01020304u;
    parent()->uid.low = 0x05060708u;
    parent()->asid = 3;
    parent()->self_index = PARENT_IDX;
    parent()->cr_rec = 0x00110000u;
    parent()->cr_rec_2 = 0x00220000u;
    parent()->first_child_idx = 6;
    parent()->pad_18[0] = 0x1234;
    parent()->stack_uid.high = 0xAAAA0000u;
    parent()->stack_uid.low = 0xBBBB0000u;
    parent()->tty_uid.high = 0xCCCC0000u;
    parent()->tty_uid.low = 0xDDDD0000u;
    parent()->acct_uid.high = 0xEEEE0000u;
    parent()->acct_uid.low = 0xFFFF0000u;
    parent()->acct_info_len = 11;
    memset(parent()->acct_info, 0x5A, sizeof(parent()->acct_info));

    parent()->sig_pending   = 0x70707070u;
    parent()->sig_blocked_1 = 0x74747474u;
    parent()->sig_blocked_2 = 0x78787878u;
    parent()->sig_mask_3    = 0x7C7C7C7Cu;
    parent()->sig_mask_2    = 0x80808080u;
    parent()->sig_mask_1    = 0x84848484u;
    parent()->sig_mask_4    = 0x8C8C8C8Cu;

    /* The child slot's self_index is what indexes the EC pair. */
    child()->self_index = CHILD_IDX;
}

static void run_fork(int32_t flags_value, uid_t *uid_ret, uint16_t *upid_ret,
                     void **ec_ret, status_$t *status_ret)
{
    int32_t entry_point = 0x00ABCDEFu;
    int32_t user_data = 0x00335577u;
    int32_t fork_flags = flags_value;

    /* The child sets flags bit 7 (0x0080) when its half of the fork
     * succeeds; the mock child cannot, so pre-set it where a test wants
     * the eventcount handle preserved. */
    PROC2_$FORK(&entry_point, &user_data, &fork_flags,
                uid_ret, 0, upid_ret, ec_ret, status_ret);
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/*
 * The startup record is 14 bytes with a word ASID; its exact offsets are
 * asserted at compile time in proc2/fork.c under ARCH_M68K (host pointers
 * are 64-bit, so the size cannot be checked here).  What IS checkable on
 * the host is the placement: 0x00E72D1C uses a reserve of 0x10 bytes,
 * not sizeof(startup_context_t).
 */
static void test_startup_context_placement(void)
{
    uid_t uid; uint16_t upid; void *ec; status_$t st;

    assert(STARTUP_CONTEXT_RESERVE == 0x10);

    setup_table();
    run_fork(1, &uid, &upid, &ec, &st);

    assert(last_bind_stack == mock_stack);
    assert(last_bind_startup == (void *)PROC2_$STARTUP);
    assert(last_bind_ctx == (void *)((char *)mock_stack
                                     - (0x10 + (int32_t)FIM_$INITIAL_STACK_SIZE)));

    {
        startup_context_t *ctx = (startup_context_t *)last_bind_ctx;
        assert(ctx->self_ptr == &ctx->user_data);
        assert(ctx->user_data == 0x00335577);
        assert(ctx->entry_point == 0x00ABCDEF);
        assert(ctx->asid == child()->asid);
    }

    printf("test_startup_context_placement: PASSED\n");
}

/* 0x00E72C12: an empty free list reports status_$proc2_table_full and
 * returns without running the epilogue that overwrites *status_ret. */
static void test_table_full(void)
{
    uid_t uid = { 0, 0 };
    uint16_t upid = 0;
    void *ec = (void *)1;
    status_$t st = 0;

    setup_table();
    P2_FREE_LIST_HEAD = 0;

    run_fork(1, &uid, &upid, &ec, &st);

    assert(st == status_$proc2_table_full);
    assert(n_lock == 1);
    assert(n_unlock == 1);
    assert(n_init_entry == 0);

    printf("test_table_full: PASSED\n");
}

/*
 * Free/allocated list surgery (0x00E72C52-0x00E72C78) and the flag word
 * writes (0x00E72C8A-0x00E72C9C, 0x00E72D6C).
 */
static void test_normal_fork_table_and_flags(void)
{
    uid_t uid = { 0, 0 };
    uint16_t upid = 0;
    void *ec = NULL;
    status_$t st = 0;

    setup_table();
    child()->flags = 0xFFFFu;      /* every bit set, so clears are visible */

    run_fork(1 /* non-zero => not a vfork */, &uid, &upid, &ec, &st);

    /* free list emptied, child pushed onto the allocated list */
    assert(P2_FREE_LIST_HEAD == 0);
    assert(P2_INFO_ALLOC_PTR == CHILD_IDX);
    assert(child()->next_index == PARENT_IDX);
    assert(parent()->pad_14 == CHILD_IDX);   /* back-link, 0x00E72C74 */
    assert(child()->pad_14 == 0);

    /* 0x00E72C7C clears entry+0x1E, then 0x00E72DE8 stores the parent */
    assert(child()->parent_pgroup_idx == PARENT_IDX);

    /* 0x0800 cleared (fork, not vfork), 0x1000 set, 0x8000 cleared,
     * 0x0100 set by the bind */
    assert((child()->flags & PROC2_FLAG_VFORK) == 0);
    assert((child()->flags & PROC2_FLAG_ORPHAN) != 0);
    assert((child()->flags & PROC2_FLAG_INIT) == 0);
    assert((child()->flags & PROC2_FLAG_BOUND) != 0);
    /* 0x00E72DF8 sets the low-byte bit 3 */
    assert((child()->flags & PROC2_FLAG_DEBUG) != 0);

    assert(child()->asid_alt == 0);          /* 0x00E72CF2 */
    assert(child()->asid == 7);              /* MST_$ALLOC_ASID result */
    assert(child()->level1_pid == 9);
    assert(mock_pid_to_index[9] == CHILD_IDX);

    printf("test_normal_fork_table_and_flags: PASSED\n");
}

/* cr_rec (0x68) <- *user_data, cr_rec_2 (0x6C) <- parent's cr_rec_2. */
static void test_cr_rec_sources(void)
{
    uid_t uid; uint16_t upid; void *ec; status_$t st;

    setup_table();
    run_fork(1, &uid, &upid, &ec, &st);

    assert(child()->cr_rec == 0x00335577u);     /* *user_data */
    assert(child()->cr_rec_2 == 0x00220000u);   /* parent's cr_rec_2 */

    printf("test_cr_rec_sources: PASSED\n");
}

/* The child-list splice at 0x00E72DDE / 0x00E72DE4, and the odd
 * 0x18/0x1A pair at 0x00E72DD2 / 0x00E72DD8. */
static void test_child_list_links(void)
{
    uid_t uid; uint16_t upid; void *ec; status_$t st;

    setup_table();
    run_fork(1, &uid, &upid, &ec, &st);

    assert(child()->next_child_sibling == 6);        /* old head */
    assert(parent()->first_child_idx == CHILD_IDX);  /* new head */

    /* both words end up holding the parent's 0x18 */
    assert(child()->pad_18[1] == 0x1234);
    assert(child()->pad_18[0] == 0x1234);
    assert(parent()->pad_18[0] == 0x1234);           /* left untouched */

    printf("test_child_list_links: PASSED\n");
}

/* 0x00E72D80-0x00E72D9E: six longwords, 0x80 deliberately skipped. */
static void test_signal_mask_copy(void)
{
    uid_t uid; uint16_t upid; void *ec; status_$t st;

    setup_table();
    child()->sig_mask_2 = 0x11111111u;

    run_fork(1, &uid, &upid, &ec, &st);

    assert(child()->sig_pending   == 0x70707070u);
    assert(child()->sig_blocked_1 == 0x74747474u);
    assert(child()->sig_blocked_2 == 0x78787878u);
    assert(child()->sig_mask_3    == 0x7C7C7C7Cu);
    assert(child()->sig_mask_1    == 0x84848484u);
    assert(child()->sig_mask_4    == 0x8C8C8C8Cu);
    /* entry+0x80 is NOT copied */
    assert(child()->sig_mask_2    == 0x11111111u);

    printf("test_signal_mask_copy: PASSED\n");
}

/* 0x00E72DA4/0x00E72DBC: propagate flag bits 0x0400 and 0x0004. */
static void test_flag_propagation(void)
{
    uid_t uid; uint16_t upid; void *ec; status_$t st;

    setup_table();
    parent()->flags = 0x0400 | 0x0004;
    child()->flags = 0;

    run_fork(1, &uid, &upid, &ec, &st);
    assert((child()->flags & 0x0400) != 0);
    assert((child()->flags & 0x0004) != 0);

    setup_table();
    parent()->flags = 0;
    child()->flags = 0xFFFFu;

    run_fork(1, &uid, &upid, &ec, &st);
    assert((child()->flags & 0x0400) == 0);
    assert((child()->flags & 0x0004) == 0);

    printf("test_flag_propagation: PASSED\n");
}

/* 0x00E72DEC / 0x00E72DF2: the whole 48-bit clock is recorded. */
static void test_creation_time(void)
{
    uid_t uid; uint16_t upid; void *ec; status_$t st;

    setup_table();
    run_fork(1, &uid, &upid, &ec, &st);

    assert(child()->creation_time_high == 0xDEADBEEFu);
    assert(child()->creation_time_low == 0xCAFE);

    printf("test_creation_time: PASSED\n");
}

/* 0x00E72E0E-0x00E72E2A: accounting info, accounting UID and TTY UID. */
static void test_accounting_copy(void)
{
    uid_t uid; uint16_t upid; void *ec; status_$t st;
    int i;

    setup_table();
    run_fork(1, &uid, &upid, &ec, &st);

    for (i = 0; i < 32; i++) {
        assert((uint8_t)child()->acct_info[i] == 0x5A);
    }
    assert(child()->acct_info_len == 11);
    assert(child()->acct_uid.high == 0xEEEE0000u);
    assert(child()->tty_uid.high == 0xCCCC0000u);

    printf("test_accounting_copy: PASSED\n");
}

/*
 * vfork (*fork_flags == 0): the parent's ASID is kept, the new one is
 * parked as the alternate, and entry+0xDC (stack_uid) is inherited --
 * not the TTY UID.  The vfork path also skips the file/MST work.
 */
static void test_vfork_asid_and_stack_uid(void)
{
    uid_t uid; uint16_t upid; void *ec; status_$t st;

    setup_table();
    child()->tty_uid.high = 0x99990000u;

    run_fork(0 /* vfork */, &uid, &upid, &ec, &st);

    assert((child()->flags & PROC2_FLAG_VFORK) != 0);
    assert(child()->asid_alt == 7);          /* the freshly allocated one */
    assert(child()->asid == 3);              /* the parent's */
    assert(child()->stack_uid.high == 0xAAAA0000u);
    assert(child()->stack_uid.low == 0xBBBB0000u);
    /* the tty_uid copy at 0x00E72E20 still runs, from the parent */
    assert(child()->tty_uid.high == 0xCCCC0000u);
    /* MST_$GET_VA_INFO is on the non-vfork path only */
    assert(n_get_va_info == 0);

    printf("test_vfork_asid_and_stack_uid: PASSED\n");
}

/*
 * 0x00E72F6E / 0x00E72FA0: the two MST_$GET_VA_INFO queries use
 * entry+0x6C and entry+0x68 - 1, and store into entry+0x08 and entry+0xDC.
 */
static void test_get_va_info_arguments(void)
{
    uid_t uid; uint16_t upid; void *ec; status_$t st;

    setup_table();
    run_fork(1, &uid, &upid, &ec, &st);

    assert(n_get_va_info == 2);
    assert(last_va_query[0] == &child()->cr_rec_2);
    assert(last_va_uid_out[0] == &child()->parent_uid);
    assert(last_va_uid_out[1] == &child()->stack_uid);
    /* the second query's VA is a local holding cr_rec - 1 */
    assert(*last_va_query[1] == child()->cr_rec - 1);

    printf("test_get_va_info_arguments: PASSED\n");
}

/*
 * 0x00E72E2C: the eventcount pair is indexed by entry+0x1C, and the fork
 * eventcount starts at -1 (0x00E72E4E).
 */
static void test_eventcount_index(void)
{
    uid_t uid; uint16_t upid; void *ec; status_$t st;

    setup_table();
    child()->self_index = 3;
    /* poison the entry the buggy code would have used (entry+0x24) */
    child()->first_debug_target_idx = 6;
    PROC2_$EC[6 - 1].fork_ec.value = 0x5555;

    run_fork(1, &uid, &upid, &ec, &st);

    assert(PROC2_$EC[3 - 1].fork_ec.value == -1);
    assert(PROC2_$EC[6 - 1].fork_ec.value == 0x5555);  /* untouched */

    printf("test_eventcount_index: PASSED\n");
}

/*
 * 0x00E73138: the eventcount handle is retracted unless the child set
 * flags bit 7 (0x0080).
 */
static void test_ec_handle_retracted(void)
{
    uid_t uid; uint16_t upid; void *ec; status_$t st;

    setup_table();
    run_fork(1, &uid, &upid, &ec, &st);
    assert(ec == NULL);            /* the mock child never sets 0x0080 */
    assert(n_waitn == 1);
    assert(n_resume == 1);

    printf("test_ec_handle_retracted: PASSED\n");
}

/*
 * Failure after PROC1_$BIND: the child is unbound, unlinked from the
 * parent's child list through entry+0x1E / entry+0x22 (0x00E73166), and
 * returned to the free list (0x00E73252).
 */
static void test_bind_failure_cleanup(void)
{
    uid_t uid = { 0, 0 };
    uint16_t upid = 0;
    void *ec = NULL;
    status_$t st = 0;

    setup_table();
    mock_register_status = 0x00190005;   /* fail at EC2_$REGISTER_EC1 */

    run_fork(1, &uid, &upid, &ec, &st);

    /* status keeps its module code but gains bit 31 (low word != 0x19) */
    assert((st & 0xFFFF) == 0x0005);
    assert((st & 0x80000000u) != 0);

    assert(n_unbind == 1);               /* flags had PROC2_FLAG_BOUND */
    assert(n_free_stack == 0);
    assert(n_free_asid == 1);
    assert(n_pgroup_cleanup == 1);

    /* the parent's child-list head is restored from the child's sibling */
    assert(parent()->first_child_idx == 6);

    /* the entry is back on the free list with the system UID */
    assert(P2_FREE_LIST_HEAD == CHILD_IDX);
    assert(P2_INFO_ALLOC_PTR == PARENT_IDX);
    assert((child()->flags & PROC2_FLAG_BOUND) == 0);
    assert(child()->uid.high == proc2_system_uid.high);
    assert(child()->uid.low == proc2_system_uid.low);
    assert(child()->parent_uid.high == 0 && child()->parent_uid.low == 0);
    assert(PROC2_$UID[child()->asid].high == proc2_system_uid.high);

    printf("test_bind_failure_cleanup: PASSED\n");
}

/* 0x00E72FEE: profiling inherits on cleanup_flags bit 11 (0x0800). */
static void test_profil_fork_bit(void)
{
    uid_t uid; uint16_t upid; void *ec; status_$t st;

    setup_table();
    parent()->cleanup_flags = 0x0800;

    run_fork(1, &uid, &upid, &ec, &st);

    assert(n_profil_fork == 1);
    assert((child()->cleanup_flags & 0x0800) != 0);

    setup_table();
    parent()->cleanup_flags = 0x0080;   /* the MSG bit, not the profil bit */
    run_fork(1, &uid, &upid, &ec, &st);
    assert(n_profil_fork == 0);

    printf("test_profil_fork_bit: PASSED\n");
}

/* 0x00E73040/0x00E73064: debug inheritance copies 14 bytes of options. */
static void test_debug_inheritance(void)
{
    uid_t uid; uint16_t upid; void *ec; status_$t st;
    int i;

    setup_table();
    parent()->debugger_idx = 3;
    mock_inherit_ptrace = -1;          /* Domain boolean true */
    for (i = 0; i < 14; i++) {
        parent()->ptrace_opts[i] = (uint8_t)(0x40 + i);
    }

    run_fork(1, &uid, &upid, &ec, &st);

    assert(n_debug_setup == 1);
    for (i = 0; i < 14; i++) {
        assert(child()->ptrace_opts[i] == (uint8_t)(0x40 + i));
    }

    printf("test_debug_inheritance: PASSED\n");
}

/*
 * source-n7i0: MST_$FORK's third parameter is a longword.
 *
 * 0x00E72F4A  pea (-0x2c,A6)          -> &status
 * 0x00E72F4E  movea.l (0x10,A6),A0    -> fork_flags
 * 0x00E72F52  move.l (A0),-(SP)       -> *fork_flags, all 32 bits
 * 0x00E72F54  move.w (-0x4a,A2),-(SP) -> child level1_pid
 * 0x00E72F58  move.w (-0x4e,A2),-(SP) -> child asid
 * 0x00E72F62  lea (0xc,SP),SP         -> 12 bytes, i.e. 2+2+4+4
 *
 * A value with bits set above bit 7 proves the argument is not truncated
 * to a byte the way mst/mst.h used to declare it.
 */
static void test_mst_fork_flags_longword(void)
{
    uid_t uid = { 0, 0 };
    uint16_t upid = 0;
    void *ec = NULL;
    status_$t st = 0;

    setup_table();
    mock_bind_pid = 0x0031;
    mock_alloc_asid_result = 7;

    run_fork((int32_t)0x12345678, &uid, &upid, &ec, &st);

    assert(n_mst_fork == 1);
    assert(last_mst_fork_flags == 0x12345678u);
    assert(last_mst_fork_asid == child()->asid);
    assert(last_mst_fork_pid == child()->level1_pid);

    printf("test_mst_fork_flags_longword: PASSED\n");
}

/*
 * source-hh0j: the MST_$ALLOC_ASID failure branch.
 *
 * 0x00E72CC2  tst.w (0x2,A1)      test the LOW word of the CALLER's status
 * 0x00E72CC6  beq.b 0x00E72CD0
 * 0x00E72CC8  bset.b #0x7,(A1)    set bit 31 of *status_ret
 * 0x00E72CCC  bra.w 0x00E73240    straight into the entry-teardown tail
 *
 * The branch target is 0x00E73240 (cleanup_entry), NOT 0x00E73146
 * (cleanup_locked), so none of the cleanup_locked work runs: the lock is
 * never re-taken, no ASID is freed, no stack is freed and no cleanup
 * handlers run.  PROC2_$INIT_ENTRY_INTERNAL (0x00E72CFA) has not run
 * either, because the failure is detected before it.
 *
 * The status the caller ends up with is deliberately NOT asserted: the
 * shared exit at 0x00E732D6 copies the A6-0x2C frame local over
 * *status_ret, and no instruction on this path writes that local (see the
 * trace in proc2/fork.c).  The value is whatever the previous frame left
 * behind -- an original bug we reproduce rather than repair.
 */
static void test_alloc_asid_failure_goes_to_entry_teardown(void)
{
    uid_t uid = { 0, 0 };
    uint16_t upid = 0;
    void *ec = NULL;
    status_$t st = 0;
    uint16_t free_head_before;

    setup_table();
    mock_alloc_asid_status = 0x00040006; /* status_$no_asid_available */
    mock_alloc_asid_result = 0;

    free_head_before = P2_FREE_LIST_HEAD;
    assert(free_head_before == CHILD_IDX);

    run_fork(1, &uid, &upid, &ec, &st);

    /* The entry was taken off the free list and handed straight back. */
    assert(P2_FREE_LIST_HEAD == CHILD_IDX);

    /* cleanup_entry ran ... */
    assert(n_pgroup_cleanup == 1);
    assert(n_unlock == 1);
    assert(n_lock == 1);

    /* ... and cleanup_locked did not. */
    assert(n_free_asid == 0);
    assert(n_unbind == 0);
    assert(n_free_stack == 0);
    assert(n_cleanup_handlers == 0);

    /* The failure is detected before the entry is initialised or forked. */
    assert(n_init_entry == 0);
    assert(n_mst_fork == 0);

    printf("test_alloc_asid_failure_goes_to_entry_teardown: PASSED\n");
}

int main(void)
{
    printf("Running PROC2_$FORK tests...\n\n");

    test_startup_context_placement();
    test_table_full();
    test_normal_fork_table_and_flags();
    test_cr_rec_sources();
    test_child_list_links();
    test_signal_mask_copy();
    test_flag_propagation();
    test_creation_time();
    test_accounting_copy();
    test_vfork_asid_and_stack_uid();
    test_get_va_info_arguments();
    test_eventcount_index();
    test_ec_handle_retracted();
    test_bind_failure_cleanup();
    test_profil_fork_bit();
    test_debug_inheritance();
    test_mst_fork_flags_longword();
    test_alloc_asid_failure_goes_to_entry_teardown();

    printf("\nAll tests PASSED!\n");
    return 0;
}
