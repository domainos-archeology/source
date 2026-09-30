/*
 * dir/test/test_old_find_uid.c - DIR_$OLD_FIND_UID (0x00E558D6) and its
 * slot search dir_$old_find_uid_slot (0x00E557D4)
 *
 * Pins: lock failure goes straight to ACL_$EXIT_SUPER; an inline or block
 * slot match (type 1, same UID) is unmapped with the 0x20 cell and the
 * unlock status returned; a miss in the root directory asks
 * REM_NAME_$FIND_UID and caches the answer; a miss elsewhere stays
 * name-not-found; unused blocks (+0x36F != 1) are skipped.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "dir/dir_internal.h"

name_$data_t NAME_$DATA;
const int16_t name_$leaf_max_len_00e544ae = 0x0020;

/* A fake old-format directory: header words, inline slots, overflow
 * blocks.  0x4000 bytes covers inline slots and several 0x96 blocks. */
static uint8_t dirbuf[0x4000] __attribute__((aligned(4)));
static status_$t lock_status, unlock_status_to_give, rem_status;
static int16_t lock_mode_seen, lock_rights_seen;
static int nexit, nunlock, nrem, nadd;
static uint16_t add_len_seen;
static uint32_t add_extra_seen;
static uid_t add_uid_seen;
static int16_t unmap_max_seen;

void NAME_$LOCK_DIR(uid_t *dir_uid, uint32_t *handle_ret, int16_t lock_mode,
                    int16_t acl_rights, status_$t *status_ret)
{
    (void)dir_uid;
    lock_mode_seen = lock_mode;
    lock_rights_seen = acl_rights;
    *handle_ret = ARCH_PTR_TO_VA(dirbuf);
    *status_ret = lock_status;
}
void NAME_$UNLOCK_DIR(status_$t *st) { nunlock++; *st = unlock_status_to_give; }
void ACL_$EXIT_SUPER(void) { nexit++; }
void UNMAP_CASE(char *name, int16_t *name_len, char *output,
                int16_t *max_out_len, int16_t *out_len, uint8_t *truncated)
{
    int i;
    unmap_max_seen = *max_out_len;
    for (i = 0; i < *name_len; i++) output[i] = (char)(name[i] | 0x20);
    *out_len = *name_len;
    *truncated = 0;
}
void name_$old_add_entry(uid_t *dir_uid, uint16_t type, char *name,
                         uint16_t name_len, uid_t *file_uid,
                         uint32_t flags, status_$t *status_ret)
{
    (void)dir_uid; (void)type; (void)name;
    nadd++;
    add_len_seen = name_len;
    add_uid_seen = *file_uid;
    add_extra_seen = flags;
    *status_ret = 0;
}

static dir_$old_slot_t *inline_slot(int i)
{
    return (dir_$old_slot_t *)(dirbuf + 0x30 * i - 0x16);
}
static dir_$old_slot_t *block_slot(int k, int j)
{
    return (dir_$old_slot_t *)(dirbuf + 0x96 * k + 0x30 * j + 0x340);
}
static void hdr(uint16_t off, uint16_t v) { *(uint16_t *)(dirbuf + off) = v; }

static void base_setup(void)
{
    memset(dirbuf, 0, sizeof(dirbuf));
    memset(&NAME_$DATA, 0, sizeof(NAME_$DATA));
    NAME_$DATA.root_uid.high = 0x1234;
    NAME_$DATA.root_uid.low = 0x5678;
    ARCH_HOST_VA_BASE = (uintptr_t)dirbuf - 0x100;
    lock_status = 0;
    unlock_status_to_give = 0;
    rem_status = 0;
    nexit = nunlock = nrem = nadd = 0;
    hdr(0x04, 3);   /* inline slots */
    hdr(0x08, 2);   /* slots per block */
    hdr(0x0A, 2);   /* blocks */
}

void REM_NAME_$FIND_UID(uid_t *dir_uid, uid_t *target_uid,
                        void *entry_ret, status_$t *status_ret)
{
    dir_$rep_entry_t *r = (dir_$rep_entry_t *)entry_ret;
    (void)dir_uid; (void)target_uid;
    nrem++;
    memset(r, 0, sizeof(*r));
    memcpy(r->name, "REMOTE", 6);
    r->name_len = 6;
    r->uid.high = 0xAB;
    r->uid.low = 0xCD;
    r->extra = 0x77;
    *status_ret = rem_status;
}

#include "../old_find_uid.c"

static uid_t root = { 0x1234, 0x5678 };
static uid_t other = { 0x1, 0x2 };
static char name[0x20];
static int16_t len;
static status_$t st;

TEST(lock_fails)
{
    uid_t t = { 9, 9 };
    base_setup();
    lock_status = 0x1234;
    DIR_$OLD_FIND_UID(&other, &t, name, &len, &st);
    ASSERT_EQ(0x1234, st);
    ASSERT_EQ(0, nunlock);
    ASSERT_EQ(1, nexit);
    ASSERT_EQ(1, lock_mode_seen);
    ASSERT_EQ(0, lock_rights_seen);
}

TEST(inline_match)
{
    uid_t t = { 9, 9 };
    base_setup();
    inline_slot(2)->type = 1;
    inline_slot(2)->uid = t;
    memcpy(inline_slot(2)->name, "ABC", 3);
    inline_slot(2)->name_len = 3;
    unlock_status_to_give = 0x55;
    DIR_$OLD_FIND_UID(&other, &t, name, &len, &st);
    ASSERT_EQ(0x55, st);
    ASSERT_EQ(3, len);
    ASSERT_EQ('a', name[0]);
    ASSERT_EQ(0x20, unmap_max_seen);
    ASSERT_EQ(1, nexit);
}

TEST(block_match_and_unused_block_skipped)
{
    uid_t t = { 7, 8 };
    base_setup();
    /* block 1 not in use: its slot matches but must be skipped */
    block_slot(1, 1)->type = 1;
    block_slot(1, 1)->uid = t;
    memcpy(block_slot(1, 1)->name, "NO", 2);
    block_slot(1, 1)->name_len = 2;
    dirbuf[0x96 * 2 + 0x36F] = 1;
    block_slot(2, 2)->type = 1;
    block_slot(2, 2)->uid = t;
    memcpy(block_slot(2, 2)->name, "YES", 3);
    block_slot(2, 2)->name_len = 3;
    DIR_$OLD_FIND_UID(&other, &t, name, &len, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(3, len);
    ASSERT_EQ('y', name[0]);
}

TEST(miss_not_root)
{
    uid_t t = { 7, 8 };
    base_setup();
    DIR_$OLD_FIND_UID(&other, &t, name, &len, &st);
    ASSERT_EQ(status_$naming_name_not_found, st);
    ASSERT_EQ(0, nrem);
    ASSERT_EQ(1, nexit);
}

TEST(miss_root_asks_remote_and_caches)
{
    uid_t t = { 7, 8 };
    base_setup();
    DIR_$OLD_FIND_UID(&root, &t, name, &len, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, nrem);
    ASSERT_EQ(1, nadd);
    ASSERT_EQ(6, len);
    ASSERT_EQ('r', name[0]);
    ASSERT_EQ(6, add_len_seen);
    ASSERT_EQ(0xAB, add_uid_seen.high);
    ASSERT_EQ(0x77, add_extra_seen);
}

TEST(miss_root_remote_fails)
{
    uid_t t = { 7, 8 };
    base_setup();
    rem_status = 0xE0007;
    DIR_$OLD_FIND_UID(&root, &t, name, &len, &st);
    ASSERT_EQ(0xE0007, st);
    ASSERT_EQ(0, nadd);
    ASSERT_EQ(1, nexit);
}

int main(void)
{
    printf("DIR_$OLD_FIND_UID tests:\n");
    RUN_TEST(lock_fails);
    RUN_TEST(inline_match);
    RUN_TEST(block_match_and_unused_block_skipped);
    RUN_TEST(miss_not_root);
    RUN_TEST(miss_root_asks_remote_and_caches);
    RUN_TEST(miss_root_remote_fails);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
