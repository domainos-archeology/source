/*
 * xpd/test/test_memory.c - XPD_$COPY_MEMORY and the five entry points over
 * it (memory.c), and XPD_$INIT (init.c).
 */

#include <stdio.h>
#include <string.h>

#include "xpd/xpd_internal.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %-48s ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: %lld, Got: %lld at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

uint16_t PROC1_$CURRENT;
uint16_t PROC1_$AS_ID;
static proc2_info_t mock_entries[8];
proc2_info_t *P2_INFO_TABLE = mock_entries;
static uint16_t pid_to_index[64];
uint16_t *PROC2_$PID_TO_INDEX = pid_to_index;
status_$t FIM_$TRACE_STS[64];
void *PTR_PROC2_$DATA = mock_entries;

static int lock_held;
void ML_$LOCK(int16_t id)   { (void)id; lock_held++; }
void ML_$UNLOCK(int16_t id) { (void)id; lock_held--; }

static status_$t cleanup_status;
static int rls_calls, pop_calls;
static void *cleanup_rec;
status_$t FIM_$CLEANUP(void *handler) { cleanup_rec = handler; return cleanup_status; }
void FIM_$RLS_CLEANUP(void *d) { rls_calls++; ASSERT_EQ((long long)(intptr_t)cleanup_rec, (long long)(intptr_t)d); }
void FIM_$POP_SIGNAL(void *d) { (void)d; pop_calls++; }

static int set_asid_calls;
static uint16_t set_asid_log[16];
static uint16_t asid_now;
void PROC1_$SET_ASID(uint16_t asid) { if (set_asid_calls < 16) set_asid_log[set_asid_calls] = asid; set_asid_calls++; asid_now = asid; }

/* the copy is real; a "guard fault" is simulated by an address range */
static const uint8_t *guard_lo, *guard_hi;
static uint16_t guard_asid;
void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    memcpy(dst, src, len);
    if ((const uint8_t *)src >= guard_lo && (const uint8_t *)src < guard_hi && asid_now == guard_asid)
        FIM_$TRACE_STS[asid_now] = status_$mst_guard_fault;
    if ((const uint8_t *)dst >= guard_lo && (const uint8_t *)dst < guard_hi && asid_now == guard_asid)
        FIM_$TRACE_STS[asid_now] = status_$mst_guard_fault;
}

static int16_t xfind_result;
static status_$t xfind_status;
int16_t XPD_$FIND_INDEX(uid_t *u, status_$t *st) { (void)u; *st = xfind_status; return xfind_result; }
int16_t PROC2_$FIND_INDEX(uid_t *u, status_$t *st) { (void)u; *st = xfind_status; return xfind_result; }
static int8_t acl_result;
static int16_t *acl_a, *acl_b;
int8_t ACL_$CHECK_DEBUG_RIGHTS(int16_t *a, int16_t *b) { acl_a = a; acl_b = b; return acl_result; }

static int wire_calls;
static const void *wire_start, *wire_end, *wire_max;
void MST_$WIRE_AREA(const void *s, const void *e, void *pl, const void *m, void *pc)
{ (void)pl; wire_calls++; wire_start = s; wire_end = e; wire_max = m; *(uint16_t *)pc = 2; }
void OS_$DATA_ZERO(void *p, uint32_t len) { memset(p, 0, len); }
static int ec_init_calls;
static ec_$eventcount_t *ec_init_log[70];
void EC_$INIT(ec_$eventcount_t *ec) { if (ec_init_calls < 70) ec_init_log[ec_init_calls] = ec; ec_init_calls++; }

#include "../xpd_data.c"
#include "../memory.c"
#include "../init.c"

static uint8_t src_area[0x900], dst_area[0x900];

static void reset(void)
{
    int i;
    memset(mock_entries, 0, sizeof(mock_entries));
    memset(FIM_$TRACE_STS, 0x55, sizeof(FIM_$TRACE_STS));
    for (i = 0; i < 0x900; i++) { src_area[i] = (uint8_t)(i * 7); dst_area[i] = 0; }
    PROC1_$CURRENT = 3; PROC1_$AS_ID = 5;
    pid_to_index[3] = 2;
    asid_now = 5;
    cleanup_status = status_$fault_cleanup_in_progress;
    rls_calls = pop_calls = 0;
    set_asid_calls = 0;
    guard_lo = guard_hi = NULL; guard_asid = 0;
    xfind_result = 4; xfind_status = status_$ok;
    mock_entries[3].asid = 9; mock_entries[3].debugger_idx = 2; mock_entries[3].level1_pid = 0x1F;
    acl_result = 0;
    lock_held = 0;
    wire_calls = 0; ec_init_calls = 0;
}

TEST(copy_in_chunks)
{
    status_$t st = 0x55;
    reset();
    XPD_$COPY_MEMORY(7, dst_area, 9, src_area, 0x900, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, memcmp(src_area, dst_area, 0x900));
    /* 3 chunks: src, dst, src, dst, src, dst, then back to 5 */
    ASSERT_EQ(7, set_asid_calls);
    ASSERT_EQ(9, set_asid_log[0]); ASSERT_EQ(7, set_asid_log[1]);
    ASSERT_EQ(9, set_asid_log[4]); ASSERT_EQ(7, set_asid_log[5]);
    ASSERT_EQ(5, set_asid_log[6]);
    ASSERT_EQ(1, rls_calls); ASSERT_EQ(0, pop_calls);
    ASSERT_EQ(XPD_TRACE_STS_DONE, FIM_$TRACE_STS[7]); ASSERT_EQ(XPD_TRACE_STS_DONE, FIM_$TRACE_STS[9]);
    ASSERT_EQ(0x55555555, FIM_$TRACE_STS[5]);
}

TEST(copy_zero_length_and_no_asid_change)
{
    status_$t st = 0x55;
    reset();
    XPD_$COPY_MEMORY(5, dst_area, 5, src_area, 0, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0, set_asid_calls);           /* never switched, never restored */
    ASSERT_EQ(0, dst_area[0]);
    XPD_$COPY_MEMORY(5, dst_area, 5, src_area, 3, &st);
    ASSERT_EQ(2, set_asid_calls);           /* 5, 5 - and no restore */
    ASSERT_EQ(0, memcmp(src_area, dst_area, 3));
}

TEST(copy_guard_fault_on_read_and_write)
{
    status_$t st = 0x55;
    reset();
    guard_lo = src_area + 0x400; guard_hi = src_area + 0x500; guard_asid = 9;
    XPD_$COPY_MEMORY(7, dst_area, 9, src_area, 0x900, &st);
    ASSERT_EQ(status_$mst_guard_fault, st);
    ASSERT_EQ(0, memcmp(src_area, dst_area, 0x400));
    ASSERT_EQ(0, dst_area[0x400]);          /* the second chunk never landed */
    ASSERT_EQ(XPD_TRACE_STS_DONE, FIM_$TRACE_STS[9]);
    ASSERT_EQ(1, rls_calls);
    ASSERT_EQ(5, set_asid_log[set_asid_calls - 1]);

    reset();
    guard_lo = dst_area; guard_hi = dst_area + 0x10; guard_asid = 7;
    XPD_$COPY_MEMORY(7, dst_area, 9, src_area, 0x900, &st);
    ASSERT_EQ(status_$mst_guard_fault, st);
    ASSERT_EQ(2, set_asid_calls - 1);       /* src, dst, then the restore */
}

TEST(copy_after_a_fault_unwound)
{
    status_$t st = 0x55;
    reset();
    cleanup_status = 0x00120001;
    XPD_$COPY_MEMORY(7, dst_area, 9, src_area, 0x900, &st);
    ASSERT_EQ(0x00120001, st);
    ASSERT_EQ(0, set_asid_calls);
    ASSERT_EQ(1, pop_calls); ASSERT_EQ(0, rls_calls);
    ASSERT_EQ(XPD_TRACE_STS_DONE, FIM_$TRACE_STS[7]); ASSERT_EQ(XPD_TRACE_STS_DONE, FIM_$TRACE_STS[9]);
}

TEST(read_and_write_proc)
{
    uid_t u = { 1, 1 };
    int32_t len = 0x10;
    status_$t st = 0x55;
    reset();
    XPD_$READ_PROC(&u, src_area, &len, dst_area, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(9, set_asid_log[0]);          /* source = the target's AS */
    ASSERT_EQ(5, set_asid_log[1]);
    ASSERT_EQ(0, memcmp(src_area, dst_area, 0x10));
    ASSERT_EQ(0, lock_held);

    reset();
    XPD_$WRITE_PROC(&u, dst_area, &len, src_area, &st);
    ASSERT_EQ(5, set_asid_log[0]);          /* source = us */
    ASSERT_EQ(9, set_asid_log[1]);
    ASSERT_EQ(0, memcmp(src_area, dst_area, 0x10));

    reset();
    xfind_status = status_$xpd_target_not_suspended;
    XPD_$READ_PROC(&u, src_area, &len, dst_area, &st);
    ASSERT_EQ(status_$xpd_target_not_suspended, st);
    ASSERT_EQ(0, set_asid_calls);
}

TEST(read_and_write_by_asid)
{
    uint16_t asid = 0x21;
    int32_t len = 8;
    status_$t st = 0x55;
    reset();
    XPD_$READ(&asid, src_area, &len, dst_area, &st);
    ASSERT_EQ(0x21, set_asid_log[0]); ASSERT_EQ(5, set_asid_log[1]);
    reset();
    XPD_$WRITE(&asid, dst_area, &len, src_area, &st);
    ASSERT_EQ(5, set_asid_log[0]); ASSERT_EQ(0x21, set_asid_log[1]);
    ASSERT_EQ(0, memcmp(src_area, dst_area, 8));
}

TEST(read_proc_async_rights)
{
    uid_t u = { 1, 1 };
    int32_t len = 8;
    status_$t st = 0x55;
    reset();
    /* the debugger: no ACL check */
    XPD_$READ_PROC_ASYNC(&u, src_area, &len, dst_area, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(2, set_asid_calls);
    /* not the debugger, rights granted */
    reset();
    mock_entries[3].debugger_idx = 6;
    acl_result = -1;
    XPD_$READ_PROC_ASYNC(&u, src_area, &len, dst_area, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ((long long)(intptr_t)&PROC1_$CURRENT, (long long)(intptr_t)acl_a);
    ASSERT_EQ((long long)(intptr_t)&mock_entries[3].level1_pid, (long long)(intptr_t)acl_b);
    /* rights denied */
    reset();
    mock_entries[3].debugger_idx = 6;
    XPD_$READ_PROC_ASYNC(&u, src_area, &len, dst_area, &st);
    ASSERT_EQ(status_$proc2_permission_denied, st);
    ASSERT_EQ(0, set_asid_calls);
    ASSERT_EQ(0, lock_held);
}

TEST(init)
{
    reset();
    memset(XPD_$DATA, 0xEE, sizeof(XPD_$DATA));
    XPD_$INIT();
    ASSERT_EQ(1, wire_calls);
    ASSERT_EQ((long long)(intptr_t)&PTR_XPD_$DATA, (long long)(intptr_t)wire_start);
    ASSERT_EQ((long long)(intptr_t)&PTR_PROC2_$DATA, (long long)(intptr_t)wire_end);
    ASSERT_EQ((long long)(intptr_t)&xpd_$wire_limit, (long long)(intptr_t)wire_max);
    ASSERT_EQ(3, xpd_$wire_limit);
    ASSERT_EQ(0, XPD_$DATA[0]); ASSERT_EQ(0, XPD_$DATA[XPD_DATA_SIZE - 1]);
    ASSERT_EQ(64, ec_init_calls);
    ASSERT_EQ((long long)(intptr_t)XPD_$DATA, (long long)(intptr_t)ec_init_log[0]);
    ASSERT_EQ((long long)(intptr_t)(XPD_$DATA + 57 * XPD_TARGET_RECORD_SIZE), (long long)(intptr_t)ec_init_log[57]);
    ASSERT_EQ((long long)(intptr_t)&XPD_DEBUGGER(1)->ec, (long long)(intptr_t)ec_init_log[58]);
    ASSERT_EQ((long long)(intptr_t)&XPD_DEBUGGER(6)->ec, (long long)(intptr_t)ec_init_log[63]);
}

int main(void)
{
    printf("XPD memory / init tests\n");
    RUN_TEST(copy_in_chunks);
    RUN_TEST(copy_zero_length_and_no_asid_change);
    RUN_TEST(copy_guard_fault_on_read_and_write);
    RUN_TEST(copy_after_a_fault_unwound);
    RUN_TEST(read_and_write_proc);
    RUN_TEST(read_and_write_by_asid);
    RUN_TEST(read_proc_async_rights);
    RUN_TEST(init);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
