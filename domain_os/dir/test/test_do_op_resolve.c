/*
 * dir/test/test_do_op_resolve.c - unit tests for dir_$do_op_resolve
 * (0x00E4D0E2), focused on the timeout guard bead source-2tmk reported.
 *
 * The guard is three nested conditions in the image:
 *   0x00E4D12E  `cmpi.w #0x9,(-0x2,A1,D0w*1)` / `seq D5b`
 *               A1 = 0xE2612C, D0w = 2 * PROC1_$CURRENT, so the cell is
 *               PROC1_$DATA.type[PROC1_$CURRENT] off the 0xE2612A base
 *               proc1.h declares.  D5 is 0xFF only for a type 9 process.
 *   0x00E4D1A0  `tst.b D5b` / `bpl`  - a NON-type-9 process skips the check
 *   0x00E4D1A4  `cmpi.w #0x1,D2w` / `bls` - the FIRST component skips it
 *   0x00E4D1B2  `cmpi.l #0x14,D0` / `bgt 0x00e4d274` - strictly more than
 *               0x14 ticks sets flags2 (`st (A1)` at 0x00E4D278) and leaves
 *
 * Every routine dir_$do_op_resolve calls is mocked; the pathname components
 * simply resolve to fresh UIDs so the walk keeps going.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %s... ", #name);          \
    current_failed = 0;                         \
    test_##name();                              \
    if (current_failed) { tests_failed++; }     \
    else { tests_passed++; printf("PASSED\n"); }\
} while (0)

#define ASSERT_EQ(expected, actual) do {                                 \
    unsigned long long _e = (unsigned long long)(expected);              \
    unsigned long long _a = (unsigned long long)(actual);                \
    if (_e != _a) {                                                      \
        printf("FAILED\n    Expected 0x%llx, got 0x%llx at line %d\n",   \
               _e, _a, __LINE__);                                        \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "dir/dir_internal.h"

/* ------------------------------------------------------------------ */
/* Globals the routine reads                                            */
/* ------------------------------------------------------------------ */

uint16_t     PROC1_$CURRENT;
#include "proc1/proc1.h"
MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);
uint32_t     TIME_$CLOCKH;
uid_t        UID_$NIL = { 0, 0 };
name_$data_t NAME_$DATA;

/* The process type the guard looks for (`cmpi.w #0x9` at 0x00E4D128). */
#define PROC1_TYPE_SERVER   9

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

/* How much TIME_$CLOCKH advances per resolved component. */
static uint32_t mock_ticks_per_component;
static int      mock_get_entryu_calls;
static int      mock_read_linku_calls;
static int      mock_parent_calls;
static status_$t mock_get_entryu_status;
static uint16_t mock_entry_type;

void dir_$do_op_get_entryu(uid_t *uid, void *name, uint16_t name_len,
                           uint16_t *type_ret, uid_t *uid_ret,
                           uint32_t *extra_ret, status_$t *status_ret)
{
    (void)name; (void)name_len;
    mock_get_entryu_calls++;
    /* A distinct child uid each time, so the walk never repeats a uid. */
    uid_ret->high = uid->high + 1;
    uid_ret->low  = uid->low + 1;
    *type_ret  = mock_entry_type;
    *extra_ret = 0;
    *status_ret = mock_get_entryu_status;
    TIME_$CLOCKH += mock_ticks_per_component;
}

void dir_$do_op_read_linku(uid_t *uid, void *name, uint16_t name_len,
                           uint16_t buf_len, uint32_t extra, void *link_type_ret,
                           uid_t *uid_ret, status_$t *status_ret)
{
    (void)uid; (void)name; (void)name_len; (void)buf_len; (void)extra;
    (void)link_type_ret; (void)uid_ret;
    mock_read_linku_calls++;
    *status_ret = status_$ok;
}

void dir_$get_parent_uid(uid_t *uid, status_$t *status_ret)
{
    (void)uid;
    mock_parent_calls++;
    *status_ret = status_$ok;
}

int8_t DIR_$IS_RETRYABLE_STATUS(status_$t status)
{
    (void)status;
    return 0;
}

#include "../do_op_resolve.c"

/* ------------------------------------------------------------------ */
/* Harness                                                              */
/* ------------------------------------------------------------------ */

/* Everything dir_$do_op_resolve's fourteen parameters point at. */
typedef struct {
    uid_t    dir_uid;           /* the `result` record's leading uid */
    uint32_t extra;
    uint32_t parent[2];
    uint8_t  flags1;
    uint8_t  flags2;
    uint16_t cont;
    uint16_t size;
    uint16_t last_start;
    uint16_t last_size;
    uint16_t link_count;
    status_$t status;
} resolve_out_t;

static void reset_mocks(void)
{
    memset(PROC1_$DATA.type, 0, sizeof(PROC1_$DATA.type));
    memset(&NAME_$DATA, 0, sizeof(NAME_$DATA));
    /* Keep root and node uids away from anything the walk produces. */
    NAME_$DATA.root_uid.high = 0x0F0F0001u;
    NAME_$DATA.root_uid.low  = 0x0F0F0002u;
    NAME_$DATA.node_uid.high = 0x0F0F0003u;
    NAME_$DATA.node_uid.low  = 0x0F0F0004u;
    PROC1_$CURRENT = 3;
    TIME_$CLOCKH = 0x1000;
    mock_ticks_per_component = 0;
    mock_get_entryu_calls = 0;
    mock_read_linku_calls = 0;
    mock_parent_calls = 0;
    mock_get_entryu_status = status_$ok;
    mock_entry_type = 2;
}

/*
 * Runs the walk over `path` (1-based, as the image addresses it) and returns
 * the outputs.  The uid the walk starts from is deliberately not the root or
 * node uid, so the '..' and cross-node arms stay out of the way.
 */
#define PATH_BUF_VA     0x00010000u

static void run_resolve(const char *path, resolve_out_t *out)
{
    static char buf[64];

    memset(out, 0, sizeof(*out));
    out->dir_uid.high = 0x11110000u;
    out->dir_uid.low  = 0x22220000u;
    out->cont = 1;

    memset(buf, 0, sizeof(buf));
    strncpy(buf, path, sizeof(buf) - 1);

    /* The pathname reaches dir_$do_op_resolve as a target VA
     * (`move.l (0x8e,A2),-(SP)` in DIR_$DO_OP's case 0x58, 0x00E4C8CA), so
     * the host arena base has to be set up before the call - a 64-bit host
     * pointer does not survive the uint32_t field otherwise. */
    ARCH_HOST_VA_BASE = (uintptr_t)buf - PATH_BUF_VA;

    dir_$do_op_resolve(PATH_BUF_VA, (uint16_t)strlen(buf),
                       &out->dir_uid, &out->extra, out->parent,
                       &out->flags1, &out->flags2,
                       &out->cont, &out->size,
                       &out->last_start, &out->last_size,
                       0x1e, &out->link_count, &out->status);
}

/* ------------------------------------------------------------------ */

/*
 * 0x00E4D12E: the guard's boolean is `PROC1_$DATA.type[PROC1_$CURRENT] == 9`.
 * A type 1 process walks the whole path however long the clock runs.
 */
TEST(non_type9_process_never_times_out)
{
    resolve_out_t out;

    reset_mocks();
    PROC1_$DATA.type[PROC1_$CURRENT] = 1;
    mock_ticks_per_component = 0x1000;   /* far past 0x14 */

    run_resolve("a/b/c/d", &out);

    ASSERT_EQ(status_$ok, out.status);
    ASSERT_EQ(0, out.flags2);
    ASSERT_EQ(4, mock_get_entryu_calls);
}

/*
 * 0x00E4D1A4 `cmpi.w #0x1,D2w` / `bls`: the check is skipped while the
 * component counter is 0 or 1, so a single-component path resolves even for
 * a type 9 process whose clock has run away.
 */
TEST(type9_first_component_is_exempt)
{
    resolve_out_t out;

    reset_mocks();
    PROC1_$DATA.type[PROC1_$CURRENT] = PROC1_TYPE_SERVER;
    mock_ticks_per_component = 0x1000;

    run_resolve("a", &out);

    ASSERT_EQ(status_$ok, out.status);
    ASSERT_EQ(0, out.flags2);
    ASSERT_EQ(1, mock_get_entryu_calls);
}

/*
 * 0x00E4D1B2 / 0x00E4D274: from the SECOND component on, an elapsed time of
 * more than 0x14 ticks sets flags2 and abandons the walk with status ok.
 */
TEST(type9_times_out_on_the_second_component)
{
    resolve_out_t out;

    reset_mocks();
    PROC1_$DATA.type[PROC1_$CURRENT] = PROC1_TYPE_SERVER;
    mock_ticks_per_component = 0x15;     /* > 0x14 after one component */

    run_resolve("a/b/c/d", &out);

    ASSERT_EQ(status_$ok, out.status);
    ASSERT_EQ(0xFF, out.flags2);
    /* The first component resolved; the second tripped the guard before
     * its own lookup. */
    ASSERT_EQ(1, mock_get_entryu_calls);
}

/*
 * `bgt` is a strictly-greater test, so exactly 0x14 ticks is still inside
 * the budget.  Advancing 0x14 per component means the elapsed time at the
 * Nth component is 0x14*(N-1); the walk survives until that exceeds 0x14,
 * i.e. it resolves components 1 and 2 and trips on the third.
 */
TEST(type9_boundary_is_strictly_greater_than_0x14)
{
    resolve_out_t out;

    reset_mocks();
    PROC1_$DATA.type[PROC1_$CURRENT] = PROC1_TYPE_SERVER;
    mock_ticks_per_component = 0x14;

    run_resolve("a/b/c/d", &out);

    ASSERT_EQ(status_$ok, out.status);
    ASSERT_EQ(0xFF, out.flags2);
    ASSERT_EQ(2, mock_get_entryu_calls);
}

/* A type 9 process whose clock does not move resolves the whole path. */
TEST(type9_within_budget_completes)
{
    resolve_out_t out;

    reset_mocks();
    PROC1_$DATA.type[PROC1_$CURRENT] = PROC1_TYPE_SERVER;
    mock_ticks_per_component = 0;

    run_resolve("a/b/c/d", &out);

    ASSERT_EQ(status_$ok, out.status);
    ASSERT_EQ(0, out.flags2);
    ASSERT_EQ(0xFF, out.flags1);      /* 0x00E4D0F8: `st` on entry */
    ASSERT_EQ(4, mock_get_entryu_calls);
}

/*
 * The guard reads PROC1_$TYPE at index PROC1_$CURRENT, not PROC1_$CURRENT-1
 * (0x00E4D120 `movea.l #0xe2612c,A1` with the -0x2 displacement folded in).
 * Planting 9 one slot low must NOT arm the timeout.
 */
TEST(guard_index_is_proc1_current_not_current_minus_one)
{
    resolve_out_t out;

    reset_mocks();
    PROC1_$DATA.type[PROC1_$CURRENT - 1] = PROC1_TYPE_SERVER;
    PROC1_$DATA.type[PROC1_$CURRENT] = 1;
    mock_ticks_per_component = 0x1000;

    run_resolve("a/b/c/d", &out);

    ASSERT_EQ(0, out.flags2);
    ASSERT_EQ(4, mock_get_entryu_calls);
}

int main(void)
{
    printf("=== dir_$do_op_resolve timeout guard tests ===\n");
    RUN_TEST(non_type9_process_never_times_out);
    RUN_TEST(type9_first_component_is_exempt);
    RUN_TEST(type9_times_out_on_the_second_component);
    RUN_TEST(type9_boundary_is_strictly_greater_than_0x14);
    RUN_TEST(type9_within_budget_completes);
    RUN_TEST(guard_index_is_proc1_current_not_current_minus_one);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
