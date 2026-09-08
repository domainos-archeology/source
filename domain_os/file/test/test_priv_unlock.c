/*
 * file/test/test_priv_unlock.c - unit tests for FILE_$PRIV_UNLOCK (0x00E5FD32)
 *
 * The real file/priv_unlock.c is #included at the bottom together with the
 * real file/file_data.c, so the lock tables and FILE_$LOCK_MAP_TABLE carry the
 * values read out of the binary image.  Everything the function calls out to
 * is mocked here.
 *
 * The behaviours exercised are the ones source-fi9u is about: the ten-argument
 * frame at A6+0x08..A6+0x27, in particular that
 *   - the lock slot at A6+0x0C is a full longword whose low word is used
 *     (0x00E5FDA2 `move.w (0xe,A6),D4w`),
 *   - lock_mode (A6+0x10) and asid (A6+0x12) are two separate words,
 *   - by_key (A6+0x14) is a byte-sized Pascal boolean and key (A6+0x16) is a
 *     word of its own - the previous prototype merged them into one longword,
 *   - rem_key (A6+0x18) is matched against entry->context and rem_node
 *     (A6+0x1C) against entry->node_low (0x00E5FF38 / 0x00E5FF2E).
 */

#include <stdio.h>
#include <string.h>

#include "file/file_internal.h"
#include "ml/ml.h"
#include "netlog/netlog.h"

/* ============================================================================
 * Test framework
 * ============================================================================ */

static int tests_failed = 0;
static int tests_run = 0;
static int current_failed = 0;

#define TEST(name)      static void test_##name(void)
#define RUN_TEST(name)  do {                                                  \
        printf("  %-46s ", #name);                                            \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        if (current_failed == 0) { printf("PASSED\n"); }                      \
    } while (0)

#define ASSERT_EQ(expected, actual) do {                                      \
        unsigned long _e = (unsigned long)(expected);                         \
        unsigned long _a = (unsigned long)(actual);                           \
        if (_e != _a) {                                                       \
            if (current_failed == 0) { printf("FAILED\n"); }                  \
            printf("      line %d: expected 0x%lx, got 0x%lx\n",              \
                   __LINE__, _e, _a);                                         \
            current_failed = 1; tests_failed++;                               \
            return;                                                           \
        }                                                                     \
    } while (0)

/* ============================================================================
 * Mocked globals
 * ============================================================================ */

uint32_t NODE_$ME = 0x00012345;
int8_t   NETLOG_$OK_TO_LOG = 0;          /* >= 0: logging disabled */
int8_t   NETLOG_$OK_TO_LOG_SERVER = 0;
uint16_t PROC1_$AS_ID = 3;
uint16_t PROC1_$CURRENT = 1;
int8_t   AUDIT_$ENABLED = 0;

/* ============================================================================
 * Mock bookkeeping
 * ============================================================================ */

static int      mock_ml_lock_depth;
static int      mock_ml_lock_calls;
static int      mock_ml_unlock_calls;
static int16_t  mock_hash = 7;

static int      mock_set_dts_calls;
static uint8_t  mock_set_dts_result;

static int      mock_set_attr_calls;
static uint16_t mock_set_attr_id;
static uint16_t mock_set_attr_word;
static uint32_t mock_set_attr_long;

static int      mock_purify_calls;
static uint16_t mock_purify_flags;
static int16_t  mock_purify_segment;
static uint32_t *mock_purify_list;
static uint16_t mock_purify_unused;

static int      mock_get_dtv_calls;
static uint32_t mock_get_dtv_value;
static status_$t mock_get_dtv_status;

static int      mock_truncate_calls;
static uint8_t  mock_truncate_result;
static int      mock_cond_flush_calls;

static int      mock_cattr_calls;
static uint16_t mock_cattr_flags;
static status_$t mock_cattr_status;
static uint8_t  mock_cattr_byte0;       /* +0x00: "not empty" */
static uint16_t mock_cattr_refcount;         /* +0x14 */

static int      mock_local_read_lock_calls;
static status_$t mock_local_read_lock_status;

static int       mock_rem_unlock_calls;
static uint16_t  mock_rem_unlock_mode;
static uint32_t  mock_rem_unlock_key;
static uint16_t  mock_rem_unlock_seq;
static uint32_t  mock_rem_unlock_node;
static boolean   mock_rem_unlock_release;
static uid_t     mock_rem_unlock_uid;
static uint32_t  mock_rem_unlock_loc_info;
static uint32_t  mock_rem_unlock_desc_node;
static status_$t mock_rem_unlock_status;
static uint8_t   mock_rem_unlock_result;

/* ============================================================================
 * Mocks
 * ============================================================================ */

void ML_$LOCK(int16_t id)   { (void)id; mock_ml_lock_depth++;  mock_ml_lock_calls++; }
void ML_$UNLOCK(int16_t id) { (void)id; mock_ml_lock_depth--;  mock_ml_unlock_calls++; }

uint32_t UID_$HASH(uid_t *uid, uint16_t *table_size)
{
    (void)uid;
    /* The real routine divides by the word the caller passes by reference. */
    if (table_size != NULL && *table_size == 0) {
        return 0;
    }
    return (uint32_t)(uint16_t)mock_hash;
}

uint8_t AST_$SET_DTS(uint16_t flags, uid_t *uid, uint32_t *dtv,
                     uint32_t *access_time, status_$t *status)
{
    (void)flags; (void)uid;
    mock_set_dts_calls++;
    /* Both pointers are the same scratch cell (`move.l (SP),-(SP)`). */
    if (dtv != access_time) {
        printf("\n      AST_$SET_DTS got two different scratch pointers\n");
        current_failed = 1; tests_failed++;
    }
    *status = 0;
    return mock_set_dts_result;
}

void AST_$SET_ATTRIBUTE(uid_t *uid, uint16_t attr_id, void *value,
                        status_$t *status)
{
    (void)uid;
    mock_set_attr_calls++;
    mock_set_attr_id   = attr_id;
    mock_set_attr_word = *(uint16_t *)value;
    mock_set_attr_long = *(uint32_t *)value;
    *status = 0;
}

uint16_t AST_$PURIFY(uid_t *uid, uint16_t flags, int16_t segment,
                     uint32_t *segment_list, uint16_t unused, status_$t *status)
{
    (void)uid;
    mock_purify_calls++;
    mock_purify_flags   = flags;
    mock_purify_segment = segment;
    mock_purify_list    = segment_list;
    mock_purify_unused  = unused;
    *status = 0;
    return 0;
}

void AST_$GET_DTV(uid_t *uid, uint32_t unused, uint32_t *dtv, status_$t *status)
{
    (void)uid; (void)unused;
    mock_get_dtv_calls++;
    *dtv = mock_get_dtv_value;
    *status = mock_get_dtv_status;
}

void AST_$TRUNCATE(uid_t *uid, uint32_t new_size, uint16_t flags,
                   uint8_t *result, status_$t *status)
{
    (void)uid; (void)new_size; (void)flags;
    mock_truncate_calls++;
    *result = mock_truncate_result;
    *status = 0;
}

void AST_$COND_FLUSH(uid_t *uid, uint32_t *timestamp, status_$t *status)
{
    (void)uid; (void)timestamp;
    mock_cond_flush_calls++;
    *status = 0;
}

void AST_$GET_COMMON_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags,
                                ast_$common_attr_t *attrs, status_$t *status)
{
    (void)loc_rec;
    mock_cattr_calls++;
    mock_cattr_flags = flags;
    memset(attrs, 0, sizeof(*attrs));
    attrs->obj_type = mock_cattr_byte0;
    attrs->refcount = mock_cattr_refcount;
    *status = mock_cattr_status;
}

void REM_FILE_$LOCAL_READ_LOCK(void *addr_info, uid_t *file_uid,
                               void *lock_entry_out, status_$t *status)
{
    (void)addr_info; (void)file_uid; (void)lock_entry_out;
    mock_local_read_lock_calls++;
    *status = mock_local_read_lock_status;
}

uint8_t REM_FILE_$UNLOCK(file_$obj_loc_t *location_block, uint16_t unlock_mode,
                         uint32_t rem_key, uint16_t lock_key,
                         uint32_t rem_node, boolean release_flag,
                         status_$t *status)
{
    file_$obj_loc_t *desc = location_block;

    mock_rem_unlock_calls++;
    mock_rem_unlock_mode      = unlock_mode;
    mock_rem_unlock_key       = rem_key;
    mock_rem_unlock_seq       = lock_key;
    mock_rem_unlock_node      = rem_node;
    mock_rem_unlock_release   = release_flag;
    mock_rem_unlock_uid       = desc->uid;
    mock_rem_unlock_loc_info  = desc->loc_info;
    mock_rem_unlock_desc_node = desc->node;
    *status = mock_rem_unlock_status;
    return mock_rem_unlock_result;
}

void NETLOG_$LOG_IT(uint16_t kind, uint32_t *uid,
                    uint16_t p3, uint16_t p4, uint16_t p5, uint16_t p6,
                    uint16_t p7, uint16_t p8)
{
    (void)kind; (void)uid; (void)p3; (void)p4; (void)p5; (void)p6;
    (void)p7; (void)p8;
}

/* ============================================================================
 * The code under test (and the real tables it reads)
 * ============================================================================ */

#include "../file_data.c"
#include "../priv_unlock.c"

/* ============================================================================
 * Fixtures
 * ============================================================================ */

#define TEST_ASID       3
#define TEST_HASH       7
#define ENTRY_A         50
#define ENTRY_B         51
#define FREE_HEAD       100

static const uid_t TEST_UID  = { 0x11223344u, 0x55667788u };
static const uid_t OTHER_UID = { 0x99AABBCCu, 0xDDEEFF00u };

static void reset(void)
{
    memset(FILE_$LOCK_ENTRIES, 0, sizeof(FILE_$LOCK_ENTRIES));
    memset(FILE_$LOCK_TABLE, 0, sizeof(FILE_$LOCK_TABLE));
    memset(FILE_$LOCK_TABLE2, 0, sizeof(FILE_$LOCK_TABLE2));
    memset(FILE_$LOT_HASHTAB, 0, sizeof(FILE_$LOT_HASHTAB));
    FILE_$LOT_FREE = FREE_HEAD;

    mock_ml_lock_depth = 0;
    mock_ml_lock_calls = 0;
    mock_ml_unlock_calls = 0;
    mock_hash = TEST_HASH;

    mock_set_dts_calls = 0;
    mock_set_dts_result = 0;

    mock_set_attr_calls = 0;
    mock_set_attr_id = 0xFFFF;
    mock_set_attr_word = 0xFFFF;
    mock_set_attr_long = 0xFFFFFFFFu;

    mock_purify_calls = 0;
    mock_purify_flags = 0;
    mock_purify_segment = -1;
    mock_purify_list = NULL;
    mock_purify_unused = 0xFFFF;

    mock_get_dtv_calls = 0;
    mock_get_dtv_value = 0xCAFEBABEu;
    mock_get_dtv_status = 0;

    mock_truncate_calls = 0;
    mock_truncate_result = 0;
    mock_cond_flush_calls = 0;

    mock_cattr_calls = 0;
    mock_cattr_flags = 0;
    mock_cattr_status = 0;
    mock_cattr_byte0 = 0;
    mock_cattr_refcount = 1;             /* != 0: skip REM_FILE_$LOCAL_READ_LOCK */

    mock_local_read_lock_calls = 0;
    mock_local_read_lock_status = 0;

    mock_rem_unlock_calls = 0;
    mock_rem_unlock_mode = 0xFFFF;
    mock_rem_unlock_key = 0xFFFFFFFFu;
    mock_rem_unlock_seq = 0xFFFF;
    mock_rem_unlock_node = 0xFFFFFFFFu;
    mock_rem_unlock_release = 0;
    mock_rem_unlock_status = 0;
    mock_rem_unlock_result = 0;
}

/* Build a lock-table entry and hang it off the hash bucket. */
static file_lock_entry_detail_t *make_entry(int16_t idx, const uid_t *uid,
                                            uint8_t mode, uint8_t flags2_extra,
                                            uint8_t refcount)
{
    file_lock_entry_detail_t *e = FILE_$LOT_ENTRY(idx);

    e->context   = 0xAABBCCDDu;
    e->node_low  = 0x00011111u;
    e->node_high = 0x00022222u;
    e->uid_high  = uid->high;
    e->uid_low   = uid->low;
    e->sequence  = 0x1357;
    e->refcount  = refcount;
    e->flags1    = 0;
    e->rights    = 0;
    e->flags2    = (uint8_t)((mode << FILE_LOCK_F2_MODE_SHIFT) | flags2_extra);

    e->next = FILE_$LOT_HASHTAB[TEST_HASH];
    FILE_$LOT_HASHTAB[TEST_HASH] = (uint16_t)idx;
    return e;
}

/* ============================================================================
 * Early exits (0x00E5FD6C-0x00E5FD82)
 * ============================================================================ */

/* `cmpi.w #0x8,D3w; seq D1b; move.b (0x14,A6),D2b; not.b D2b; and.b; bmi`. */
TEST(mode_8_without_by_key_never_touches_the_tables)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0xDEAD;
    boolean r;

    reset();
    r = FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 8, TEST_ASID,
                          0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, r);
    ASSERT_EQ(0, mock_ml_lock_calls);
    ASSERT_EQ(0, dtv);                  /* cleared at 0x00E5FD50 */
}

/* The same test with by_key TRUE must fall through into the body. */
TEST(mode_8_with_by_key_enters_the_body)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;

    reset();
    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 8, TEST_ASID,
                            -1, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(1, mock_ml_lock_calls);
    ASSERT_EQ(1, mock_ml_unlock_calls);
    /* Empty chain and mode 8 -> "not locked by this process". */
    ASSERT_EQ(file_$object_not_locked_by_this_process, st);
}

TEST(mode_9_never_touches_the_tables)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;

    reset();
    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 9, TEST_ASID,
                            -1, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, mock_ml_lock_calls);
}

/* ============================================================================
 * The explicit-slot path (0x00E5FE4A)
 * ============================================================================ */

/* `cmpi.w #0x96,D4w; bls` - anything above 150 is invalid. */
TEST(slot_above_0x96_is_invalid_arg)
{
    status_$t st = 0;
    uint32_t dtv = 0;

    reset();
    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0x97, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(file_$invalid_arg, st);
    ASSERT_EQ(1, mock_ml_lock_calls);
    ASSERT_EQ(1, mock_ml_unlock_calls);
    ASSERT_EQ(0, mock_ml_lock_depth);
}

/*
 * The slot argument is a full longword whose low word is what the body uses
 * (0x00E5FDA2 `move.w (0xe,A6),D4w`).  0x00010005 therefore names slot 5, and
 * 0x00010097 is still rejected as > 0x96.
 */
TEST(only_the_low_word_of_the_slot_argument_is_used)
{
    status_$t st = 0;
    uint32_t dtv = 0;

    reset();
    FILE_$LOCK_TABLE2[TEST_ASID] = 8;
    make_entry(ENTRY_A, &TEST_UID, 4, 0, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0x00010005, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, FILE_$PROC_LOT_SLOT(TEST_ASID, 5));   /* 0x00E5FEBE */

    reset();
    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0x00010097, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(file_$invalid_arg, st);
}

TEST(explicit_slot_with_wrong_uid_is_not_locked)
{
    status_$t st = 0;
    uint32_t dtv = 0;

    reset();
    make_entry(ENTRY_A, &OTHER_UID, 4, 0, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(file_$object_not_locked_by_this_process, st);
    ASSERT_EQ(ENTRY_A, FILE_$PROC_LOT_SLOT(TEST_ASID, 5));  /* not cleared */
}

/* 0x00E5FEAA `btst.b #0x0,(-0x1,A0); beq` - bit 0 must be CLEAR here. */
TEST(explicit_slot_rejects_flags2_bit0)
{
    status_$t st = 0;
    uint32_t dtv = 0;

    reset();
    make_entry(ENTRY_A, &TEST_UID, 4, FILE_LOCK_F2_FLAG0, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(file_$object_not_locked_by_this_process, st);
}

/* With lock_mode 0 the mode and the bit-0 test are both skipped. */
TEST(explicit_slot_with_mode_zero_skips_the_mode_tests)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;

    reset();
    make_entry(ENTRY_A, &TEST_UID, 4, FILE_LOCK_F2_FLAG0, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;
    FILE_$LOCK_TABLE2[TEST_ASID] = 0;   /* the mode-0 retry finds nothing */

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 0, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0, FILE_$PROC_LOT_SLOT(TEST_ASID, 5));
    /* The retry ends on "not locked", which did_unlock turns back into 0. */
    ASSERT_EQ(0, st);
}

/* ============================================================================
 * Reference counting and unlinking
 * ============================================================================ */

TEST(a_shared_entry_is_only_decremented)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;
    file_lock_entry_detail_t *e;

    reset();
    e = make_entry(ENTRY_A, &TEST_UID, 4, 0, 2);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, e->refcount);                          /* 0x00E6009E */
    ASSERT_EQ(ENTRY_A, FILE_$LOT_HASHTAB[TEST_HASH]);   /* still linked */
    ASSERT_EQ(FREE_HEAD, FILE_$LOT_FREE);
    ASSERT_EQ(0, mock_truncate_calls);
    ASSERT_EQ(1, mock_ml_unlock_calls);
    ASSERT_EQ(0, mock_ml_lock_depth);
}

TEST(the_last_reference_unlinks_and_frees_the_entry)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;
    file_lock_entry_detail_t *e;

    reset();
    e = make_entry(ENTRY_A, &TEST_UID, 4, 0, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, e->refcount);
    ASSERT_EQ(0, FILE_$LOT_HASHTAB[TEST_HASH]);         /* 0x00E6010E */
    ASSERT_EQ(ENTRY_A, FILE_$LOT_FREE);                 /* 0x00E60138 */
    ASSERT_EQ(FREE_HEAD, e->next);                      /* 0x00E60132 */
    /* Local entry, no other lock, real UID -> AST_$TRUNCATE (0x00E602F0). */
    ASSERT_EQ(1, mock_truncate_calls);
    ASSERT_EQ(0, mock_ml_lock_depth);
}

/* A second lock on the same UID keeps the object from being truncated
 * (0x00E60140 sets saw_other, 0x00E602E2 `bmi` skips the truncate). */
TEST(another_lock_on_the_same_uid_suppresses_the_truncate)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;

    reset();
    make_entry(ENTRY_B, &TEST_UID, 2, 0, 1);        /* survives */
    make_entry(ENTRY_A, &TEST_UID, 4, 0, 1);        /* released */
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(ENTRY_B, FILE_$LOT_HASHTAB[TEST_HASH]);
    ASSERT_EQ(ENTRY_A, FILE_$LOT_FREE);
    ASSERT_EQ(0, mock_truncate_calls);
    /* Mode 4 was exclusive and mode 2 is not, so the purify still runs. */
    ASSERT_EQ(1, mock_purify_calls);
}

/* Another *exclusive* lock (mode 4 or 11) suppresses the purify as well
 * (0x00E60158 sets other_exclusive, 0x00E60168 `not.b`/`and.b`/`bpl`). */
TEST(another_exclusive_lock_suppresses_the_purify)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;

    reset();
    make_entry(ENTRY_B, &TEST_UID, 0x0B, 0, 1);
    make_entry(ENTRY_A, &TEST_UID, 4, 0, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0, mock_purify_calls);
}

/* 0x00E6017A: the constant cell at 0x00E5E61E is a longword 0 passed by
 * reference, and the longword 0x80000000 at A6+0x0C reaches the callee as
 * flags = 0x8000 with segment = 0. */
TEST(purify_gets_the_pc_relative_nil_list_and_0x8000)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;

    reset();
    make_entry(ENTRY_A, &TEST_UID, 4, 0, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(1, mock_purify_calls);
    ASSERT_EQ(0x8000, mock_purify_flags);
    ASSERT_EQ(0, mock_purify_segment);
    ASSERT_EQ(0, mock_purify_unused);
    ASSERT_EQ(1, mock_purify_list == &file_$nil_cell);
    ASSERT_EQ(0, file_$nil_cell);
}

/* ============================================================================
 * The search-the-process-row path (0x00E5FDC0, slot == 0)
 * ============================================================================ */

TEST(slot_zero_scans_the_process_row)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;

    reset();
    FILE_$LOCK_TABLE2[TEST_ASID] = 4;               /* four slots in use */
    /* With a non-zero lock_mode the row scan needs flags2 bit 0 set - see
     * row_scan_requires_flags2_bit0_when_a_mode_is_given below. */
    make_entry(ENTRY_A, &TEST_UID, 4, FILE_LOCK_F2_FLAG0, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 3) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, FILE_$PROC_LOT_SLOT(TEST_ASID, 3));
    ASSERT_EQ(ENTRY_A, FILE_$LOT_FREE);
}

/*
 * 0x00E5FE08 `btst.b #0,(-0x1,A0); sne; tst.w D3w; seq; or.b; bpl`: with a
 * non-zero lock_mode the row scan only considers entries whose flags2 bit 0 is
 * SET - the opposite of the explicit-slot path.
 */
TEST(row_scan_requires_flags2_bit0_when_a_mode_is_given)
{
    status_$t st = 0;
    uint32_t dtv = 0;

    reset();
    FILE_$LOCK_TABLE2[TEST_ASID] = 4;
    make_entry(ENTRY_A, &TEST_UID, 4, 0, 1);        /* bit 0 clear */
    FILE_$PROC_LOT_SLOT(TEST_ASID, 3) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(file_$object_not_locked_by_this_process, st);
    ASSERT_EQ(ENTRY_A, FILE_$PROC_LOT_SLOT(TEST_ASID, 3));

    reset();
    FILE_$LOCK_TABLE2[TEST_ASID] = 4;
    make_entry(ENTRY_A, &TEST_UID, 4, FILE_LOCK_F2_FLAG0, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 3) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, FILE_$PROC_LOT_SLOT(TEST_ASID, 3));
}

/* Mode 0 repeats the whole search until it comes up empty (0x00E60322). */
TEST(mode_zero_releases_every_matching_lock)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;

    reset();
    FILE_$LOCK_TABLE2[TEST_ASID] = 4;
    make_entry(ENTRY_B, &TEST_UID, 2, 0, 1);
    make_entry(ENTRY_A, &TEST_UID, 4, 0, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 1) = ENTRY_A;
    FILE_$PROC_LOT_SLOT(TEST_ASID, 2) = ENTRY_B;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 0, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0, st);                                /* 0xF0005 -> 0 */
    ASSERT_EQ(0, FILE_$PROC_LOT_SLOT(TEST_ASID, 1));
    ASSERT_EQ(0, FILE_$PROC_LOT_SLOT(TEST_ASID, 2));
    ASSERT_EQ(0, FILE_$LOT_HASHTAB[TEST_HASH]);
    ASSERT_EQ(3, mock_ml_lock_calls);                /* two hits + one miss */
    ASSERT_EQ(3, mock_ml_unlock_calls);
    ASSERT_EQ(0, mock_ml_lock_depth);
}

/* ============================================================================
 * The by_key path (0x00E5FED2) - the arguments source-fi9u was dropping
 * ============================================================================ */

TEST(by_key_matches_key_rem_key_and_rem_node)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;
    file_lock_entry_detail_t *e;

    reset();
    e = make_entry(ENTRY_A, &TEST_UID, 4, 0, 1);

    /* Right key, wrong rem_key (0x00E5FF38). */
    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 4, 0,
                            -1, e->sequence, e->context + 1, e->node_low,
                            &dtv, &st);
    ASSERT_EQ(0, st);                       /* not mode 8: silent miss */
    ASSERT_EQ(1, e->refcount);

    /* Right rem_key, wrong rem_node (0x00E5FF2E). */
    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 4, 0,
                            -1, e->sequence, e->context, e->node_low + 1,
                            &dtv, &st);
    ASSERT_EQ(1, e->refcount);

    /* Wrong key (0x00E5FF1A). */
    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 4, 0,
                            -1, (uint16_t)(e->sequence + 1), e->context,
                            e->node_low, &dtv, &st);
    ASSERT_EQ(1, e->refcount);

    /* Everything matches. */
    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 4, 0,
                            -1, e->sequence, e->context, e->node_low,
                            &dtv, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, e->refcount);
    ASSERT_EQ(ENTRY_A, FILE_$LOT_FREE);
}

/* `tst.w (0x16,A6); seq D2b; or.b` - key 0 matches any sequence. */
TEST(by_key_with_key_zero_matches_any_sequence)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;
    file_lock_entry_detail_t *e;

    reset();
    e = make_entry(ENTRY_A, &TEST_UID, 4, 0, 1);

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 4, 0,
                            -1, 0, e->context, e->node_low, &dtv, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, e->refcount);
}

/* 0x00E5FF3E `btst.b #0x2,(-0x1,A0); bne` - entries flagged remote are
 * skipped by this search. */
TEST(by_key_skips_entries_flagged_remote)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;
    file_lock_entry_detail_t *e;

    reset();
    e = make_entry(ENTRY_A, &TEST_UID, 4, FILE_LOCK_F2_REMOTE, 1);

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 4, 0,
                            -1, e->sequence, e->context, e->node_low,
                            &dtv, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, e->refcount);
}

/* 0x00E5FF56: an entry whose refcount is already zero is not a match. */
TEST(by_key_skips_entries_with_a_zero_refcount)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;
    file_lock_entry_detail_t *e;

    reset();
    e = make_entry(ENTRY_A, &TEST_UID, 4, 0, 0);

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 4, 0,
                            -1, e->sequence, e->context, e->node_low,
                            &dtv, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, mock_set_dts_calls);
}

/* 0x00E5FF6E: only mode 8 turns "nothing found" into an error. */
TEST(by_key_miss_is_an_error_only_for_mode_8)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;

    reset();
    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 4, 0,
                            -1, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0, st);

    reset();
    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 8, 0,
                            -1, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(file_$object_not_locked_by_this_process, st);
}

/* 0x00E5FF84-0x00E5FFDC: mode 8 marks the object delete-pending through
 * attribute 7 and never touches the refcount. */
TEST(by_key_mode_8_sets_the_delete_pending_attribute)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;
    file_lock_entry_detail_t *e;

    reset();
    mock_cattr_byte0 = 0;               /* "empty" -> the attribute is set */
    e = make_entry(ENTRY_A, &TEST_UID, 8, 0, 1);

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 8, 0,
                            -1, e->sequence, e->context, e->node_low,
                            &dtv, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, e->refcount);                  /* untouched */
    ASSERT_EQ(1, mock_cattr_calls);
    ASSERT_EQ(0x10, mock_cattr_flags);          /* `move.w #0x10,-(SP)` */
    ASSERT_EQ(1, mock_set_attr_calls);
    ASSERT_EQ(7, mock_set_attr_id);
    ASSERT_EQ(1, mock_set_attr_word);           /* `move.w #0x1,(-0x78,A6)` */
    ASSERT_EQ(0, mock_ml_lock_depth);

    /* A non-empty object skips the attribute write (0x00E5FFB8). */
    reset();
    mock_cattr_byte0 = 1;
    e = make_entry(ENTRY_A, &TEST_UID, 8, 0, 1);
    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 8, 0,
                            -1, e->sequence, e->context, e->node_low,
                            &dtv, &st);
    ASSERT_EQ(0, mock_set_attr_calls);
}

/* ============================================================================
 * The remote tail (0x00E60210)
 * ============================================================================ */

TEST(remote_entry_forwards_context_and_sequence_to_rem_file_unlock)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;
    file_lock_entry_detail_t *e;

    reset();
    mock_set_dts_result = 0xFF;
    mock_rem_unlock_result = 0x01;
    /* flags2 bit 2 = the object is remote; bit 1 clear = not pending. */
    e = make_entry(ENTRY_A, &TEST_UID, 4, FILE_LOCK_F2_REMOTE, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(1, mock_rem_unlock_calls);
    ASSERT_EQ(0xAABBCCDDu, mock_rem_unlock_key);    /* entry->context  */
    ASSERT_EQ(0x1357, mock_rem_unlock_seq);         /* entry->sequence */
    ASSERT_EQ(NODE_$ME, mock_rem_unlock_node);
    ASSERT_EQ(0xFF, (uint8_t)mock_rem_unlock_release);
    ASSERT_EQ(TEST_UID.high, mock_rem_unlock_uid.high);
    ASSERT_EQ(TEST_UID.low, mock_rem_unlock_uid.low);
    /* desc.loc_info comes from entry+0x08 and desc.node from entry+0x04
     * (0x00E6003A / 0x00E60040). */
    ASSERT_EQ(0x00022222u, mock_rem_unlock_loc_info);
    ASSERT_EQ(0x00011111u, mock_rem_unlock_desc_node);
    ASSERT_EQ(0, mock_truncate_calls);              /* local tail not taken */
    ASSERT_EQ(0, mock_ml_lock_depth);
    (void)e;
}

/* 0x00E6026C-0x00E60278: a non-pending entry's mode is remapped through
 * FILE_$LOCK_MAP_TABLE before it goes on the wire. */
TEST(non_pending_remote_mode_is_mapped)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;

    reset();
    make_entry(ENTRY_A, &TEST_UID, 4, FILE_LOCK_F2_REMOTE, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(FILE_$LOCK_MAP_TABLE[4], mock_rem_unlock_mode);

    /* A pending entry (flags2 bit 1) keeps its own mode. */
    reset();
    make_entry(ENTRY_A, &TEST_UID, 4,
               FILE_LOCK_F2_REMOTE | FILE_LOCK_F2_PENDING, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(4, mock_rem_unlock_mode);
}

/* 0x00E602A6: REM_FILE_$UNLOCK's status only wins when local_status is 0. */
TEST(rem_file_unlock_status_is_reported)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;

    reset();
    mock_rem_unlock_status = 0x000F0004;
    make_entry(ENTRY_A, &TEST_UID, 4, FILE_LOCK_F2_REMOTE, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0x000F0004, st);
    ASSERT_EQ(0, mock_cond_flush_calls);         /* status3 != 0 */
}

/* 0x00E602C4: a negative result byte from REM_FILE_$UNLOCK asks for a
 * conditional flush. */
TEST(negative_rem_file_unlock_result_conditionally_flushes)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;

    reset();
    mock_rem_unlock_result = 0x80;
    make_entry(ENTRY_A, &TEST_UID, 4, FILE_LOCK_F2_REMOTE, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(1, mock_cond_flush_calls);
}

/* 0x00E6021E-0x00E6026A: a remote entry with no surviving lock consults
 * AST_$GET_COMMON_ATTRIBUTES(0x30) and, when its +0x14 word is zero, asks the
 * holder for a read lock; a "not locked" answer schedules the truncate. */
TEST(remote_entry_read_lock_probe_schedules_a_truncate)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;

    reset();
    mock_cattr_refcount = 0;
    mock_local_read_lock_status = file_$object_not_locked_by_this_process;
    make_entry(ENTRY_A, &TEST_UID, 4, FILE_LOCK_F2_REMOTE, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(1, mock_cattr_calls);
    ASSERT_EQ(0x30, mock_cattr_flags);
    ASSERT_EQ(1, mock_local_read_lock_calls);
    ASSERT_EQ(1, mock_truncate_calls);

    /* A non-zero +0x14 word skips the probe entirely. */
    reset();
    mock_cattr_refcount = 1;
    make_entry(ENTRY_A, &TEST_UID, 4, FILE_LOCK_F2_REMOTE, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;

    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0, mock_local_read_lock_calls);
    ASSERT_EQ(0, mock_truncate_calls);
}

/* ============================================================================
 * dtv_out and the return value
 * ============================================================================ */

/* 0x00E601C2 `move.b D5b,D2b; and.b (0x14,A6),D2b; bpl` - the DTV is only
 * produced for a by_key unlock of an exclusive lock. */
TEST(dtv_is_only_produced_for_an_exclusive_by_key_unlock)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0xDEAD;
    file_lock_entry_detail_t *e;

    reset();
    e = make_entry(ENTRY_A, &TEST_UID, 4, 0, 1);
    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 4, 0,
                            -1, e->sequence, e->context, e->node_low,
                            &dtv, &st);
    ASSERT_EQ(1, mock_get_dtv_calls);
    ASSERT_EQ(0xCAFEBABEu, dtv);

    /* Not exclusive (mode 2): no DTV. */
    reset();
    dtv = 0xDEAD;
    e = make_entry(ENTRY_A, &TEST_UID, 2, 0, 1);
    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 2, 0,
                            -1, e->sequence, e->context, e->node_low,
                            &dtv, &st);
    ASSERT_EQ(0, mock_get_dtv_calls);
    ASSERT_EQ(0, dtv);                          /* cleared at 0x00E5FD50 */

    /* Exclusive but not by_key: no DTV either. */
    reset();
    dtv = 0xDEAD;
    make_entry(ENTRY_A, &TEST_UID, 4, 0, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;
    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0, mock_get_dtv_calls);
}

/* 0x00E601EE: a failing AST_$GET_DTV clears the output again. */
TEST(a_failing_get_dtv_clears_the_output)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0xDEAD;
    file_lock_entry_detail_t *e;

    reset();
    mock_get_dtv_status = 0x000F0001;
    e = make_entry(ENTRY_A, &TEST_UID, 4, 0, 1);
    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 4, 0,
                            -1, e->sequence, e->context, e->node_low,
                            &dtv, &st);
    ASSERT_EQ(1, mock_get_dtv_calls);
    ASSERT_EQ(0, dtv);
    ASSERT_EQ(0, st);           /* the failure goes to the scratch status */
}

/* 0x00E6039C `seq D0b; and.b (-0xd8,A6),D0b`: the result byte survives only
 * when the reported status is zero. */
TEST(the_result_byte_is_gated_on_a_zero_status)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;
    boolean r;

    reset();
    mock_truncate_result = 0x01;
    make_entry(ENTRY_A, &TEST_UID, 4, 0, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;
    r = FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                          0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, (uint8_t)r);

    reset();
    mock_rem_unlock_result = 0x01;
    mock_rem_unlock_status = 0x000F0004;
    make_entry(ENTRY_A, &TEST_UID, 4, FILE_LOCK_F2_REMOTE, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 5) = ENTRY_A;
    r = FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                          0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0x000F0004, st);
    ASSERT_EQ(0, (uint8_t)r);
}

/* 0x00E6037C: "not locked by this process" is suppressed once something was
 * actually released, and reported otherwise. */
TEST(not_locked_is_suppressed_only_after_a_release)
{
    status_$t st = 0xDEAD;
    uint32_t dtv = 0;

    reset();
    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 5, 4, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(file_$object_not_locked_by_this_process, st);

    reset();
    FILE_$LOCK_TABLE2[TEST_ASID] = 2;
    make_entry(ENTRY_A, &TEST_UID, 4, 0, 1);
    FILE_$PROC_LOT_SLOT(TEST_ASID, 1) = ENTRY_A;
    (void)FILE_$PRIV_UNLOCK((uid_t *)&TEST_UID, 0, 0, TEST_ASID,
                            0, 0, 0, 0, &dtv, &st);
    ASSERT_EQ(0, st);
}

/* ============================================================================
 * The hash modulus constant cell (0x00E5EA28)
 * ============================================================================ */

TEST(hash_modulus_cell_is_251)
{
    ASSERT_EQ(251, file_$lot_hash_modulus);
}

int main(void)
{
    printf("FILE_$PRIV_UNLOCK (0x00E5FD32) tests\n");

    RUN_TEST(mode_8_without_by_key_never_touches_the_tables);
    RUN_TEST(mode_8_with_by_key_enters_the_body);
    RUN_TEST(mode_9_never_touches_the_tables);

    RUN_TEST(slot_above_0x96_is_invalid_arg);
    RUN_TEST(only_the_low_word_of_the_slot_argument_is_used);
    RUN_TEST(explicit_slot_with_wrong_uid_is_not_locked);
    RUN_TEST(explicit_slot_rejects_flags2_bit0);
    RUN_TEST(explicit_slot_with_mode_zero_skips_the_mode_tests);

    RUN_TEST(a_shared_entry_is_only_decremented);
    RUN_TEST(the_last_reference_unlinks_and_frees_the_entry);
    RUN_TEST(another_lock_on_the_same_uid_suppresses_the_truncate);
    RUN_TEST(another_exclusive_lock_suppresses_the_purify);
    RUN_TEST(purify_gets_the_pc_relative_nil_list_and_0x8000);

    RUN_TEST(slot_zero_scans_the_process_row);
    RUN_TEST(row_scan_requires_flags2_bit0_when_a_mode_is_given);
    RUN_TEST(mode_zero_releases_every_matching_lock);

    RUN_TEST(by_key_matches_key_rem_key_and_rem_node);
    RUN_TEST(by_key_with_key_zero_matches_any_sequence);
    RUN_TEST(by_key_skips_entries_flagged_remote);
    RUN_TEST(by_key_skips_entries_with_a_zero_refcount);
    RUN_TEST(by_key_miss_is_an_error_only_for_mode_8);
    RUN_TEST(by_key_mode_8_sets_the_delete_pending_attribute);

    RUN_TEST(remote_entry_forwards_context_and_sequence_to_rem_file_unlock);
    RUN_TEST(non_pending_remote_mode_is_mapped);
    RUN_TEST(rem_file_unlock_status_is_reported);
    RUN_TEST(negative_rem_file_unlock_result_conditionally_flushes);
    RUN_TEST(remote_entry_read_lock_probe_schedules_a_truncate);

    RUN_TEST(dtv_is_only_produced_for_an_exclusive_by_key_unlock);
    RUN_TEST(a_failing_get_dtv_clears_the_output);
    RUN_TEST(the_result_byte_is_gated_on_a_zero_status);
    RUN_TEST(not_locked_is_suppressed_only_after_a_release);

    RUN_TEST(hash_modulus_cell_is_251);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
