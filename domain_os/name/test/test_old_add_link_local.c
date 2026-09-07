/*
 * name/test/test_old_add_link_local.c - unit tests for
 * name_$old_add_link_local (0x00E565B8)
 *
 * The real name/old_add_link_local.c is #included at the bottom; everything it
 * calls is mocked here.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  %-50s ", #name);                  \
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
#include "acl/acl.h"

/* ------------------------------------------------------------------ */
/* Globals                                                              */
/* ------------------------------------------------------------------ */

name_$data_t NAME_$DATA;                /* carries NAME_$ROOT_UID at +0x38 */

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static int      m_add_entry_calls;
static uid_t    m_add_entry_dir;
static uint16_t m_add_entry_type;
static uint16_t m_add_entry_len;
static uint32_t m_add_entry_flags;
static status_$t m_add_entry_status;

void name_$old_add_entry(uid_t *dir_uid, uint16_t type, char *name,
                         uint16_t name_len, uid_t *file_uid,
                         uint32_t flags, status_$t *status_ret)
{
    (void)name; (void)file_uid;
    m_add_entry_calls++;
    m_add_entry_dir   = *dir_uid;
    m_add_entry_type  = type;
    m_add_entry_len   = name_len;
    m_add_entry_flags = flags;
    *status_ret = m_add_entry_status;
}

static int      m_validate_calls;
static int8_t   m_validate_result;
static uint16_t m_validate_len;
static char     m_validate_leaf[0x20];

int8_t name_$validate_leaf(char *name, uint16_t name_len,
                           uint8_t *parsed, uint16_t *parsed_len)
{
    (void)name; (void)name_len;
    m_validate_calls++;
    memcpy(parsed, m_validate_leaf, sizeof(m_validate_leaf));
    *parsed_len = m_validate_len;
    return m_validate_result;
}

static int       m_lock_calls;
static int16_t   m_lock_mode;
static int16_t   m_lock_rights;
static uint32_t  m_lock_handle;
static status_$t m_lock_status;

void NAME_$LOCK_DIR(uid_t *dir_uid, uint32_t *handle_ret,
                    int16_t lock_mode, int16_t acl_rights,
                    status_$t *status_ret)
{
    (void)dir_uid;
    m_lock_calls++;
    m_lock_mode = lock_mode;
    m_lock_rights = acl_rights;
    *handle_ret = m_lock_handle;
    *status_ret = m_lock_status;
}

static int       m_dir_add_calls;
static uint32_t  m_dir_add_handle;
static uint16_t  m_dir_add_type;
static uint16_t  m_dir_add_len;
static uint16_t  m_dir_add_flags;
static char      m_dir_add_name[0x20];
static status_$t m_dir_add_status;

void dir_$old_add_entry(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                        uint16_t name_len, uint16_t type, void *uid_data,
                        uint16_t flags, uint8_t *result, status_$t *status_ret)
{
    (void)dir_uid; (void)uid_data;
    m_dir_add_calls++;
    m_dir_add_handle = handle;
    m_dir_add_type   = type;
    m_dir_add_len    = name_len;
    m_dir_add_flags  = flags;
    memcpy(m_dir_add_name, name, sizeof(m_dir_add_name));
    *(uint32_t *)(void *)result = 0xABCD1234u;
    *status_ret = m_dir_add_status;
}

static int       m_unlock_calls;
static status_$t m_unlock_status;

void NAME_$UNLOCK_DIR(status_$t *status_ret)
{
    m_unlock_calls++;
    *status_ret = m_unlock_status;
}

static int m_exit_super_calls;

void ACL_$EXIT_SUPER(void)
{
    m_exit_super_calls++;
}

/* ------------------------------------------------------------------ */
/* Fixtures                                                             */
/* ------------------------------------------------------------------ */

static const uid_t ROOT_UID = { 0x00000001u, 0x00000002u };
static const uid_t DIR_UID  = { 0x11112222u, 0x33334444u };
static uid_t       file_uid = { 0x55556666u, 0x77778888u };
static char        the_name[] = "hello";
static status_$t   status;

static void reset_world(void)
{
    memset(&NAME_$DATA, 0, sizeof(NAME_$DATA));
    NAME_$ROOT_UID = ROOT_UID;

    m_add_entry_calls = 0;   m_add_entry_status = 0;
    m_validate_calls = 0;    m_validate_result = (int8_t)0xFF; /* usable leaf */
    m_validate_len = 5;
    memset(m_validate_leaf, 0, sizeof(m_validate_leaf));
    memcpy(m_validate_leaf, "HELLO", 5);
    m_lock_calls = 0;        m_lock_status = 0;   m_lock_handle = 0xDEADBEEFu;
    m_dir_add_calls = 0;     m_dir_add_status = 0;
    m_unlock_calls = 0;      m_unlock_status = 0;
    m_exit_super_calls = 0;
    status = 0x7F7F7F7F;
}

static void run(uid_t dir, int16_t acl_rights)
{
    name_$old_add_link_local(&dir, acl_rights, the_name, 5, &file_uid, &status);
}

/* ------------------------------------------------------------------ */

TEST(root_directory_is_forwarded_to_name_old_add_entry)
{
    reset_world();
    m_add_entry_status = 0x00112233u;
    run(ROOT_UID, 6);
    ASSERT_EQ(1, m_add_entry_calls);
    ASSERT_EQ(6, m_add_entry_type);             /* the A6+0x0C word */
    ASSERT_EQ(5, m_add_entry_len);
    ASSERT_EQ(0, m_add_entry_flags);            /* `clr.l -(SP)` at 0x00E565E4 */
    ASSERT_EQ(0x00112233u, status);
    /* `bra.b 0x00e56678` at 0x00E565F8 SKIPS the ACL_$EXIT_SUPER. */
    ASSERT_EQ(0, m_exit_super_calls);
    ASSERT_EQ(0, m_validate_calls);
    ASSERT_EQ(0, m_lock_calls);
}

TEST(root_uid_compare_needs_both_halves)
{
    reset_world();
    uid_t almost = ROOT_UID;
    almost.low ^= 1u;
    run(almost, 0);
    ASSERT_EQ(0, m_add_entry_calls);
    ASSERT_EQ(1, m_validate_calls);
}

TEST(invalid_leaf_reports_naming_invalid_leaf)
{
    reset_world();
    m_validate_result = 0;                      /* >= 0 -> not a leaf */
    run(DIR_UID, 0);
    ASSERT_EQ(0x000E000Bu, status);             /* 0x00E56616 */
    ASSERT_EQ(0, m_lock_calls);
    /* `bra.b 0x00e56678` at 0x00E5661C also skips ACL_$EXIT_SUPER. */
    ASSERT_EQ(0, m_exit_super_calls);
}

TEST(lock_dir_gets_mode_4_and_the_caller_rights_word)
{
    reset_world();
    run(DIR_UID, 2);
    ASSERT_EQ(1, m_lock_calls);
    ASSERT_EQ(4, m_lock_mode);                  /* 0x00E56624 */
    ASSERT_EQ(2, m_lock_rights);                /* 0x00E56620 */
}

TEST(lock_failure_still_exits_super)
{
    reset_world();
    m_lock_status = 0x000E0006u;
    run(DIR_UID, 0);
    ASSERT_EQ(0x000E0006u, status);
    ASSERT_EQ(0, m_dir_add_calls);              /* `bne.b 0x00e56672` */
    ASSERT_EQ(0, m_unlock_calls);
    ASSERT_EQ(1, m_exit_super_calls);
}

TEST(successful_add_passes_the_parsed_leaf_and_handle)
{
    reset_world();
    run(DIR_UID, 0);
    ASSERT_EQ(1, m_dir_add_calls);
    ASSERT_EQ(0xDEADBEEFu, m_dir_add_handle);   /* NAME_$LOCK_DIR's handle */
    ASSERT_EQ(1, m_dir_add_type);               /* `move.w #0x1` 0x00E56646 */
    ASSERT_EQ(5, m_dir_add_len);                /* the PARSED length */
    ASSERT_EQ(0, m_dir_add_flags);              /* `clr.w -(SP)` 0x00E56642 */
    ASSERT_EQ(0, memcmp(m_dir_add_name, "HELLO", 5));
    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, m_unlock_calls);
    ASSERT_EQ(1, m_exit_super_calls);
}

TEST(unlock_status_overwrites_a_successful_add)
{
    reset_world();
    m_unlock_status = 0x000F0005u;
    run(DIR_UID, 0);
    ASSERT_EQ(0x000F0005u, status);             /* 0x00E56670 */
}

TEST(a_clean_unlock_does_not_mask_an_add_failure)
{
    reset_world();
    m_dir_add_status = 0x000E0002u;
    m_unlock_status  = 0;
    run(DIR_UID, 0);
    /* 0x00E5666E `beq.b` leaves the caller's status alone. */
    ASSERT_EQ(0x000E0002u, status);
    ASSERT_EQ(1, m_exit_super_calls);
}

TEST(unlock_status_also_overwrites_an_add_failure)
{
    reset_world();
    m_dir_add_status = 0x000E0002u;
    m_unlock_status  = 0x000F0005u;
    run(DIR_UID, 0);
    ASSERT_EQ(0x000F0005u, status);
}

int main(void)
{
    printf("name_$old_add_link_local (0x00E565B8)\n");

    RUN_TEST(root_directory_is_forwarded_to_name_old_add_entry);
    RUN_TEST(root_uid_compare_needs_both_halves);
    RUN_TEST(invalid_leaf_reports_naming_invalid_leaf);
    RUN_TEST(lock_dir_gets_mode_4_and_the_caller_rights_word);
    RUN_TEST(lock_failure_still_exits_super);
    RUN_TEST(successful_add_passes_the_parsed_leaf_and_handle);
    RUN_TEST(unlock_status_overwrites_a_successful_add);
    RUN_TEST(a_clean_unlock_does_not_mask_an_add_failure);
    RUN_TEST(unlock_status_also_overwrites_an_add_failure);

    printf("\n%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed != 0;
}

#include "../old_add_link_local.c"
