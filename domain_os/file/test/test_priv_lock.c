/*
 * file/test/test_priv_lock.c - unit tests for FILE_$PRIV_LOCK (0x00E5F0EE)
 *
 * The real file/priv_lock.c is #included at the bottom, together with the
 * real file/file_data.c so that the lock-mode, compatibility and conflict
 * tables carry the exact values read out of the binary image.  Everything the
 * function calls out to is mocked here.
 *
 * The behaviours exercised are the ones the 2026-09-06 audit found wrong:
 *   - the conflict tests in the change path are the CONSEQUENT of a
 *     successful btst (0x00E5F04A / 0x00E5F48A), not its alternative;
 *   - the conflict matrix at FILE_$LOCK_CONTROL+0x18 and the cross-node
 *     special cases for mapped modes 2 and 6;
 *   - the status constants (0x000F0007, 0x000F0015, 0x000F0016, 0x000F0011);
 *   - the flags word bit numbers (word at A6+0x14, not a longword);
 *   - alloc_entry's parameter order (add_to_proc, skip_fill).
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
        printf("  %-42s ", #name);                                            \
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
static int      mock_uid_lock_acquires;
static int      mock_uid_lock_releases;
static int      mock_rem_lock_calls;
static int      mock_set_attr_calls;
static uint32_t mock_set_attr_value;
static int      mock_hint_addi_calls;
static int16_t  mock_hash = 7;

/* AST_$GET_ATTRIBUTES canned answers */
static status_$t mock_attr_status;
static uint8_t   mock_attr_not_empty;
static uint8_t   mock_attr_obj_type;
static uint16_t  mock_attr_vol_flags;
static int8_t    mock_attr_desc_flags;
static uint32_t  mock_attr_desc_node;

/* HINT_$GET_HINTS canned answer */
static int16_t   mock_hint_count;
static uint32_t  mock_hint_node[FILE_LOCK_MAX_HINTS + 1];

/* ACL canned answer */
static int16_t   mock_acl_rights;
static status_$t mock_acl_status;

/* ============================================================================
 * Mocks
 * ============================================================================ */

void ML_$LOCK(int16_t id)   { (void)id; mock_ml_lock_depth++;  mock_ml_lock_calls++; }
void ML_$UNLOCK(int16_t id) { (void)id; mock_ml_lock_depth--;  mock_ml_unlock_calls++; }

uint32_t UID_$HASH(uid_t *uid, uint16_t *table_size)
{
    (void)uid; (void)table_size;
    return (uint32_t)(uint16_t)mock_hash;
}

int16_t HINT_$GET_HINTS(uid_t *file_uid, uint32_t *addresses)
{
    int16_t i;

    (void)file_uid;
    for (i = 0; i < mock_hint_count; i++) {
        addresses[i * 2]     = 0;
        addresses[i * 2 + 1] = mock_hint_node[i + 1];
    }
    return mock_hint_count;
}

void HINT_$ADDI(uid_t *uid_ptr, uint32_t *addresses)
{
    (void)uid_ptr; (void)addresses; mock_hint_addi_calls++;
}

void HINT_$LOOKUP_CACHE(uint32_t *key, uint8_t *result) { (void)key; *result = 0; }
void HINT_$ADD_CACHE(uint32_t *key, uint8_t *result)    { (void)key; (void)result; }

void AST_$GET_ATTRIBUTES(file_$obj_loc_t *desc, uint16_t flags, void *attrs,
                         status_$t *status)
{
    uint8_t *a = (uint8_t *)attrs;

    (void)flags;
    memset(a, 0, FILE_ATTR_FULL_SIZE);
    a[0] = mock_attr_not_empty;
    a[1] = mock_attr_obj_type;
    /* attrs+2 is a native 16-bit field in this port (the original reads it
     * with `move.w (-0xd6,A6),D3w` at 0x00E5F7A4). */
    *(uint16_t *)(void *)(a + 2) = mock_attr_vol_flags;
    /* The real callee refills the whole 32-byte descriptor from the AOTE. */
    desc->flags = mock_attr_desc_flags;
    desc->node  = mock_attr_desc_node;
    desc->loc_info = 0;
    desc->rights_bits = 0;
    *status = mock_attr_status;
}

uint16_t AST_$PURIFY(uid_t *uid, uint16_t flags, int16_t segment,
                     uint32_t *segment_list, uint16_t unused, status_$t *status)
{
    (void)uid; (void)flags; (void)segment; (void)segment_list; (void)unused;
    *status = 0;
    return 0;
}

void AST_$COND_FLUSH(uid_t *uid, uint32_t *timestamp, status_$t *status)
{
    (void)uid; (void)timestamp; *status = 0;
}

void AST_$LOAD_AOTE(uint32_t *attrs, uint32_t *obj_info)
{
    (void)attrs; (void)obj_info;
}

void AST_$SET_ATTRIBUTE(uid_t *uid, uint16_t attr_id, void *value, status_$t *status)
{
    (void)uid; (void)attr_id;
    mock_set_attr_calls++;
    mock_set_attr_value = *(uint32_t *)value;
    *status = 0;
}

uint32_t ACL_$RIGHTS(uid_t *uid, boolean *ignore_super, uint32_t *required_mask,
                     int16_t *option_flags, status_$t *status)
{
    (void)uid; (void)ignore_super; (void)required_mask; (void)option_flags;
    *status = mock_acl_status;
    return mock_acl_rights;
}

int16_t ACL_$RIGHTS_CHECK(void *acl_ctx, uid_t *file_uid,
                          void *required_mask, void *option_flags,
                          int8_t *check_flag, status_$t *status)
{
    (void)acl_ctx; (void)file_uid; (void)required_mask; (void)option_flags;
    (void)check_flag;
    *status = mock_acl_status;
    return mock_acl_rights;
}

void FILE_$UID_LOCK_ACQUIRE(uid_t *uid) { (void)uid; mock_uid_lock_acquires++; }
void FILE_$UID_LOCK_RELEASE(uid_t *uid) { (void)uid; mock_uid_lock_releases++; }

void REM_FILE_$LOCK(void *location_block, uint16_t lock_mode, uint16_t lock_type,
                    uint16_t flags, uint16_t wait_flag, int8_t extended,
                    uint32_t lock_key, uint16_t *packet_id_out,
                    uint16_t *status_word, void *lock_result, status_$t *status)
{
    (void)location_block; (void)lock_mode; (void)lock_type; (void)flags;
    (void)wait_flag; (void)extended; (void)lock_key; (void)packet_id_out;
    (void)lock_result;
    mock_rem_lock_calls++;
    *status_word = 0x1234;
    *status = 0;
}

void FILE_$READ_LOCK_ENTRYUI(uid_t *file_uid, void *info_out, status_$t *status)
{
    (void)file_uid; (void)info_out;
    *status = 0;              /* not "not locked by this process": no retry */
}

void OS_PROC_SHUTWIRED(status_$t *status) { (void)status; }

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
#include "../priv_lock.c"

/* ============================================================================
 * Fixtures
 * ============================================================================ */

#define TEST_ASID       3
#define FREE_ENTRY      100
#define HELD_ENTRY      50

static uid_t     test_uid;
static uint32_t  test_slot;
static uint16_t  test_rights;
static status_$t test_status;
static void     *test_acl_ctx;

static void reset_world(void)
{
    memset(FILE_$LOCK_ENTRIES, 0, sizeof(FILE_$LOCK_ENTRIES));
    memset(FILE_$LOCK_TABLE, 0,
           sizeof(file_lock_table_entry_t) * FILE_LOCK_TABLE_ENTRIES);
    memset(FILE_$LOCK_TABLE2, 0,
           sizeof(uint16_t) * FILE_LOCK_TABLE_ENTRIES);
    memset(FILE_$LOT_HASHTAB, 0,
           sizeof(uint16_t) * FILE_LOCK_TABLE_ENTRIES);

    FILE_$LOT_FULL    = 0;
    FILE_$LOT_FREE    = FREE_ENTRY;
    FILE_$LOT_HIGH    = 0;
    FILE_$LOT_SEQN    = 1;
    FILE_$LOT_PENDING = 0;
    FILE_$LOT_ENTRY(FREE_ENTRY)->next = 0;

    mock_ml_lock_depth     = 0;
    mock_ml_lock_calls     = 0;
    mock_ml_unlock_calls   = 0;
    mock_uid_lock_acquires = 0;
    mock_uid_lock_releases = 0;
    mock_rem_lock_calls    = 0;
    mock_set_attr_calls    = 0;
    mock_set_attr_value    = 0xDEADBEEF;
    mock_hint_addi_calls   = 0;
    mock_hash              = 7;

    mock_attr_status     = 0;
    mock_attr_not_empty  = 0;
    mock_attr_obj_type   = 0;          /* not a directory */
    mock_attr_vol_flags  = 0;          /* not mounted read-only */
    mock_attr_desc_flags = 0;          /* object is local */
    mock_attr_desc_node  = NODE_$ME;

    mock_hint_count   = 1;
    mock_hint_node[1] = NODE_$ME;

    mock_acl_rights = 0x7FFF;
    mock_acl_status = 0;

    test_uid.high = 0x11223344;        /* first byte non-zero: not a nil UID */
    test_uid.low  = 0x00055555;
    test_slot     = 0;
    test_rights   = 0;
    test_status   = 0x5A5A5A5A;
    test_acl_ctx  = NULL;
}

/* Install a held lock on the same UID in the hash bucket. */
static file_lock_entry_detail_t *hold_lock(uint16_t entry_side,
                                           uint16_t entry_mode,
                                           uint32_t node,
                                           uint8_t rights,
                                           uint8_t extra_flags2)
{
    file_lock_entry_detail_t *e = FILE_$LOT_ENTRY(HELD_ENTRY);

    e->uid_high = test_uid.high;
    e->uid_low  = test_uid.low;
    e->node_low = node;
    e->node_high = node;
    e->refcount = 1;
    e->rights   = rights;
    e->flags1   = 0;
    e->flags2   = (uint8_t)((entry_side << 7) | ((entry_mode & 0x0F) << 3) |
                            extra_flags2);
    e->next     = 0;
    e->sequence = 0;
    e->context  = 0;

    FILE_$LOT_HASHTAB[mock_hash] = HELD_ENTRY;
    FILE_$PROC_LOT_SLOT(TEST_ASID, 1) = HELD_ENTRY;
    FILE_$PROC_LOT_COUNT(TEST_ASID)   = 1;
    return e;
}

static void call_new_lock(uint16_t side, uint16_t mode, uint16_t flags)
{
    FILE_$PRIV_LOCK(&test_uid, TEST_ASID, side, mode,
                    0 /* local_only */, flags, 0 /* key */,
                    0, 0, 0, &test_acl_ctx, 0,
                    &test_slot, &test_rights, &test_status);
}

static void call_change_lock(uint16_t side, uint16_t mode, uint16_t flags,
                             uint32_t slot)
{
    test_slot = slot;
    FILE_$PRIV_LOCK(&test_uid, TEST_ASID, side, mode,
                    0 /* local_only */, flags, 0 /* key */,
                    0, 0, 0, &test_acl_ctx, 0,
                    &test_slot, &test_rights, &test_status);
}

/* ============================================================================
 * Tests: entry validation
 * ============================================================================ */

TEST(table_full_is_0x000F0009)
{
    reset_world();
    FILE_$LOT_FULL = -1;                    /* Domain boolean TRUE */
    call_new_lock(0, 1, FILE_LOCK_FLAG_NO_RIGHTS);
    ASSERT_EQ(file_$local_lock_table_full, test_status);
    ASSERT_EQ(0x000F0009, test_status);
}

TEST(unmapped_mode_is_0x000F0007)
{
    reset_world();
    /* FILE_$LOCK_MODE_TABLE[0][8] == 0 -> mapped_mode 0 -> illegal. */
    ASSERT_EQ(0, FILE_$LOCK_MODE_TABLE[8]);
    call_new_lock(0, 8, FILE_LOCK_FLAG_NO_RIGHTS);
    ASSERT_EQ(file_$illegal_lock_request, test_status);
    ASSERT_EQ(0x000F0007, test_status);
}

TEST(bad_side_is_rejected)
{
    reset_world();
    call_new_lock(2, 1, FILE_LOCK_FLAG_NO_RIGHTS);
    ASSERT_EQ(file_$illegal_lock_request, test_status);
}

TEST(mode_above_11_is_rejected)
{
    reset_world();
    /* btst against 0x0FFF admits only modes 0..11. */
    call_new_lock(0, 12, FILE_LOCK_FLAG_NO_RIGHTS);
    ASSERT_EQ(file_$illegal_lock_request, test_status);
}

/* ============================================================================
 * Tests: the new-lock path and the conflict matrix
 * ============================================================================ */

TEST(uncontended_local_lock_succeeds)
{
    file_lock_entry_detail_t *e;

    reset_world();
    call_new_lock(0, 1, FILE_LOCK_FLAG_NO_RIGHTS);

    ASSERT_EQ(0, test_status);
    /* The entry came off the free list and was pushed onto the bucket. */
    ASSERT_EQ(FREE_ENTRY, FILE_$LOT_HASHTAB[mock_hash]);
    e = FILE_$LOT_ENTRY(FREE_ENTRY);
    ASSERT_EQ(1, e->refcount);
    ASSERT_EQ(0, e->next);
    ASSERT_EQ(test_uid.high, e->uid_high);
    ASSERT_EQ(test_uid.low, e->uid_low);
    /* flags2: side 0, mode 1 in bits 3..6, PENDING set, REMOTE clear. */
    ASSERT_EQ(1 << 3, e->flags2 & FILE_LOCK_F2_MODE_MASK);
    ASSERT_EQ(0, e->flags2 & FILE_LOCK_F2_SIDE);
    ASSERT_EQ(0, e->flags2 & FILE_LOCK_F2_REMOTE);
    ASSERT_EQ(FILE_LOCK_F2_PENDING, e->flags2 & FILE_LOCK_F2_PENDING);
    /* add_to_proc was TRUE, so a per-process slot was claimed. */
    ASSERT_EQ(1, test_slot);
    ASSERT_EQ(FREE_ENTRY, FILE_$PROC_LOT_SLOT(TEST_ASID, 1));
    /* NO_RIGHTS short-circuits the ACL call with 0x10. */
    ASSERT_EQ(0x10, test_rights);
    /* The UID bucket lock was taken and dropped exactly once. */
    ASSERT_EQ(1, mock_uid_lock_acquires);
    ASSERT_EQ(1, mock_uid_lock_releases);
    ASSERT_EQ(0, mock_ml_lock_depth);
}

TEST(conflict_matrix_rejects_incompatible_hold)
{
    reset_world();
    /*
     * request side 0 mode 1 -> mapped 3, FILE_$LOCK_CONFLICT_TABLE[3] = 0x5B
     * held    side 0 mode 3 -> mapped 5, and bit 5 of 0x5B is clear.
     */
    ASSERT_EQ(3, FILE_$LOCK_MODE_TABLE[1]);
    ASSERT_EQ(5, FILE_$LOCK_MODE_TABLE[3]);
    ASSERT_EQ(0x005B, FILE_$LOCK_CONFLICT_TABLE[3]);

    hold_lock(0, 3, NODE_$ME, 0xFF, 0);
    call_new_lock(0, 1, FILE_LOCK_FLAG_NO_RIGHTS);
    ASSERT_EQ(file_$object_in_use, test_status);
    ASSERT_EQ(0x000F0006, test_status);
    /* The speculatively allocated entry went back on the free list. */
    ASSERT_EQ(FREE_ENTRY, FILE_$LOT_FREE);
    ASSERT_EQ(HELD_ENTRY, FILE_$LOT_HASHTAB[mock_hash]);
    ASSERT_EQ(0, mock_ml_lock_depth);
}

TEST(conflict_matrix_admits_compatible_hold)
{
    reset_world();
    /* held side 0 mode 1 -> mapped 3; bit 3 of 0x5B is set. */
    hold_lock(0, 1, NODE_$ME, 0xFF, 0);
    call_new_lock(0, 1, FILE_LOCK_FLAG_NO_RIGHTS);
    ASSERT_EQ(0, test_status);
    ASSERT_EQ(FREE_ENTRY, FILE_$LOT_HASHTAB[mock_hash]);
    ASSERT_EQ(HELD_ENTRY, FILE_$LOT_ENTRY(FREE_ENTRY)->next);
}

TEST(mapped_mode_6_conflicts_across_nodes)
{
    reset_world();
    /*
     * 0x00E5F05E: two mapped-mode-6 locks are compatible per the matrix
     * (bit 6 of CONFLICT_TABLE[6] = 0x4F is set) but not across nodes.
     * side 1 mode 2 maps to 6.
     */
    ASSERT_EQ(6, FILE_$LOCK_MODE_TABLE[12 + 2]);
    ASSERT_EQ(0x004F, FILE_$LOCK_CONFLICT_TABLE[6]);

    hold_lock(1, 2, 0x000AAAAA, 0xFF, 0);   /* different node */
    call_new_lock(1, 2, FILE_LOCK_FLAG_NO_RIGHTS);
    ASSERT_EQ(file_$object_in_use, test_status);
}

TEST(mapped_mode_6_ok_on_same_node)
{
    reset_world();
    hold_lock(1, 2, NODE_$ME, 0xFF, 0);
    call_new_lock(1, 2, FILE_LOCK_FLAG_NO_RIGHTS);
    ASSERT_EQ(0, test_status);
}

TEST(mapped_mode_2_conflicts_across_nodes)
{
    reset_world();
    /* side 1 mode 4 maps to 2 and mode 4 is not in FILE_$LOCK_ILLEGAL_MASK
     * (0x00E8), so it takes the new-lock path.  CONFLICT_TABLE[2] = 0x47 has
     * bit 2 set, so the matrix alone would allow it. */
    ASSERT_EQ(2, FILE_$LOCK_MODE_TABLE[12 + 4]);
    ASSERT_EQ(0x0047, FILE_$LOCK_CONFLICT_TABLE[2]);
    ASSERT_EQ(0, FILE_$LOCK_ILLEGAL_MASK & (1u << 4));

    hold_lock(1, 4, 0x000AAAAA, 0xFF, 0);
    call_new_lock(1, 4, FILE_LOCK_FLAG_NO_RIGHTS);
    ASSERT_EQ(file_$object_in_use, test_status);
}

TEST(mapped_mode_2_ok_on_same_node)
{
    reset_world();
    hold_lock(1, 4, NODE_$ME, 0xFF, 0);
    call_new_lock(1, 4, FILE_LOCK_FLAG_NO_RIGHTS);
    ASSERT_EQ(0, test_status);
}

TEST(read_only_volume_rejects_writing_mode)
{
    reset_world();
    /*
     * 0x00E5F7A4: volume flags bit 1 plus a mode whose entry in
     * FILE_$LOCK_COMPAT_TABLE has bit 1 set.  Mode 2's entry is 6.
     */
    ASSERT_EQ(6, FILE_$LOCK_COMPAT_TABLE[2]);
    mock_attr_vol_flags = 0x0002;
    call_new_lock(0, 2, FILE_LOCK_FLAG_NO_RIGHTS);
    /* Not a directory -> the file_$ flavour, 0x000F0016. */
    ASSERT_EQ(status_$file_volume_has_been_mounted_read_only, test_status);
    ASSERT_EQ(0x000F0016, test_status);
}

TEST(read_only_volume_directory_uses_naming_status)
{
    reset_world();
    mock_attr_vol_flags = 0x0002;
    mock_attr_obj_type  = 1;             /* directory */
    call_new_lock(0, 2, FILE_LOCK_FLAG_NO_RIGHTS);
    ASSERT_EQ(status_$naming_vol_mounted_read_only, test_status);
    ASSERT_EQ(0x000E0030, test_status);
}

TEST(non_empty_directory_delete_lock_is_rejected)
{
    reset_world();
    /* 0x00E5F784: type 1/2, non-empty, mode 1, flags bit 7. */
    mock_attr_obj_type  = 2;
    mock_attr_not_empty = 1;
    call_new_lock(0, 1, (uint16_t)(FILE_LOCK_FLAG_NO_RIGHTS |
                                   FILE_LOCK_FLAG_FOR_DELETE));
    ASSERT_EQ(status_$naming_bad_directory, test_status);
    ASSERT_EQ(0x000E000D, test_status);
}

TEST(remote_object_with_local_only_is_rejected)
{
    reset_world();
    /* 0x00E5F822: the AOTE says "remote" and the caller demanded local. */
    mock_attr_desc_flags = (int8_t)FILE_OBJ_LOC_REMOTE;
    FILE_$PRIV_LOCK(&test_uid, TEST_ASID, 0, 1,
                    -1 /* local_only TRUE */, FILE_LOCK_FLAG_NO_RIGHTS, 0,
                    0, 0, 0, &test_acl_ctx, 0,
                    &test_slot, &test_rights, &test_status);
    ASSERT_EQ(file_$cannot_create_on_remote_with_uid, test_status);
    ASSERT_EQ(0x000F000B, test_status);
}

TEST(no_hints_reports_object_not_found)
{
    reset_world();
    mock_hint_count = 0;                 /* the loop body never runs */
    call_new_lock(0, 1, FILE_LOCK_FLAG_NO_RIGHTS);
    ASSERT_EQ(file_$object_not_found, test_status);
    ASSERT_EQ(0x000F0001, test_status);
}

TEST(insufficient_rights_on_new_lock)
{
    reset_world();
    /* Without NO_RIGHTS, CHECK_RIGHTS runs; with CHECK_RIGHTS set it
     * enforces FILE_$LOCK_COMPAT_TABLE[mode]. */
    ASSERT_EQ(4, FILE_$LOCK_COMPAT_TABLE[1]);
    mock_acl_rights = 0x0002;            /* missing bit 2 */
    call_new_lock(0, 1, FILE_LOCK_FLAG_CHECK_RIGHTS);
    ASSERT_EQ(status_$insufficient_rights, test_status);
    ASSERT_EQ(0x000F0011, test_status);
}

TEST(no_rights_at_all_reports_0x000F0010)
{
    reset_world();
    mock_acl_rights = 0;
    call_new_lock(0, 1, FILE_LOCK_FLAG_CHECK_RIGHTS);
    ASSERT_EQ(status_$no_rights, test_status);
    ASSERT_EQ(0x000F0010, test_status);
}

TEST(set_attribute_records_holder_node)
{
    reset_world();
    /* mapped mode 5 (side 0 mode 3) makes CHECK_CONFLICTS stamp node_id. */
    ASSERT_EQ(5, FILE_$LOCK_MODE_TABLE[3]);
    /* Mode 3 is in FILE_$LOCK_ILLEGAL_MASK (0x00E8), so reach it via side 1
     * mode 4 which also maps to 2 ... use side 0 mode 4 -> mapped 5. */
    ASSERT_EQ(5, FILE_$LOCK_MODE_TABLE[4]);
    call_new_lock(0, 4, FILE_LOCK_FLAG_NO_RIGHTS);
    ASSERT_EQ(0, test_status);
    ASSERT_EQ(1, mock_set_attr_calls);
    ASSERT_EQ(NODE_$ME, mock_set_attr_value);
}

/* ============================================================================
 * Tests: the change path
 * ============================================================================ */

/*
 * The change-path fixtures use request mode 6 against a held entry whose raw
 * mode field is 1:
 *   FILE_$LOCK_ILLEGAL_MASK bit 6 is set     -> the change path is taken
 *   FILE_$LOCK_CVT_TABLE[6]  = 0x0002 bit 1  -> the held entry is a match
 *   FILE_$LOCK_MODE_TABLE[0][6] = 4          -> request maps to 4
 *   FILE_$LOCK_CONFLICT_TABLE[4] = 0x000B bit 3 set, and the held entry maps
 *                                               to 3, so they are compatible
 *   FILE_$LOCK_COMPAT_TABLE[6] = 2           -> bit 1, the read-only test
 *   FILE_$LOCK_REQ_TABLE[6]   = 2            -> the new mode field
 */
#define CHG_MODE        6
#define CHG_HELD_MODE   1

TEST(change_path_fixture_is_what_the_tables_say)
{
    ASSERT_EQ(0x00E8, FILE_$LOCK_ILLEGAL_MASK);
    ASSERT_EQ(0x0002, FILE_$LOCK_CVT_TABLE[CHG_MODE]);
    ASSERT_EQ(4, FILE_$LOCK_MODE_TABLE[CHG_MODE]);
    ASSERT_EQ(3, FILE_$LOCK_MODE_TABLE[CHG_HELD_MODE]);
    ASSERT_EQ(0x000B, FILE_$LOCK_CONFLICT_TABLE[4]);
    ASSERT_EQ(2, FILE_$LOCK_COMPAT_TABLE[CHG_MODE]);
    ASSERT_EQ(2, FILE_$LOCK_REQ_TABLE[CHG_MODE]);
}

TEST(change_of_remote_non_pending_entry_is_incompatible)
{
    reset_world();
    /*
     * 0x00E5F48A: all three of CHANGE, entry REMOTE and entry !PENDING must
     * hold before the request is refused.
     */
    hold_lock(0, CHG_HELD_MODE, NODE_$ME, 0xFF, FILE_LOCK_F2_REMOTE);
    call_change_lock(0, CHG_MODE, (uint16_t)(FILE_LOCK_FLAG_CHANGE |
                                             FILE_LOCK_FLAG_NO_RIGHTS), 1);
    ASSERT_EQ(file_$incompatible_request, test_status);
    ASSERT_EQ(0x000F0015, test_status);
}

TEST(change_of_remote_pending_entry_is_allowed)
{
    reset_world();
    hold_lock(0, CHG_HELD_MODE, NODE_$ME, 0xFF,
              (uint8_t)(FILE_LOCK_F2_REMOTE | FILE_LOCK_F2_PENDING));
    call_change_lock(0, CHG_MODE, (uint16_t)(FILE_LOCK_FLAG_CHANGE |
                                             FILE_LOCK_FLAG_NO_RIGHTS), 1);
    /* The remote round trip is taken instead of the flat refusal. */
    ASSERT_EQ(0, test_status);
    ASSERT_EQ(1, mock_rem_lock_calls);
}

TEST(change_of_local_entry_is_not_refused)
{
    reset_world();
    /* REMOTE clear -> the three-way test above must not fire. */
    hold_lock(0, CHG_HELD_MODE, NODE_$ME, 0xFF, 0);
    call_change_lock(0, CHG_MODE, (uint16_t)(FILE_LOCK_FLAG_CHANGE |
                                             FILE_LOCK_FLAG_NO_RIGHTS), 1);
    ASSERT_EQ(0, test_status);
    ASSERT_EQ(0, mock_rem_lock_calls);
    ASSERT_EQ(2 << 3,
              FILE_$LOT_ENTRY(HELD_ENTRY)->flags2 & FILE_LOCK_F2_MODE_MASK);
}

TEST(change_on_read_only_volume_entry_is_rejected)
{
    reset_world();
    /* 0x00E5F4B0: entry flags1 bit 7 + FILE_$LOCK_COMPAT_TABLE[mode] bit 1. */
    hold_lock(0, CHG_HELD_MODE, NODE_$ME, 0xFF, 0)->flags1 = 0x80;
    call_change_lock(0, CHG_MODE, (uint16_t)(FILE_LOCK_FLAG_CHANGE |
                                             FILE_LOCK_FLAG_NO_RIGHTS), 1);
    ASSERT_EQ(status_$file_volume_has_been_mounted_read_only, test_status);
    ASSERT_EQ(0x000F0016, test_status);
}

TEST(change_without_rights_is_rejected)
{
    reset_world();
    /* 0x00E5F4DC: without FILE_LOCK_FLAG_NO_RIGHTS the entry's own rights
     * byte must contain FILE_$LOCK_COMPAT_TABLE[mode] (2 for mode 6). */
    hold_lock(0, CHG_HELD_MODE, NODE_$ME, 0x01, 0);
    call_change_lock(0, CHG_MODE, FILE_LOCK_FLAG_CHANGE, 1);
    ASSERT_EQ(status_$insufficient_rights, test_status);
}

TEST(change_with_no_matching_entry_is_illegal)
{
    reset_world();
    /* Nothing in the per-process table -> found_entry stays 0. */
    call_change_lock(0, CHG_MODE, (uint16_t)(FILE_LOCK_FLAG_CHANGE |
                                             FILE_LOCK_FLAG_NO_RIGHTS), 0);
    ASSERT_EQ(file_$illegal_lock_request, test_status);
}

TEST(shared_entry_is_split_before_being_changed)
{
    file_lock_entry_detail_t *held;

    reset_world();
    held = hold_lock(0, CHG_HELD_MODE, NODE_$ME, 0xFF, 0);
    held->refcount = 2;                  /* 0x00E5F538 */
    call_change_lock(0, CHG_MODE, (uint16_t)(FILE_LOCK_FLAG_CHANGE |
                                             FILE_LOCK_FLAG_NO_RIGHTS), 1);
    ASSERT_EQ(0, test_status);
    /* The shared entry lost a reference and the slot now names the copy. */
    ASSERT_EQ(1, held->refcount);
    ASSERT_EQ(FREE_ENTRY, FILE_$PROC_LOT_SLOT(TEST_ASID, 1));
    ASSERT_EQ(1, FILE_$LOT_ENTRY(FREE_ENTRY)->refcount);
    /* rights_out was pre-loaded from the shared entry (0x00E5F53A). */
    ASSERT_EQ(0xFF, test_rights);
}

/* ============================================================================
 * Tests: helper-level behaviour
 * ============================================================================ */

TEST(alloc_entry_parameter_order)
{
    priv_lock_frame_t f;
    status_$t st;

    reset_world();
    memset(&f, 0, sizeof(f));
    f.file_uid   = &test_uid;
    f.asid       = TEST_ASID;
    f.side       = 0;
    f.lock_mode  = 1;
    f.slot_io    = &test_slot;
    f.status_ret = &test_status;
    f.from_remote = 0;

    /* add_to_proc FALSE, skip_fill TRUE: nothing is written and no slot is
     * claimed (0x00E5F544's argument order). */
    FILE_$LOT_ENTRY(FREE_ENTRY)->uid_high = 0xCAFEBABE;
    st = priv_lock_alloc_entry(&f, 0, -1);
    ASSERT_EQ(0, st);
    ASSERT_EQ(FREE_ENTRY, f.new_entry);
    ASSERT_EQ(0xCAFEBABE, FILE_$LOT_ENTRY(FREE_ENTRY)->uid_high);
    ASSERT_EQ(0, f.new_slot);
    ASSERT_EQ(0, FILE_$PROC_LOT_SLOT(TEST_ASID, 1));

    /* add_to_proc TRUE, skip_fill FALSE: the entry is filled and a slot is
     * claimed (0x00E5F884 / 0x00E5F93A). */
    reset_world();
    memset(&f, 0, sizeof(f));
    f.file_uid   = &test_uid;
    f.asid       = TEST_ASID;
    f.side       = 1;
    f.lock_mode  = 2;
    f.slot_io    = &test_slot;
    f.status_ret = &test_status;
    f.from_remote = 0;
    st = priv_lock_alloc_entry(&f, -1, 0);
    ASSERT_EQ(0, st);
    ASSERT_EQ(test_uid.high, FILE_$LOT_ENTRY(FREE_ENTRY)->uid_high);
    ASSERT_EQ(1, f.new_slot);
    ASSERT_EQ(FREE_ENTRY, FILE_$PROC_LOT_SLOT(TEST_ASID, 1));
    ASSERT_EQ(1, test_slot);
}

TEST(alloc_entry_reports_table_full)
{
    priv_lock_frame_t f;

    reset_world();
    memset(&f, 0, sizeof(f));
    f.file_uid   = &test_uid;
    f.asid       = TEST_ASID;
    f.slot_io    = &test_slot;
    f.status_ret = &test_status;
    FILE_$LOT_FREE = 0;
    ASSERT_EQ(file_$local_lock_table_full, priv_lock_alloc_entry(&f, 0, -1));
}

TEST(free_entry_returns_slot_and_entry)
{
    priv_lock_frame_t f;

    reset_world();
    memset(&f, 0, sizeof(f));
    f.asid      = TEST_ASID;
    f.new_entry = FREE_ENTRY;
    f.new_slot  = 4;
    FILE_$PROC_LOT_SLOT(TEST_ASID, 4) = FREE_ENTRY;
    FILE_$LOT_FREE = 0;

    priv_lock_free_entry(&f);
    ASSERT_EQ(FREE_ENTRY, FILE_$LOT_FREE);
    ASSERT_EQ(0, FILE_$PROC_LOT_SLOT(TEST_ASID, 4));
    ASSERT_EQ(0, f.new_entry);
    ASSERT_EQ(0, f.new_slot);
}

TEST(check_rights_flag_bits_are_word_sized)
{
    priv_lock_frame_t f;
    status_$t st = 0x5A5A;
    uint16_t rights = 0;

    reset_world();
    memset(&f, 0, sizeof(f));
    f.file_uid = &test_uid;
    f.lock_mode = 1;

    /* bit 3 of the flags WORD, not bit 19 of a longword. */
    f.flags = FILE_LOCK_FLAG_NO_RIGHTS;
    priv_lock_check_rights(&f, &st, &rights);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0x0010, rights);

    /* A nil UID short-circuits with 0x0F. */
    f.flags = 0;
    f.uid_is_null = -1;
    priv_lock_check_rights(&f, &st, &rights);
    ASSERT_EQ(0x000F, rights);
}

/* ============================================================================
 * main
 * ============================================================================ */

int main(void)
{
    printf("FILE_$PRIV_LOCK (0x00E5F0EE) tests\n");

    RUN_TEST(table_full_is_0x000F0009);
    RUN_TEST(unmapped_mode_is_0x000F0007);
    RUN_TEST(bad_side_is_rejected);
    RUN_TEST(mode_above_11_is_rejected);

    RUN_TEST(uncontended_local_lock_succeeds);
    RUN_TEST(conflict_matrix_rejects_incompatible_hold);
    RUN_TEST(conflict_matrix_admits_compatible_hold);
    RUN_TEST(mapped_mode_6_conflicts_across_nodes);
    RUN_TEST(mapped_mode_6_ok_on_same_node);
    RUN_TEST(mapped_mode_2_conflicts_across_nodes);
    RUN_TEST(mapped_mode_2_ok_on_same_node);
    RUN_TEST(read_only_volume_rejects_writing_mode);
    RUN_TEST(read_only_volume_directory_uses_naming_status);
    RUN_TEST(non_empty_directory_delete_lock_is_rejected);
    RUN_TEST(remote_object_with_local_only_is_rejected);
    RUN_TEST(no_hints_reports_object_not_found);
    RUN_TEST(insufficient_rights_on_new_lock);
    RUN_TEST(no_rights_at_all_reports_0x000F0010);
    RUN_TEST(set_attribute_records_holder_node);

    RUN_TEST(change_path_fixture_is_what_the_tables_say);
    RUN_TEST(change_of_remote_non_pending_entry_is_incompatible);
    RUN_TEST(change_of_remote_pending_entry_is_allowed);
    RUN_TEST(change_of_local_entry_is_not_refused);
    RUN_TEST(change_on_read_only_volume_entry_is_rejected);
    RUN_TEST(change_without_rights_is_rejected);
    RUN_TEST(change_with_no_matching_entry_is_illegal);
    RUN_TEST(shared_entry_is_split_before_being_changed);

    RUN_TEST(alloc_entry_parameter_order);
    RUN_TEST(alloc_entry_reports_table_full);
    RUN_TEST(free_entry_returns_slot_and_entry);
    RUN_TEST(check_rights_flag_bits_are_word_sized);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
