/*
 * area/test/test_copy.c - Unit tests for AREA_$COPY (0x00E0901A)
 *
 * The test #includes area/copy.c directly and drives the real AREA_$COPY
 * through mocked callees.  It pins down the three defects bead source-0gzh
 * found, plus the parts of the walk that surround them:
 *
 *   - area_$get_aste (0x00E092A4/0x00E092D0) and AST_$COPY_AREA
 *     (0x00E0930C) are all handed the CALLER's status cell with `pea (A4)`,
 *     and the tests are `tst.l (A4)`; a copy failure therefore reaches the
 *     caller.  The tree used a local and swallowed it.
 *   - a source virt_size of 0 (0x00E090FA `beq 0x00E0937A`) does NOT return
 *     early: it still clears AREA_FLAG_IN_TRANS on the source entry and
 *     advances AREA_$IN_TRANS_EC.
 *   - area_$internal_delete's second argument is `move.w (0x2a,A0),-(SP)`
 *     with A0 = the DESTINATION entry, i.e. dst_entry->reserved_2a, not the
 *     new area id (0x00E09348).
 *   - the three early failures return the frame slot at A6-0x2C, which is
 *     only ever written at 0x00E090D4; the C models that slot as zero.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <setjmp.h>

/* Avoid the macOS uid_t conflict - must come AFTER the system includes. */
#define uid_t area_uid_t

#include "area/area_internal.h"
#include "misc/crash_system.h"

/* ==========================================================================
 * Test infrastructure
 * ========================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

static void reset_mocks(void);

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                                                   \
    printf("  Running %-46s", #name);                                         \
    current_failed = 0;                                                       \
    reset_mocks();                                                            \
    test_##name();                                                            \
    if (current_failed == 0) { tests_passed++; printf("PASSED\n"); }          \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
    if ((unsigned long long)(expected) != (unsigned long long)(actual)) {      \
        printf("FAILED\n    Expected: 0x%llx, Got: 0x%llx at line %d\n",       \
               (unsigned long long)(expected),                                \
               (unsigned long long)(actual), __LINE__);                       \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

#define ASSERT_TRUE(cond) do {                                                \
    if (!(cond)) {                                                            \
        printf("FAILED\n    Assertion failed at line %d: %s\n",               \
               __LINE__, #cond);                                              \
        tests_failed++; current_failed = 1;                                   \
        return;                                                               \
    }                                                                         \
} while (0)

/* ==========================================================================
 * Mock area table
 *
 * AREA_TABLE_BASE is the fixed image address 0xD94C00 on the target; rebind
 * it to a host array so AREA_ID_TO_ENTRY works here.
 * ========================================================================== */

static area_$entry_t mock_area_table[AREA_MAX_ENTRIES];

#undef AREA_TABLE_BASE
#define AREA_TABLE_BASE ((uintptr_t)mock_area_table)

/*
 * area_$entry_t is 0x30 bytes on the m68k target but wider on a 64-bit host
 * (`next` and `prev` are 8 bytes each there), so AREA_ID_TO_ENTRY's constant
 * stride would not land on mock_area_table[] elements.  Rebind the stride to
 * the host's own sizeof; the field offsets inside a record are still the ones
 * the disassembly names, which is what these tests check.
 */
#undef AREA_ENTRY_SIZE
#define AREA_ENTRY_SIZE ((int)sizeof(area_$entry_t))

/* ==========================================================================
 * Module globals and cross-subsystem storage
 * ========================================================================== */

area_$globals_t AREA_$GLOBALS;
status_$t       Area_Internal_Error = 0x0032000A;

as_$info_t AS_$INFO;
uint16_t   PROC1_$AS_ID;

/* ==========================================================================
 * Mocked callees
 * ========================================================================== */

static int      lock_calls;
static int      unlock_calls;
static int16_t  last_lock_id;
static int16_t  last_unlock_id;

void ML_$LOCK(int16_t resource_id)
{
    lock_calls++;
    last_lock_id = resource_id;
}

void ML_$UNLOCK(int16_t resource_id)
{
    unlock_calls++;
    last_unlock_id = resource_id;
}

static int                 ec_advance_calls;
static ec_$eventcount_t   *ec_advance_last;

void EC_$ADVANCE(ec_$eventcount_t *ec)
{
    ec_advance_calls++;
    ec_advance_last = ec;
}

static int wait_in_trans_calls;

void area_$wait_in_trans(void)
{
    wait_in_trans_calls++;
    /* Clear the bit so the wait loop terminates. */
    mock_area_table[0].flags &= (uint16_t)~AREA_FLAG_IN_TRANS;
}

/* area_$internal_create */
static int       create_calls;
static uint32_t  create_result;
static status_$t create_status;
static uint32_t  create_virt;
static uint32_t  create_commit;
static int16_t   create_owner;
static boolean   create_shared;

uint32_t area_$internal_create(uint32_t virt_size, uint32_t commit_size,
                               uint32_t remote_uid, int16_t owner_asid,
                               int16_t alloc_remote, boolean shared,
                               status_$t *status_p)
{
    (void)remote_uid;
    (void)alloc_remote;
    create_calls++;
    create_virt = virt_size;
    create_commit = commit_size;
    create_owner = owner_asid;
    create_shared = shared;
    *status_p = create_status;
    return create_result;
}

/* area_$internal_delete */
static int             delete_calls;
static area_$entry_t  *delete_entry;
static int16_t         delete_area_id;
static status_$t      *delete_status_ptr;
static boolean         delete_unlink;

void area_$internal_delete(area_$entry_t *entry, int16_t area_id,
                           status_$t *status_p, boolean do_unlink)
{
    delete_calls++;
    delete_entry = entry;
    delete_area_id = area_id;
    delete_status_ptr = status_p;
    delete_unlink = do_unlink;
    *status_p = 0;
}

/* area_$get_aste */
#define MAX_GET_ASTE 16
static int                get_aste_calls;
static int16_t            get_aste_area_id[MAX_GET_ASTE];
static area_$seg_slot_t  *get_aste_slot[MAX_GET_ASTE];
static int16_t            get_aste_seg[MAX_GET_ASTE];
static int8_t             get_aste_wait[MAX_GET_ASTE];
static int8_t             get_aste_create[MAX_GET_ASTE];
static status_$t         *get_aste_status_ptr[MAX_GET_ASTE];
static aste_t             mock_astes[MAX_GET_ASTE];

aste_t *area_$get_aste(int16_t area_id, area_$seg_slot_t *slot,
                       int16_t seg_idx, int8_t wait, int8_t create,
                       status_$t *status_p)
{
    int i = get_aste_calls;

    if (i < MAX_GET_ASTE) {
        get_aste_area_id[i] = area_id;
        get_aste_slot[i] = slot;
        get_aste_seg[i] = seg_idx;
        get_aste_wait[i] = wait;
        get_aste_create[i] = create;
        get_aste_status_ptr[i] = status_p;
    }
    get_aste_calls++;
    /* The image never writes the status here on the paths under test. */
    return &mock_astes[i < MAX_GET_ASTE ? i : 0];
}

/* AST_$COPY_AREA */
static int        copy_area_calls;
static uint16_t   copy_area_area_id;
static uint16_t   copy_area_unused;
static aste_t    *copy_area_src;
static aste_t    *copy_area_dst;
static uint16_t   copy_area_start_seg;
static char      *copy_area_buffer;
static status_$t *copy_area_status_ptr;
static status_$t  copy_area_status;

void AST_$COPY_AREA(uint16_t area_id, uint16_t unused, aste_t *src_aste,
                    aste_t *dst_aste, uint16_t start_seg, char *buffer,
                    status_$t *status)
{
    copy_area_calls++;
    copy_area_area_id = area_id;
    copy_area_unused = unused;
    copy_area_src = src_aste;
    copy_area_dst = dst_aste;
    copy_area_start_seg = start_seg;
    copy_area_buffer = buffer;
    copy_area_status_ptr = status;
    *status = copy_area_status;
}

/* CRASH_SYSTEM - never returns in the image; longjmp out here. */
static jmp_buf   crash_jmp;
static int       crash_calls;
static status_$t crash_status;

void CRASH_SYSTEM(const status_$t *status_p)
{
    crash_calls++;
    crash_status = *status_p;
    longjmp(crash_jmp, 1);
}

/* M$OIS$WLW - the signed 16-bit modulus helper (math/math.h) */
short M$OIS$WLW(long dividend, short divisor)
{
    return (short)(dividend % divisor);
}

/* ==========================================================================
 * The unit under test
 * ========================================================================== */

#include "../copy.c"

/* ==========================================================================
 * Fixtures
 * ========================================================================== */

#define SRC_ID  1
#define DST_ID  2

static area_$entry_t *src_entry(void) { return &mock_area_table[SRC_ID - 1]; }
static area_$entry_t *dst_entry(void) { return &mock_area_table[DST_ID - 1]; }

static area_$seg_slot_t *slots_of(area_$entry_t *e)
{
    return (area_$seg_slot_t *)&e->seg_bitmap[0];
}

static void reset_mocks(void)
{
    memset(mock_area_table, 0, sizeof(mock_area_table));
    memset(&AREA_$GLOBALS, 0, sizeof(AREA_$GLOBALS));
    memset(&AS_$INFO, 0, sizeof(AS_$INFO));
    memset(mock_astes, 0, sizeof(mock_astes));

    lock_calls = unlock_calls = 0;
    last_lock_id = last_unlock_id = -1;
    ec_advance_calls = 0;
    ec_advance_last = NULL;
    wait_in_trans_calls = 0;
    create_calls = 0;
    create_result = ((uint32_t)0x1234 << 16) | DST_ID;
    create_status = status_$ok;
    delete_calls = 0;
    delete_entry = NULL;
    delete_area_id = 0;
    delete_status_ptr = NULL;
    delete_unlink = 0;
    get_aste_calls = 0;
    copy_area_calls = 0;
    copy_area_status = status_$ok;
    crash_calls = 0;

    PROC1_$AS_ID = 0;               /* skip the ownership test by default */
    AS_$STACK_LOW = 0;
    AREA_$N_AREAS = AREA_MAX_ENTRIES;
}

/*
 * A source area with one 32K segment whose only bitmap bit (segment 0) is
 * set, so exactly one AST_$COPY_AREA call happens.
 */
static void make_one_segment_source(void)
{
    area_$entry_t *e = src_entry();

    e->flags = AREA_FLAG_ACTIVE;
    e->generation = 0x55;
    e->virt_size = 0x8000;          /* 32K -> bitmap_bytes == 1 */
    e->commit_size = 0x1000;
    e->first_seg_index = 9;
    e->owner_asid = 3;
    slots_of(e)->bits = 0x01;       /* segment 0 allocated */
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* 0x00E09034-0x00E0906C */
TEST(bad_area_id_returns_not_active)
{
    status_$t status = 0x1111;
    uint32_t r;

    r = AREA_$COPY(0, 0, 4, 0, 0, &status);

    ASSERT_EQ(status_$area_not_active, status);
    ASSERT_EQ(0, r);                /* the A6-0x2C slot, modelled as 0 */
    ASSERT_EQ(0, create_calls);
}

/* 0x00E0905A-0x00E0906A: inactive or wrong generation */
TEST(wrong_generation_returns_not_active)
{
    status_$t status = 0;
    uint32_t r;

    make_one_segment_source();

    r = AREA_$COPY(0x56, SRC_ID, 4, 0, 0, &status);

    ASSERT_EQ(status_$area_not_active, status);
    ASSERT_EQ(0, r);
    ASSERT_EQ(0, create_calls);
}

/* 0x00E09076-0x00E09090 */
TEST(not_owner_returns_not_owner)
{
    status_$t status = 0;
    uint32_t r;

    make_one_segment_source();
    src_entry()->remote_uid = 0;
    src_entry()->owner_asid = 3;
    PROC1_$AS_ID = 4;

    r = AREA_$COPY(0x55, SRC_ID, 4, 0, 0, &status);

    ASSERT_EQ(status_$area_not_owner, status);
    ASSERT_EQ(0, r);
    ASSERT_EQ(0, create_calls);
}

/* 0x00E090C0-0x00E090C2: create failed, nothing else runs */
TEST(create_failure_propagates_and_skips_the_tail)
{
    status_$t status = 0;
    uint32_t r;

    make_one_segment_source();
    create_status = 0x00320001;

    r = AREA_$COPY(0x55, SRC_ID, 4, 0, 0, &status);

    ASSERT_EQ(0x00320001, status);
    ASSERT_EQ(0, r);
    ASSERT_EQ(1, create_calls);
    ASSERT_EQ(0, ec_advance_calls);         /* the tail is not reached */
}

/*
 * 0x00E090FA: virt_size == 0 branches to the COMMON TAIL, not to the
 * epilogue - the in-transition bit is cleared and the eventcount advanced
 * even though neither was ever set here.
 */
TEST(virt_size_zero_still_clears_in_trans_and_advances)
{
    status_$t status = 0;
    uint32_t r;
    area_$entry_t *e = src_entry();

    e->flags = AREA_FLAG_ACTIVE | AREA_FLAG_IN_TRANS;
    e->generation = 0x55;
    e->virt_size = 0;

    r = AREA_$COPY(0x55, SRC_ID, 4, 0, 0, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(create_result, r);
    ASSERT_EQ(0, (e->flags & AREA_FLAG_IN_TRANS));
    ASSERT_EQ(1, ec_advance_calls);
    ASSERT_EQ((uintptr_t)&AREA_$IN_TRANS_EC, (uintptr_t)ec_advance_last);
    ASSERT_EQ(0, copy_area_calls);
}

/* 0x00E09122-0x00E09160: the in-transition handshake and the dst fields */
TEST(source_marked_in_trans_and_dst_initialised)
{
    status_$t status = 0;

    make_one_segment_source();
    src_entry()->remote_uid = 0xAABBCCDD;

    AREA_$COPY(0x55, SRC_ID, 7, 0, 0, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0xAABBCCDD, dst_entry()->remote_uid);
    ASSERT_EQ(7, dst_entry()->first_bste);
    ASSERT_EQ(9, dst_entry()->first_seg_index);
    /* Cleared again by the tail. */
    ASSERT_EQ(0, (src_entry()->flags & AREA_FLAG_IN_TRANS));
    ASSERT_EQ(1, ec_advance_calls);
}

/*
 * 0x00E092A4 / 0x00E092D0 / 0x00E0930C: all three calls get `pea (A4)`,
 * the CALLER's status cell.
 */
TEST(callees_receive_the_caller_status_cell)
{
    status_$t status = 0;

    make_one_segment_source();

    AREA_$COPY(0x55, SRC_ID, 7, 0, 0, &status);

    ASSERT_EQ(2, get_aste_calls);
    ASSERT_EQ((uintptr_t)&status, (uintptr_t)get_aste_status_ptr[0]);
    ASSERT_EQ((uintptr_t)&status, (uintptr_t)get_aste_status_ptr[1]);
    ASSERT_EQ(1, copy_area_calls);
    ASSERT_EQ((uintptr_t)&status, (uintptr_t)copy_area_status_ptr);
}

/* The AST_$COPY_AREA failure must survive into the caller's cell. */
TEST(copy_area_failure_reaches_the_caller)
{
    status_$t status = 0;
    uint32_t r;

    make_one_segment_source();
    copy_area_status = 0x00030005;

    r = AREA_$COPY(0x55, SRC_ID, 7, 0, 0, &status);

    ASSERT_EQ(0x00030005, status);
    ASSERT_EQ(create_result, r);
    ASSERT_EQ(1, delete_calls);
    ASSERT_EQ(1, ec_advance_calls);
}

/*
 * 0x00E09344-0x00E0934C: area_$internal_delete(dst_entry,
 * dst_entry->reserved_2a, &local_status, TRUE) - the second argument is the
 * DESTINATION entry's +0x2A word, and the status is a scratch local so the
 * caller keeps the AST_$COPY_AREA failure.
 */
TEST(internal_delete_uses_dst_reserved_2a_and_a_scratch_status)
{
    status_$t status = 0;

    make_one_segment_source();
    copy_area_status = 0x00030005;
    /* area_$internal_create fills this in for real; forge it here. */
    dst_entry()->reserved_2a = 0x2BAD;

    AREA_$COPY(0x55, SRC_ID, 7, 0, 0, &status);

    ASSERT_EQ(1, delete_calls);
    ASSERT_EQ((uintptr_t)dst_entry(), (uintptr_t)delete_entry);
    ASSERT_EQ((int16_t)0x2BAD, delete_area_id);
    ASSERT_EQ((int8_t)-1, delete_unlink);
    ASSERT_TRUE(delete_status_ptr != &status);
    ASSERT_EQ(0x00030005, status);
}

/* 0x00E0930E-0x00E09328: the sixth argument is seg_page << 15, by value */
TEST(copy_area_arguments)
{
    status_$t status = 0;

    make_one_segment_source();

    AREA_$COPY(0x55, SRC_ID, 7, 0x33, 0, &status);

    ASSERT_EQ(1, copy_area_calls);
    ASSERT_EQ(SRC_ID, copy_area_area_id);
    ASSERT_EQ(0x33, copy_area_unused);
    ASSERT_EQ(0, copy_area_start_seg);
    ASSERT_EQ((uintptr_t)((uint32_t)9 << 15), (uintptr_t)copy_area_buffer);
    ASSERT_EQ((uintptr_t)&mock_astes[0], (uintptr_t)copy_area_src);
    ASSERT_EQ((uintptr_t)&mock_astes[1], (uintptr_t)copy_area_dst);
    /* 0x00E09332/0x00E09336: both ASTEs lose a reference */
    ASSERT_EQ((uint8_t)0xFF, mock_astes[0].wire_count);
    ASSERT_EQ((uint8_t)0xFF, mock_astes[1].wire_count);
}

/* 0x00E092A4-0x00E092E2: the two slot pointers and the constant flags */
TEST(get_aste_arguments)
{
    status_$t status = 0;

    make_one_segment_source();

    AREA_$COPY(0x55, SRC_ID, 7, 0, 0, &status);

    ASSERT_EQ(SRC_ID, get_aste_area_id[0]);
    ASSERT_EQ((uintptr_t)slots_of(src_entry()), (uintptr_t)get_aste_slot[0]);
    ASSERT_EQ(DST_ID, get_aste_area_id[1]);
    ASSERT_EQ((uintptr_t)slots_of(dst_entry()), (uintptr_t)get_aste_slot[1]);
    ASSERT_EQ(0, get_aste_wait[0]);
    ASSERT_EQ((int8_t)-1, get_aste_create[0]);
}

/*
 * 0x00E0928C-0x00E09292: a segment in [stack_low_page, stack_high_page)
 * is skipped without any AST work.
 */
TEST(stack_segments_are_skipped)
{
    status_$t status = 0;

    make_one_segment_source();
    src_entry()->first_seg_index = 0;
    AS_$STACK_LOW = 0;

    /* stack_limit >> 15 == 1, so segment page 0 is inside the stack range */
    AREA_$COPY(0x55, SRC_ID, 7, 0, 0x8000, &status);

    ASSERT_EQ(0, get_aste_calls);
    ASSERT_EQ(0, copy_area_calls);
    ASSERT_EQ(1, ec_advance_calls);
}

/*
 * 0x00E0916E-0x00E0917E and 0x00E0935C: AREA_FLAG_REVERSED walks the
 * segment pages downwards.
 */
TEST(reversed_area_walks_backwards)
{
    status_$t status = 0;

    make_one_segment_source();
    src_entry()->flags |= AREA_FLAG_REVERSED;
    src_entry()->first_seg_index = 9;
    /* Allocate bit 1, i.e. the SECOND segment of the walk. */
    slots_of(src_entry())->bits = 0x02;

    AREA_$COPY(0x55, SRC_ID, 7, 0, 0, &status);

    ASSERT_EQ(1, copy_area_calls);
    ASSERT_EQ(1, copy_area_start_seg);
    /* seg_page went 9 -> 8 */
    ASSERT_EQ((uintptr_t)((uint32_t)8 << 15), (uintptr_t)copy_area_buffer);
    /* `sne D5b` -> a Domain true, forwarded to area_$internal_create */
    ASSERT_EQ((int8_t)-1, create_shared);
}

/*
 * 0x00E091F8: a missing overflow table crashes the system.  Reaching it
 * needs bitmap_bytes >= 3, i.e. a virt_size of at least 17 * 32K.
 */
TEST(missing_seg_table_crashes)
{
    status_$t status = 0;
    area_$entry_t *e = src_entry();

    e->flags = AREA_FLAG_ACTIVE;
    e->generation = 0x55;
    e->virt_size = 0x8000 * 24;     /* 24 segments -> 3 bitmap bytes */
    e->owner_asid = 3;
    e->first_seg_index = 0;
    /* Nothing on AREA_$GLOBALS.seg_table_list[3]. */

    if (setjmp(crash_jmp) == 0) {
        AREA_$COPY(0x55, SRC_ID, 7, 0, 0, &status);
        ASSERT_TRUE(0 && "CRASH_SYSTEM was not reached");
    }

    ASSERT_EQ(1, crash_calls);
    ASSERT_EQ(Area_Internal_Error, crash_status);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int main(void)
{
    printf("AREA_$COPY tests:\n");

    RUN_TEST(bad_area_id_returns_not_active);
    RUN_TEST(wrong_generation_returns_not_active);
    RUN_TEST(not_owner_returns_not_owner);
    RUN_TEST(create_failure_propagates_and_skips_the_tail);
    RUN_TEST(virt_size_zero_still_clears_in_trans_and_advances);
    RUN_TEST(source_marked_in_trans_and_dst_initialised);
    RUN_TEST(callees_receive_the_caller_status_cell);
    RUN_TEST(copy_area_failure_reaches_the_caller);
    RUN_TEST(internal_delete_uses_dst_reserved_2a_and_a_scratch_status);
    RUN_TEST(copy_area_arguments);
    RUN_TEST(get_aste_arguments);
    RUN_TEST(stack_segments_are_skipped);
    RUN_TEST(reversed_area_walks_backwards);
    RUN_TEST(missing_seg_table_crashes);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
