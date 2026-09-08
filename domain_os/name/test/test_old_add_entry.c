/*
 * name/test/test_old_add_entry.c - name_$old_add_entry (0x00E56682)
 *
 * The behaviour bead source-0o3n is about: 0x00E5670A-0x00E56722 builds ONE
 * 8-byte record in the frame -
 *
 *   move.l D2,(-0x10,A6)          ; the caller's location word
 *   move.l #0xfffff,D0
 *   and.l  (0x4,A4),D0            ; file_uid->low & 0xFFFFF
 *   move.l D0,(-0xc,A6)
 *   pea    (-0x10,A6)             ; the record's ADDRESS
 *   pea    (A4)
 *   jsr    HINT_$ADDI
 *
 * - and hands HINT_$ADDI its address.  The tree used to declare two adjacent
 * locals and rely on the C compiler laying them out contiguously, which the
 * standard does not promise; it is now a hint_addr_t.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  Running %-46s ", #name);          \
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

#include "name/name_internal.h"
#include "dir/dir.h"
#include "hint/hint.h"

/* ------------------------------------------------------------------ */
/* Mocks                                                              */
/* ------------------------------------------------------------------ */

static int8_t    mock_validate_result;
static int       mock_validate_calls;
static uint16_t  mock_parsed_len;

static int       mock_lock_calls;
static int16_t   mock_lock_mode;
static int16_t   mock_lock_rights;
static status_$t mock_lock_status;
static uint32_t  mock_lock_handle;

static int       mock_add_calls;
static uint32_t  mock_add_handle;
static uint16_t  mock_add_type;
static uint32_t  mock_add_extra;
static uint8_t   mock_add_replace;
static status_$t mock_add_status;

static int        mock_addi_calls;
static uid_t     *mock_addi_uid;
static hint_addr_t mock_addi_record;
static const void *mock_addi_ptr;

static int       mock_unlock_calls;
static status_$t mock_unlock_status;

static int       mock_exit_super_calls;

int8_t name_$validate_leaf(char *name, uint16_t name_len,
                           uint8_t *parsed, uint16_t *parsed_len)
{
    mock_validate_calls++;
    (void)name; (void)name_len;
    parsed[0] = 'x';
    *parsed_len = mock_parsed_len;
    return mock_validate_result;
}

void NAME_$LOCK_DIR(uid_t *dir_uid, uint32_t *handle_ret,
                    int16_t lock_mode, int16_t acl_rights,
                    status_$t *status_ret)
{
    (void)dir_uid;
    mock_lock_calls++;
    mock_lock_mode = lock_mode;
    mock_lock_rights = acl_rights;
    *handle_ret = mock_lock_handle;
    *status_ret = mock_lock_status;
}

void NAME_$UNLOCK_DIR(status_$t *status_ret)
{
    mock_unlock_calls++;
    *status_ret = mock_unlock_status;
}

void dir_$old_add_entry_ext(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                            uint16_t name_len, uint16_t type, void *uid_data,
                            uint32_t extra, uint8_t replace_flag,
                            uint8_t *result, status_$t *status_ret)
{
    (void)dir_uid; (void)name; (void)name_len; (void)uid_data; (void)result;
    mock_add_calls++;
    mock_add_handle  = handle;
    mock_add_type    = type;
    mock_add_extra   = extra;
    mock_add_replace = replace_flag;
    *status_ret = mock_add_status;
}

void HINT_$ADDI(uid_t *uid_ptr, uint32_t *addresses)
{
    mock_addi_calls++;
    mock_addi_uid = uid_ptr;
    mock_addi_ptr = addresses;
    memcpy(&mock_addi_record, addresses, sizeof(hint_addr_t));
}

void ACL_$EXIT_SUPER(void)
{
    mock_exit_super_calls++;
}

/* ------------------------------------------------------------------ */
/* Code under test                                                    */
/* ------------------------------------------------------------------ */

#include "../old_add_entry.c"

static uid_t dir_uid  = { 0x11111111u, 0x22222222u };
static uid_t file_uid = { 0x33333333u, 0xABCDEF12u };

static void reset(void)
{
    mock_validate_result = -1;          /* Domain boolean: valid */
    mock_validate_calls = 0;
    mock_parsed_len = 3;

    mock_lock_calls = 0;
    mock_lock_mode = 0;
    mock_lock_rights = 0;
    mock_lock_status = status_$ok;
    mock_lock_handle = 0xDEADBEEFu;

    mock_add_calls = 0;
    mock_add_status = status_$ok;

    mock_addi_calls = 0;
    mock_addi_uid = NULL;
    mock_addi_ptr = NULL;
    memset(&mock_addi_record, 0, sizeof(mock_addi_record));

    mock_unlock_calls = 0;
    mock_unlock_status = status_$ok;
    mock_exit_super_calls = 0;

    dir_uid  = (uid_t){ 0x11111111u, 0x22222222u };
    file_uid = (uid_t){ 0x33333333u, 0xABCDEF12u };
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

/* The record is exactly 8 bytes, {flags, node_id}. */
TEST(hint_record_layout)
{
    ASSERT_EQ(8, sizeof(hint_addr_t));
    ASSERT_EQ(0, __builtin_offsetof(hint_addr_t, flags));
    ASSERT_EQ(4, __builtin_offsetof(hint_addr_t, node_id));
}

/* 0x00E5670A / 0x00E5670E-0x00E56718: the two longwords of the record. */
TEST(hint_record_contents)
{
    status_$t status = 0;

    reset();
    name_$old_add_entry(&dir_uid, 2, "abc", 3, &file_uid, 0x5A5A5A5Au, &status);

    ASSERT_EQ(1, mock_addi_calls);
    ASSERT_EQ(0x5A5A5A5Au, mock_addi_record.flags);      /* the caller's word */
    ASSERT_EQ(0xDEF12u, mock_addi_record.node_id);       /* 0xABCDEF12 & 0xFFFFF */
    /* 0x00E56720 `pea (A4)`: the file UID, by address */
    ASSERT_EQ((uintptr_t)&file_uid, (uintptr_t)mock_addi_uid);
    ASSERT_EQ(status_$ok, status);
}

/* The mask really is 20 bits, not 24 or 16. */
TEST(node_id_is_the_low_twenty_bits)
{
    status_$t status = 0;

    reset();
    file_uid.low = 0xFFFFFFFFu;
    name_$old_add_entry(&dir_uid, 2, "abc", 3, &file_uid, 0, &status);
    ASSERT_EQ(0x000FFFFFu, mock_addi_record.node_id);

    reset();
    file_uid.low = 0x00100000u;
    name_$old_add_entry(&dir_uid, 2, "abc", 3, &file_uid, 0, &status);
    ASSERT_EQ(0u, mock_addi_record.node_id);
}

/* 0x00E566C8: lock_mode 4 and the caller's type as acl_rights. */
TEST(lock_dir_arguments)
{
    status_$t status = 0;

    reset();
    name_$old_add_entry(&dir_uid, 7, "abc", 3, &file_uid, 0, &status);

    ASSERT_EQ(1, mock_lock_calls);
    ASSERT_EQ(4, mock_lock_mode);
    ASSERT_EQ(7, mock_lock_rights);
    /* 0x00E566F8: the handle NAME_$LOCK_DIR returned is passed straight on */
    ASSERT_EQ(0xDEADBEEFu, mock_add_handle);
    ASSERT_EQ(1, mock_add_type);        /* 0x00E566EC move.w #0x1 */
    ASSERT_EQ(0xFF, mock_add_replace);  /* 0x00E566E6 st -(SP) */
}

/* 0x00E566B4-0x00E566BE: an invalid leaf name short-circuits everything. */
TEST(invalid_leaf_name)
{
    status_$t status = 0;

    reset();
    mock_validate_result = 0;
    name_$old_add_entry(&dir_uid, 2, "abc", 3, &file_uid, 0, &status);

    ASSERT_EQ(0x000E000B, status);
    ASSERT_EQ(0, mock_lock_calls);
    ASSERT_EQ(0, mock_addi_calls);
    ASSERT_EQ(0, mock_exit_super_calls);
}

/* 0x00E56706-0x00E56708: a failed add skips the hint update but still
 * unlocks and exits super mode. */
TEST(add_failure_skips_the_hint)
{
    status_$t status = 0;

    reset();
    mock_add_status = 0x000E0002;
    name_$old_add_entry(&dir_uid, 2, "abc", 3, &file_uid, 0, &status);

    ASSERT_EQ(0, mock_addi_calls);
    ASSERT_EQ(1, mock_unlock_calls);
    ASSERT_EQ(1, mock_exit_super_calls);
    ASSERT_EQ(0x000E0002, status);
}

/* 0x00E566DA-0x00E566DC: a lock failure skips the add AND the unlock. */
TEST(lock_failure_skips_the_add)
{
    status_$t status = 0;

    reset();
    mock_lock_status = 0x000E0005;
    name_$old_add_entry(&dir_uid, 2, "abc", 3, &file_uid, 0, &status);

    ASSERT_EQ(0, mock_add_calls);
    ASSERT_EQ(0, mock_unlock_calls);
    ASSERT_EQ(1, mock_exit_super_calls);
    ASSERT_EQ(0x000E0005, status);
}

/* 0x00E56734-0x00E5673A: a non-zero unlock status overwrites the caller's. */
TEST(unlock_status_overrides)
{
    status_$t status = 0;

    reset();
    mock_unlock_status = 0x000E0007;
    name_$old_add_entry(&dir_uid, 2, "abc", 3, &file_uid, 0, &status);

    ASSERT_EQ(0x000E0007, status);
    ASSERT_EQ(1, mock_addi_calls);      /* the hint went in first */
}

int main(void)
{
    printf("name_$old_add_entry tests\n");
    RUN_TEST(hint_record_layout);
    RUN_TEST(hint_record_contents);
    RUN_TEST(node_id_is_the_low_twenty_bits);
    RUN_TEST(lock_dir_arguments);
    RUN_TEST(invalid_leaf_name);
    RUN_TEST(add_failure_skips_the_hint);
    RUN_TEST(lock_failure_skips_the_add);
    RUN_TEST(unlock_status_overrides);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
