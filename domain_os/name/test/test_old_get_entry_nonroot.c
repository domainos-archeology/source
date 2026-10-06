/*
 * name/test/test_old_get_entry_nonroot.c - name_$old_get_entry_nonroot
 * (0x00E57CE0)
 *
 * Pins: an invalid leaf fails; hints are tried in order, a hint on this
 * node (or a locked remote directory) switches to the local lookup; a
 * remote success promotes a non-first hint and hints a type-1 object on
 * another node; final remote errors end the call, others try the next
 * hint, and running out crashes with 0x000E0007; the local lookup fills
 * {type, UID, extra only in the root}, adds a hint (HINT_$ADDU, or
 * HINT_$ADDI {extra, node} in the root), and a miss is name_not_found;
 * the unlock status replaces an ok status.
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

#define TEST_SUMMARY() do { \
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed); \
    return tests_failed ? 1 : 0; \
} while (0)

#include "name/name_internal.h"
#include "dir/dir.h"
#include "hint/hint.h"

name_$data_t NAME_$DATA;
uint32_t NODE_$ME = 0x00000042;

static int8_t validate_result;
static int nhints_given, nrem, nlock, nunlock, nexit, nfind, naddi, naddu, ncrash;
static hint_addr_t hints_given[5];
static status_$t rem_status[5], lock_status, unlock_status;
static dir_$old_entry_t rem_entry;
static int8_t find_result;
static uint8_t slotbuf[0x40] __attribute__((aligned(4)));
static uid_t addi_uid, addu_target, addu_source;
static hint_addr_t addi_rec;
static const status_$t *crash_arg;
static hint_addr_t *rem_hint_seen[5];

int8_t name_$validate_leaf(char *name, uint16_t name_len,
                           uint8_t *parsed, uint16_t *parsed_len)
{
    memcpy(parsed, name, name_len);
    *parsed_len = name_len;
    return validate_result;
}
int16_t HINT_$GET_HINTS(uid_t *file_uid, uint32_t *addresses)
{
    (void)file_uid;
    memcpy(addresses, hints_given, sizeof(hint_addr_t) * (size_t)nhints_given);
    return (int16_t)nhints_given;
}
void REM_FILE_$NAME_GET_ENTRYU(void *addr_info, uid_t *dir_uid,
                                char *name, uint16_t name_len,
                                void *result_out, status_$t *status)
{
    (void)dir_uid; (void)name; (void)name_len;
    rem_hint_seen[nrem] = (hint_addr_t *)addr_info;
    *(dir_$old_entry_t *)result_out = rem_entry;
    *status = rem_status[nrem];
    nrem++;
}
void HINT_$ADDI(uid_t *uid_ptr, uint32_t *addresses)
{
    naddi++;
    addi_uid = *uid_ptr;
    memcpy(&addi_rec, addresses, sizeof(addi_rec));
}
void HINT_$ADDU(uid_t *target_uid, uid_t *source_uid)
{
    naddu++;
    addu_target = *target_uid;
    addu_source = *source_uid;
}
void NAME_$LOCK_DIR(uid_t *dir_uid, uint32_t *handle_ret, int16_t lock_mode,
                    int16_t acl_rights, status_$t *status_ret)
{
    (void)dir_uid; (void)lock_mode; (void)acl_rights;
    nlock++;
    *handle_ret = 0x100;
    *status_ret = lock_status;
}
void NAME_$UNLOCK_DIR(status_$t *st) { nunlock++; *st = unlock_status; }
void ACL_$EXIT_SUPER(void) { nexit++; }
int8_t dir_$old_find_entry(uint32_t handle, uint8_t *name, uint16_t name_len,
                           int32_t *entry_ret, uint16_t *slot_idx,
                           uint16_t *chain_level)
{
    (void)handle; (void)name; (void)name_len; (void)slot_idx; (void)chain_level;
    nfind++;
    *entry_ret = (int32_t)ARCH_PTR_TO_VA(slotbuf);
    return find_result;
}
void CRASH_SYSTEM(const status_$t *status) { ncrash++; crash_arg = status; }

#include "../old_get_entry_nonroot.c"

static uid_t dir = { 0x10, 0x00000042 };
static dir_$old_entry_t e;

static void setup(void)
{
    dir_$old_slot_t *s = (dir_$old_slot_t *)slotbuf;
    memset(&NAME_$DATA, 0, sizeof(NAME_$DATA));
    NAME_$DATA.root_uid.high = 0x1234;
    NAME_$DATA.root_uid.low = 0x5678;
    ARCH_HOST_VA_BASE = (uintptr_t)slotbuf - 0x100;
    validate_result = (int8_t)0xFF;
    nhints_given = nrem = nlock = nunlock = nexit = nfind = naddi = naddu = ncrash = 0;
    memset(hints_given, 0, sizeof(hints_given));
    memset(rem_status, 0, sizeof(rem_status));
    memset(&rem_entry, 0, sizeof(rem_entry));
    lock_status = unlock_status = 0;
    find_result = (int8_t)0xFF;
    memset(slotbuf, 0, sizeof(slotbuf));
    s->type = 1;
    s->uid.high = 0x99;
    s->uid.low = 0x00100007;            /* node 0x00007: another node */
    s->extra = 0x55;
    memset(&e, 0, sizeof(e));
}

TEST(invalid_leaf)
{
    status_$t st = 0;
    setup();
    validate_result = 0;
    name_$old_get_entry_nonroot(&dir, "x", 1, &e, &st);
    ASSERT_EQ(status_$naming_invalid_leaf, st);
    ASSERT_EQ(0, nlock);
}

TEST(local_hint_found)
{
    status_$t st = 9;
    setup();
    nhints_given = 1;
    hints_given[0].node_id = 0x42;
    name_$old_get_entry_nonroot(&dir, "x", 1, &e, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, nlock);
    ASSERT_EQ(1, e.type);
    ASSERT_EQ(0x99, e.uid.high);
    ASSERT_EQ(0, e.extra);                      /* not the root */
    ASSERT_EQ(1, naddu);
    ASSERT_EQ(0x99, addu_target.high);
    ASSERT_EQ(0x10, addu_source.high);
    ASSERT_EQ(1, nunlock);
    ASSERT_EQ(1, nexit);
}

TEST(local_root_hint_pair)
{
    status_$t st = 9;
    setup();
    nhints_given = 1;
    hints_given[0].node_id = 0x42;
    name_$old_get_entry_nonroot(&NAME_$DATA.root_uid, "x", 1, &e, &st);
    ASSERT_EQ(0x55, e.extra);
    ASSERT_EQ(1, naddi);
    ASSERT_EQ(0x55, addi_rec.flags);
    ASSERT_EQ(0x00007, addi_rec.node_id);
}

TEST(local_miss)
{
    status_$t st = 9;
    setup();
    nhints_given = 1;
    hints_given[0].node_id = 0x42;
    find_result = 0;
    unlock_status = 0x77;
    name_$old_get_entry_nonroot(&dir, "x", 1, &e, &st);
    ASSERT_EQ(status_$naming_name_not_found, st);
}

TEST(unlock_status_replaces_ok)
{
    status_$t st = 9;
    setup();
    nhints_given = 1;
    hints_given[0].node_id = 0x42;
    unlock_status = 0x77;
    name_$old_get_entry_nonroot(&dir, "x", 1, &e, &st);
    ASSERT_EQ(0x77, st);
}

TEST(remote_second_hint)
{
    status_$t st = 9;
    setup();
    nhints_given = 2;
    hints_given[0].node_id = 0x50;
    hints_given[1].node_id = 0x51;
    rem_status[0] = 0x00110001;                 /* not final: next hint */
    rem_status[1] = 0;
    rem_entry.type = 2;
    rem_entry.uid.low = 0x00100042;
    name_$old_get_entry_nonroot(&dir, "x", 1, &e, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(2, nrem);
    ASSERT_EQ(1, naddi);                        /* hint 2 promoted */
    ASSERT_EQ(0x51, addi_rec.node_id);
    ASSERT_EQ(0, nlock);
}

TEST(remote_final_error)
{
    status_$t st = 9;
    setup();
    nhints_given = 2;
    hints_given[0].node_id = 0x50;
    rem_status[0] = status_$naming_insufficient_rights;
    name_$old_get_entry_nonroot(&dir, "x", 1, &e, &st);
    ASSERT_EQ(status_$naming_insufficient_rights, st);
    ASSERT_EQ(1, nrem);
}

TEST(remote_locked_goes_local)
{
    status_$t st = 9;
    setup();
    nhints_given = 1;
    hints_given[0].node_id = 0x50;
    rem_status[0] = status_$naming_directory_locked;
    name_$old_get_entry_nonroot(&dir, "x", 1, &e, &st);
    ASSERT_EQ(1, nlock);
    ASSERT_EQ(0, st);
}

TEST(hints_exhausted_crash)
{
    status_$t st = 9;
    setup();
    nhints_given = 1;
    hints_given[0].node_id = 0x50;
    rem_status[0] = 0x00110001;
    name_$old_get_entry_nonroot(&dir, "x", 1, &e, &st);
    ASSERT_EQ(1, ncrash);
    ASSERT_EQ(0x000E0007, *crash_arg);
}

int main(void)
{
    printf("name_$old_get_entry_nonroot tests\n");
    RUN_TEST(invalid_leaf);
    RUN_TEST(local_hint_found);
    RUN_TEST(local_root_hint_pair);
    RUN_TEST(local_miss);
    RUN_TEST(unlock_status_replaces_ok);
    RUN_TEST(remote_second_hint);
    RUN_TEST(remote_final_error);
    RUN_TEST(remote_locked_goes_local);
    RUN_TEST(hints_exhausted_crash);
    TEST_SUMMARY();
}
