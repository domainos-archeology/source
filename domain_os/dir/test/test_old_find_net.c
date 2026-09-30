/*
 * dir/test/test_old_find_net.c - DIR_$OLD_FIND_NET (0x00E55AFE) and its
 * slot search dir_$old_find_node_slot (0x00E559D6)
 *
 * Pins: lock failure answers 0; a slot whose UID low 20 bits equal the node
 * answers its extra longword; a root-directory miss asks
 * REM_NAME_$FIND_NETWORK, caches the entry and answers its extra; other
 * misses answer 0; ACL_$EXIT_SUPER runs on every path.
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

static uint32_t rem_node_seen;

void REM_NAME_$FIND_NETWORK(uid_t *dir_uid, uint32_t *target_node,
                            void *entry_ret, status_$t *status_ret)
{
    dir_$rep_entry_t *r = (dir_$rep_entry_t *)entry_ret;
    (void)dir_uid;
    nrem++;
    rem_node_seen = *target_node;
    memset(r, 0, sizeof(*r));
    memcpy(r->name, "NODE", 4);
    r->name_len = 4;
    r->uid.high = 0xAB;
    r->uid.low = 0xCD;
    r->extra = 0x99;
    *status_ret = rem_status;
}

#include "../old_find_net.c"

static uid_t root = { 0x1234, 0x5678 };
static uid_t other = { 0x1, 0x2 };

TEST(lock_fails)
{
    uint32_t node = 0x42;
    base_setup();
    lock_status = 0x1234;
    ASSERT_EQ(0, DIR_$OLD_FIND_NET(&other, &node));
    ASSERT_EQ(1, nexit);
    ASSERT_EQ(0, nunlock);
}

TEST(inline_match_masks_node)
{
    uint32_t node = 0x42;
    base_setup();
    inline_slot(3)->type = 1;
    inline_slot(3)->uid.low = 0xABF00042;   /* low 20 bits = 0x00042 */
    inline_slot(3)->extra = 0xCAFE;
    ASSERT_EQ(0xCAFE, DIR_$OLD_FIND_NET(&other, &node));
    ASSERT_EQ(1, nunlock);
    ASSERT_EQ(1, nexit);
    ASSERT_EQ(0, nrem);
}

TEST(block_match)
{
    uint32_t node = 0x42;
    base_setup();
    dirbuf[0x96 * 1 + 0x36F] = 1;
    block_slot(1, 2)->type = 1;
    block_slot(1, 2)->uid.low = 0x00100042;
    block_slot(1, 2)->extra = 0xBEEF;
    ASSERT_EQ(0xBEEF, DIR_$OLD_FIND_NET(&other, &node));
}

TEST(miss_not_root)
{
    uint32_t node = 0x42;
    base_setup();
    inline_slot(1)->type = 2;               /* not in use */
    inline_slot(1)->uid.low = 0x42;
    ASSERT_EQ(0, DIR_$OLD_FIND_NET(&other, &node));
    ASSERT_EQ(0, nrem);
}

TEST(miss_root_remote)
{
    uint32_t node = 0x42;
    base_setup();
    ASSERT_EQ(0x99, DIR_$OLD_FIND_NET(&root, &node));
    ASSERT_EQ(1, nrem);
    ASSERT_EQ(0x42, rem_node_seen);
    ASSERT_EQ(1, nadd);
    ASSERT_EQ(4, add_len_seen);
    ASSERT_EQ(0x99, add_extra_seen);
    ASSERT_EQ(0x20, unmap_max_seen);
}

TEST(miss_root_remote_fails)
{
    uint32_t node = 0x42;
    base_setup();
    rem_status = 0x11;
    ASSERT_EQ(0, DIR_$OLD_FIND_NET(&root, &node));
    ASSERT_EQ(0, nadd);
    ASSERT_EQ(1, nexit);
}

int main(void)
{
    printf("DIR_$OLD_FIND_NET tests:\n");
    RUN_TEST(lock_fails);
    RUN_TEST(inline_match_masks_node);
    RUN_TEST(block_match);
    RUN_TEST(miss_not_root);
    RUN_TEST(miss_root_remote);
    RUN_TEST(miss_root_remote_fails);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
