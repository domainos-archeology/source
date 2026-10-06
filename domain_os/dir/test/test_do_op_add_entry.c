/*
 * dir/test/test_do_op_add_entry.c - dir_$do_op_add_entry (0x00E4FEF2)
 *
 * Pins: open (mode 2, the caller rights) then dir_$add_entry; an open
 * failure only releases; name_already_exists from a type-9 process is
 * turned into ok when the existing entry is the same object (type 2 UID,
 * type 3 UID and extra, type 4 text inline or on its link page), kept
 * otherwise; a hard link added to the root directory is hinted with
 * {extra, node bits}; the handle volume is returned; release and
 * ACL_$EXIT_SUPER on every path.
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

#include "dir/dir_internal.h"

status_$t Naming_bad_request_header_ver_err = 0x000E0025;
name_$data_t NAME_$DATA;
uint16_t PROC1_$CURRENT = 3;
MODULE_DATA_DEFINE(proc1_$data_t, PROC1_$DATA, 0x00E254E8);

static uint8_t arena[0x100] __attribute__((aligned(8)));
#define HVA 0x100
static uint8_t entry[0x40] __attribute__((aligned(4)));
static uint8_t linkpage[0x40];
static status_$t open_status, add_status;
static int8_t find_result;
static int16_t open_mode_seen, open_rights_seen;
static int nenter, nexit, nrelease, nfind, nmaplink, nhint, ncrash;
static uid_t hint_uid_seen;
static hint_addr_t hint_seen;
static uint16_t maplink_page_seen;

void ACL_$ENTER_SUPER(void) { nenter++; }
void ACL_$EXIT_SUPER(void) { nexit++; }
void dir_$open_dir(void *uid, int16_t mode, int16_t rights,
                   void *handle_ret, status_$t *status_ret)
{
    (void)uid;
    open_mode_seen = mode;
    open_rights_seen = rights;
    *(uint32_t *)handle_ret = HVA;
    *status_ret = open_status;
}
void dir_$add_entry(uint32_t handle, void *name, uint16_t name_len,
                    uint16_t entry_type, uint32_t extra, uid_t *uid,
                    uint16_t link_len, void *link_data, status_$t *status_ret)
{
    (void)handle; (void)name; (void)name_len; (void)entry_type; (void)extra;
    (void)uid; (void)link_len; (void)link_data;
    *status_ret = add_status;
}
char dir_$find_entry(void *handle, void *name, int16_t name_len,
                     int16_t flags, void **entry_ret,
                     void *extra, int16_t *depth_ret)
{
    (void)handle; (void)name; (void)name_len; (void)flags; (void)extra;
    (void)depth_ret;
    nfind++;
    *entry_ret = entry;
    return (char)find_result;
}
void *dir_$map_link_page(void *handle, uint16_t page_idx)
{
    (void)handle;
    nmaplink++;
    maplink_page_seen = page_idx;
    return linkpage;
}
void CRASH_SYSTEM(const status_$t *status) { (void)status; ncrash++; }
void HINT_$ADDI(uid_t *uid_ptr, uint32_t *addresses)
{
    nhint++;
    hint_uid_seen = *uid_ptr;
    memcpy(&hint_seen, addresses, sizeof(hint_seen));
}
void dir_$release_handle(void *handle_ptr) { (void)handle_ptr; nrelease++; }

#include "../do_op_add_entry.c"

static uid_t dir = { 0x10, 0x20 };
static uid_t target = { 0x30, 0x00ABCDEF };
static int16_t vol;

static void setup(void)
{
    memset(arena, 0, sizeof(arena));
    memset(entry, 0, sizeof(entry));
    memset(linkpage, 0, sizeof(linkpage));
    ARCH_HOST_VA_BASE = (uintptr_t)arena - HVA;
    ((dir_$handle_t *)arena)->volume = 7;
    NAME_$DATA.root_uid.high = 0x1234;
    NAME_$DATA.root_uid.low = 0x5678;
    PROC1_$DATA.type[PROC1_$CURRENT] = 9;
    open_status = add_status = 0;
    find_result = (int8_t)0xFF;
    nenter = nexit = nrelease = nfind = nmaplink = nhint = ncrash = 0;
    vol = 0;
}

TEST(plain_add)
{
    status_$t st = 9;
    setup();
    dir_$do_op_add_entry(&dir, 8, "x", 1, 2, 0, &target, 0, 0, &vol, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(2, open_mode_seen);
    ASSERT_EQ(8, open_rights_seen);
    ASSERT_EQ(7, vol);
    ASSERT_EQ(1, nenter);
    ASSERT_EQ(1, nrelease);
    ASSERT_EQ(1, nexit);
    ASSERT_EQ(0, nhint);
}

TEST(open_fails)
{
    status_$t st = 9;
    setup();
    open_status = 0x000E0033;
    dir_$do_op_add_entry(&dir, 0, "x", 1, 2, 0, &target, 0, 0, &vol, &st);
    ASSERT_EQ(0x000E0033, st);
    ASSERT_EQ(0, vol);
    ASSERT_EQ(1, nrelease);
    ASSERT_EQ(1, nexit);
}

TEST(repeat_same_file)
{
    status_$t st = 9;
    setup();
    add_status = status_$name_already_exists;
    entry[0] = 2;
    memcpy(entry + 4, &target, 8);
    dir_$do_op_add_entry(&dir, 0, "x", 1, 2, 0, &target, 0, 0, &vol, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(7, vol);
}

TEST(repeat_other_file)
{
    status_$t st = 9;
    uid_t other = { 0x31, 0 };
    setup();
    add_status = status_$name_already_exists;
    entry[0] = 2;
    memcpy(entry + 4, &other, 8);
    dir_$do_op_add_entry(&dir, 0, "x", 1, 2, 0, &target, 0, 0, &vol, &st);
    ASSERT_EQ(status_$name_already_exists, st);
    ASSERT_EQ(0, vol);
}

TEST(exists_not_type9)
{
    status_$t st = 9;
    setup();
    PROC1_$DATA.type[PROC1_$CURRENT] = 1;
    add_status = status_$name_already_exists;
    dir_$do_op_add_entry(&dir, 0, "x", 1, 2, 0, &target, 0, 0, &vol, &st);
    ASSERT_EQ(status_$name_already_exists, st);
    ASSERT_EQ(0, nfind);
}

TEST(repeat_hard_link_needs_extra)
{
    status_$t st = 9;
    uint32_t extra = 0x55;
    setup();
    add_status = status_$name_already_exists;
    entry[0] = 3;
    memcpy(entry + 4, &target, 8);
    memcpy(entry + 0xC, &extra, 4);
    dir_$do_op_add_entry(&dir, 0, "x", 1, 3, 0x56, &target, 0, 0, &vol, &st);
    ASSERT_EQ(status_$name_already_exists, st);
    dir_$do_op_add_entry(&dir, 0, "x", 1, 3, 0x55, &target, 0, 0, &vol, &st);
    ASSERT_EQ(0, st);
}

TEST(repeat_soft_link_inline)
{
    status_$t st = 9;
    int16_t inl = -1;
    uint16_t len = 3;
    static char text[] = "abc";
    setup();
    add_status = status_$name_already_exists;
    entry[0] = 4;
    entry[1] = 2;                               /* name length */
    memcpy(entry + 2, &len, 2);
    memcpy(entry + 4, &inl, 2);
    memcpy(entry + 0xC + 2, "abc", 3);
    dir_$do_op_add_entry(&dir, 0, "nm", 2, 4, 0, &target, 3,
                         ARCH_PTR_TO_VA(text), &vol, &st);
    ASSERT_EQ(0, nmaplink);
    ASSERT_EQ(0, st);
}

TEST(repeat_soft_link_on_page)
{
    status_$t st = 9;
    int16_t pg = 5;
    uint16_t len = 3;
    static char text[] = "abd";
    setup();
    add_status = status_$name_already_exists;
    entry[0] = 4;
    memcpy(entry + 2, &len, 2);
    memcpy(entry + 4, &pg, 2);
    memcpy(linkpage, "abc", 3);
    dir_$do_op_add_entry(&dir, 0, "nm", 2, 4, 0, &target, 3,
                         ARCH_PTR_TO_VA(text), &vol, &st);
    ASSERT_EQ(1, nmaplink);
    ASSERT_EQ(5, maplink_page_seen);
    ASSERT_EQ(status_$name_already_exists, st);
}

TEST(root_hard_link_hint)
{
    status_$t st = 9;
    setup();
    dir_$do_op_add_entry(&NAME_$DATA.root_uid, 0, "x", 1, 3, 0x99, &target,
                         0, 0, &vol, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, nhint);
    ASSERT_EQ(0x30, hint_uid_seen.high);
    ASSERT_EQ(0x99, hint_seen.flags);
    ASSERT_EQ(0x0BCDEF, hint_seen.node_id);
}

int main(void)
{
    printf("dir_$do_op_add_entry tests\n");
    RUN_TEST(plain_add);
    RUN_TEST(open_fails);
    RUN_TEST(repeat_same_file);
    RUN_TEST(repeat_other_file);
    RUN_TEST(exists_not_type9);
    RUN_TEST(repeat_hard_link_needs_extra);
    RUN_TEST(repeat_soft_link_inline);
    RUN_TEST(repeat_soft_link_on_page);
    RUN_TEST(root_hard_link_hint);
    TEST_SUMMARY();
}
