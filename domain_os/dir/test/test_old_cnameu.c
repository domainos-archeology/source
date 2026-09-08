/*
 * dir/test/test_old_cnameu.c - unit tests for DIR_$OLD_CNAMEU (0x00E57562)
 *
 * Bead source-rv9v.  The behaviours pinned here are: the NEW name is
 * validated first (0x00E57578) and the OLD name second (0x00E57598); the
 * root-directory arm brackets dir_$old_add_entry_ext with
 * `bset.b #0x7,(0x24,A2)` / `bclr.b #0x7,(0x24,A2)` (0x00E57654 /
 * 0x00E57688) and passes the entry's own longword at +0x20 as `extra`; on
 * success the OLD slot's type byte becomes 1 (0x00E57694) and the hash is
 * taken over the OLD name with the bucket count from the directory header
 * (0x00E5769A); and the unlock tail keeps a nonzero status (0x00E576D0).
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

#define ASSERT_TRUE(cond) do {                                           \
    if (!(cond)) {                                                       \
        printf("FAILED\n    %s at line %d\n", #cond, __LINE__);          \
        current_failed = 1;                                              \
        return;                                                          \
    }                                                                    \
} while (0)

#include "dir/dir_internal.h"

name_$data_t NAME_$DATA;

/* ------------------------------------------------------------------ */
/* The directory the handle points at, and the entry find_entry returns  */
/* ------------------------------------------------------------------ */

static uint8_t dir_buf[0x1000];
static uint8_t entry_rec[0x40];

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static int    vl_calls;
static int8_t vl_result[2];             /* [0] = new name, [1] = old name */
static char  *vl_name_seen[2];
int8_t name_$validate_leaf(char *name, uint16_t name_len,
                           uint8_t *parsed, uint16_t *parsed_len)
{
    int8_t r = vl_result[vl_calls < 2 ? vl_calls : 1];
    if (vl_calls < 2) {
        vl_name_seen[vl_calls] = name;
    }
    vl_calls++;
    (void)name_len;
    parsed[0] = (uint8_t)name[0];
    *parsed_len = (uint16_t)(vl_calls == 1 ? 7 : 9);   /* new: 7, old: 9 */
    return r;
}

/*
 * A directory handle is a 32-bit target virtual address, so a host pointer
 * does not survive the round trip; name/handle_map.c keeps a registry for
 * that and the test supplies its own one-entry version.
 */
#define TEST_HANDLE 0x00100000u
void *name_$handle_to_ptr(uint32_t handle)
{
    return handle == TEST_HANDLE ? (void *)dir_buf : (void *)0;
}
uint32_t name_$ptr_to_handle(const void *ptr)
{
    return ptr == (const void *)dir_buf ? TEST_HANDLE : 0u;
}

static int       lock_calls;
static status_$t lock_status;
void NAME_$LOCK_DIR(uid_t *dir_uid, uint32_t *handle_ret,
                    int16_t lock_mode, int16_t acl_rights,
                    status_$t *status_ret)
{
    lock_calls++;
    (void)dir_uid; (void)lock_mode; (void)acl_rights;
    *handle_ret = TEST_HANDLE;
    *status_ret = lock_status;
}

static int       unlock_calls;
static status_$t unlock_status_out;
void NAME_$UNLOCK_DIR(status_$t *status_ret)
{
    unlock_calls++;
    *status_ret = unlock_status_out;
}

static int exit_super_calls;
void ACL_$EXIT_SUPER(void) { exit_super_calls++; }

static int8_t   fe_result;
static uint16_t fe_slot;
static uint16_t fe_chain;
#define ENTRY_VA 0x00200000
int8_t dir_$old_find_entry(uint32_t handle, uint8_t *name, uint16_t name_len,
                           int32_t *entry_ret, uint16_t *slot_idx,
                           uint16_t *chain_level)
{
    (void)handle; (void)name; (void)name_len;
    *entry_ret = ENTRY_VA;
    *slot_idx = fe_slot;
    *chain_level = fe_chain;
    return fe_result;
}

static int       ae_calls;
static uint16_t  ae_type_seen;
static uint16_t  ae_len_seen;
static void     *ae_uid_seen;
static uint8_t   ae_bit_during;
static status_$t ae_status;
void dir_$old_add_entry(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                        uint16_t name_len, uint16_t type, void *uid_data,
                        uint16_t flags, uint8_t *result, status_$t *status_ret)
{
    ae_calls++;
    ae_type_seen = type;
    ae_len_seen = name_len;
    ae_uid_seen = uid_data;
    ae_bit_during = (uint8_t)(entry_rec[0x24] & 0x80);
    (void)dir_uid; (void)handle; (void)name; (void)flags;
    *(uint32_t *)result = 0;
    *status_ret = ae_status;
}

static int       aee_calls;
static uint16_t  aee_type_seen;
static uint16_t  aee_len_seen;
static void     *aee_uid_seen;
static uint32_t  aee_extra_seen;
static uint8_t   aee_replace_seen;
static uint8_t   aee_bit_during;
static status_$t aee_status;
void dir_$old_add_entry_ext(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                            uint16_t name_len, uint16_t type, void *uid_data,
                            uint32_t extra, uint8_t replace_flag,
                            uint8_t *result, status_$t *status_ret)
{
    aee_calls++;
    aee_type_seen = type;
    aee_len_seen = name_len;
    aee_uid_seen = uid_data;
    aee_extra_seen = extra;
    aee_replace_seen = replace_flag;
    aee_bit_during = (uint8_t)(entry_rec[0x24] & 0x80);
    (void)dir_uid; (void)handle; (void)name;
    *(uint32_t *)result = 0;
    *status_ret = aee_status;
}

static int      hash_calls;
static uint8_t  hash_first_char;
static uint16_t hash_len_seen;
static uint16_t hash_buckets_seen;
uint16_t dir_$old_hash_name(uint8_t *name, uint16_t name_len,
                            uint16_t num_buckets)
{
    hash_calls++;
    hash_first_char = name[0];
    hash_len_seen = name_len;
    hash_buckets_seen = num_buckets;
    return 0x0037;
}

static int      del_calls;
static uint16_t del_slot_seen;
static uint16_t del_chain_seen;
static uint16_t del_hash_seen;
void dir_$old_delete_entry(uint32_t handle, uint16_t slot_idx,
                           uint16_t chain_level, uint16_t hash)
{
    del_calls++;
    del_slot_seen = slot_idx;
    del_chain_seen = chain_level;
    del_hash_seen = hash;
    (void)handle;
}

#include "../old_cnameu.c"

/* ------------------------------------------------------------------ */

static uid_t     dir;
static status_$t st;
static uint16_t  old_len, new_len;
static char      old_name[] = "oldname";
static char      new_name[] = "newname";

static const uid_t ROOT  = { 0x0BADF00Du, 0x0BADBEEFu };
static const uid_t OTHER = { 0x11111111u, 0x22222222u };

static void reset(void)
{
    memset(&NAME_$DATA, 0, sizeof(NAME_$DATA));
    NAME_$ROOT_UID = ROOT;
    dir = OTHER;
    memset(dir_buf, 0, sizeof(dir_buf));
    *(uint16_t *)(dir_buf + 2) = 0x0101;     /* the bucket count word */
    /*
     * dir_$old_find_entry returns a 32-bit target address, so the test points
     * ARCH_HOST_VA_BASE at its own arena (arch/host/arch.h).
     */
    ARCH_HOST_VA_BASE = (uintptr_t)entry_rec - ENTRY_VA;
    memset(entry_rec, 0, sizeof(entry_rec));
    entry_rec[0x27] = 3;                     /* the entry's type byte */
    *(uint32_t *)(entry_rec + 0x20) = 0xFEEDFACEu;
    vl_calls = 0;
    vl_result[0] = (int8_t)0xFF;
    vl_result[1] = (int8_t)0xFF;
    vl_name_seen[0] = NULL; vl_name_seen[1] = NULL;
    lock_calls = 0; lock_status = status_$ok;
    unlock_calls = 0; unlock_status_out = status_$ok;
    exit_super_calls = 0;
    fe_result = (int8_t)0xFF; fe_slot = 6; fe_chain = 2;
    ae_calls = 0; ae_status = status_$ok; ae_bit_during = 0xAA;
    ae_type_seen = 0xFFFF; ae_len_seen = 0xFFFF; ae_uid_seen = NULL;
    aee_calls = 0; aee_status = status_$ok; aee_bit_during = 0xAA;
    aee_type_seen = 0xFFFF; aee_len_seen = 0xFFFF; aee_uid_seen = NULL;
    aee_extra_seen = 0; aee_replace_seen = 0;
    hash_calls = 0; hash_first_char = 0; hash_len_seen = 0;
    hash_buckets_seen = 0;
    del_calls = 0; del_slot_seen = 0; del_chain_seen = 0; del_hash_seen = 0;
    st = 0x5A5A5A5A;
    old_len = 7; new_len = 7;
}

/* 0x00E57578 then 0x00E57598: the NEW name is validated first. */
TEST(the_new_name_is_validated_first)
{
    reset();
    DIR_$OLD_CNAMEU(&dir, old_name, &old_len, new_name, &new_len, &st);
    ASSERT_EQ(2, vl_calls);
    ASSERT_TRUE(vl_name_seen[0] == new_name);
    ASSERT_TRUE(vl_name_seen[1] == old_name);
}

/* A bad NEW name stops before the old one is even looked at. */
TEST(a_bad_new_name_short_circuits)
{
    reset();
    vl_result[0] = 0;
    DIR_$OLD_CNAMEU(&dir, old_name, &old_len, new_name, &new_len, &st);
    ASSERT_EQ(1, vl_calls);
    ASSERT_EQ(status_$naming_invalid_leaf, st);
    ASSERT_EQ(0, lock_calls);
    ASSERT_EQ(0, exit_super_calls);
}

TEST(a_bad_old_name_is_also_an_invalid_leaf)
{
    reset();
    vl_result[1] = 0;
    DIR_$OLD_CNAMEU(&dir, old_name, &old_len, new_name, &new_len, &st);
    ASSERT_EQ(2, vl_calls);
    ASSERT_EQ(status_$naming_invalid_leaf, st);
    ASSERT_EQ(0, lock_calls);
}

/* 0x00E5760C: the entry has to exist, and the unlock still happens. */
TEST(a_missing_old_entry_is_name_not_found_and_still_unlocks)
{
    reset();
    fe_result = 0;
    DIR_$OLD_CNAMEU(&dir, old_name, &old_len, new_name, &new_len, &st);
    ASSERT_EQ(0, ae_calls);
    ASSERT_EQ(0, aee_calls);
    ASSERT_EQ(1, unlock_calls);
    ASSERT_EQ(status_$naming_name_not_found, st);
    ASSERT_EQ(1, exit_super_calls);
}

/* 0x00E57628: a non-root directory takes the plain add, flags word 0. */
TEST(a_non_root_directory_uses_the_plain_add)
{
    reset();
    DIR_$OLD_CNAMEU(&dir, old_name, &old_len, new_name, &new_len, &st);
    ASSERT_EQ(1, ae_calls);
    ASSERT_EQ(0, aee_calls);
    ASSERT_EQ(3, ae_type_seen);              /* the entry's type byte */
    ASSERT_EQ(7, ae_len_seen);               /* the NEW parsed length */
    ASSERT_TRUE(ae_uid_seen == (void *)(entry_rec + 0x28));
    ASSERT_EQ(0, ae_bit_during);             /* bit 7 is NOT set here */
}

/*
 * 0x00E57654-0x00E57688: the root arm sets bit 7 of the entry's byte at
 * +0x24 across the call and clears it afterwards, and hands over the entry's
 * own longword at +0x20 plus the Domain TRUE replace flag.
 */
TEST(the_root_arm_brackets_the_add_with_bit_7)
{
    reset();
    dir = ROOT;
    DIR_$OLD_CNAMEU(&dir, old_name, &old_len, new_name, &new_len, &st);
    ASSERT_EQ(1, aee_calls);
    ASSERT_EQ(0, ae_calls);
    ASSERT_EQ(0x80, aee_bit_during);
    ASSERT_EQ(0, entry_rec[0x24] & 0x80);    /* cleared again */
    ASSERT_EQ(0xFEEDFACEu, aee_extra_seen);
    ASSERT_EQ(0xFF, aee_replace_seen);
    ASSERT_EQ(3, aee_type_seen);
    ASSERT_EQ(7, aee_len_seen);
    ASSERT_TRUE(aee_uid_seen == (void *)(entry_rec + 0x28));
}

/*
 * 0x00E57694-0x00E576C2: on success the OLD slot is stamped type 1 and freed
 * using a hash of the OLD name with the directory's own bucket count.
 */
TEST(a_successful_add_frees_the_old_slot_by_its_own_hash)
{
    reset();
    DIR_$OLD_CNAMEU(&dir, old_name, &old_len, new_name, &new_len, &st);
    ASSERT_EQ(1, entry_rec[0x27]);
    ASSERT_EQ(1, hash_calls);
    ASSERT_EQ('o', hash_first_char);         /* the OLD parsed name */
    ASSERT_EQ(9, hash_len_seen);             /* the OLD parsed length */
    ASSERT_EQ(0x0101, hash_buckets_seen);    /* the word at handle+2 */
    ASSERT_EQ(1, del_calls);
    ASSERT_EQ(6, del_slot_seen);
    ASSERT_EQ(2, del_chain_seen);
    ASSERT_EQ(0x0037, del_hash_seen);
    ASSERT_EQ(status_$ok, st);
}

/* 0x00E5768E: a failed add leaves the old slot alone. */
TEST(a_failed_add_leaves_the_old_slot_alone)
{
    reset();
    ae_status = status_$name_already_exists;
    DIR_$OLD_CNAMEU(&dir, old_name, &old_len, new_name, &new_len, &st);
    ASSERT_EQ(3, entry_rec[0x27]);
    ASSERT_EQ(0, hash_calls);
    ASSERT_EQ(0, del_calls);
    ASSERT_EQ(status_$name_already_exists, st);
}

/* 0x00E576D0: the unlock status only fills in a zero status. */
TEST(the_unlock_status_only_fills_in_a_zero_status)
{
    reset();
    unlock_status_out = status_$naming_internal_error;
    DIR_$OLD_CNAMEU(&dir, old_name, &old_len, new_name, &new_len, &st);
    ASSERT_EQ(status_$naming_internal_error, st);

    reset();
    fe_result = 0;                           /* leaves 0xE0007 behind */
    unlock_status_out = status_$naming_internal_error;
    DIR_$OLD_CNAMEU(&dir, old_name, &old_len, new_name, &new_len, &st);
    ASSERT_EQ(status_$naming_name_not_found, st);
}

int main(void)
{
    printf("DIR_$OLD_CNAMEU (0x00E57562) tests\n");

    RUN_TEST(the_new_name_is_validated_first);
    RUN_TEST(a_bad_new_name_short_circuits);
    RUN_TEST(a_bad_old_name_is_also_an_invalid_leaf);
    RUN_TEST(a_missing_old_entry_is_name_not_found_and_still_unlocks);
    RUN_TEST(a_non_root_directory_uses_the_plain_add);
    RUN_TEST(the_root_arm_brackets_the_add_with_bit_7);
    RUN_TEST(a_successful_add_frees_the_old_slot_by_its_own_hash);
    RUN_TEST(a_failed_add_leaves_the_old_slot_alone);
    RUN_TEST(the_unlock_status_only_fills_in_a_zero_status);

    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
