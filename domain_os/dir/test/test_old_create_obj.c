/*
 * dir/test/test_old_create_obj.c - dir_$old_create_obj (0x00E54546)
 *
 * Pins: create (type, UID_$NIL, parent), lock (mode 4, flags 0x88, local
 * FALSE, NAME_$CONST_ZERO_L), map (0x7FFF, area 0x16, TRUE twice), init
 * the buffer and set both info lengths to 0x10, unmap and unlock; a parent
 * with fewer than 0x13 inline slots and a 16-byte info block passes on its
 * two default ACLs, otherwise ACL_$DEFAULT_ACL(&0) / (&type) are used;
 * then AST_$COND_FLUSH.  A failure truncates a created object and sets
 * bit 31 of the status.
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

/* A fake old-format directory in an arena the VA macros can reach. */
static uint8_t dirbuf[0x4000] __attribute__((aligned(4)));
#define DVA 0x100
static void w16(uint32_t off, uint16_t v) { *(uint16_t *)(dirbuf + off) = v; }
static uint16_t r16(uint32_t off) { return *(uint16_t *)(dirbuf + off); }
static uint8_t *blk(int k) { return dirbuf + 0x96 * k; }
static dir_$old_slot_t *bslot(int k, int j)
{
    return (dir_$old_slot_t *)(dirbuf + 0x96 * k + 0x30 * j + 0x340);
}
static void dir_setup(uint16_t buckets, uint16_t max_blocks,
                      uint16_t block_slots, uint16_t blocks_used)
{
    memset(dirbuf, 0, sizeof(dirbuf));
    ARCH_HOST_VA_BASE = (uintptr_t)dirbuf - DVA;
    w16(0x02, buckets);
    w16(0x06, max_blocks);
    w16(0x08, block_slots);
    w16(0x0A, blocks_used);
}

uid_t UID_$NIL = { 0, 0 };
uid_t ACL_$DIR_ACL = { 0x0000DDDD, 0x00000001 };
uid_t ACL_$FILE_ACL = { 0x0000FFFF, 0x00000002 };
uint32_t NAME_$CONST_ZERO_L = 0;
int16_t NAME_$CONST_ZERO_W = 0;
uint16_t PROC1_$AS_ID = 0x21;

static uint8_t newdir[0x400] __attribute__((aligned(4)));
static status_$t create_st, lock_st, map_st;
static int ncreate, nlock, nmap, nunmap, nunlock, ninit, nset, ndef, nflush, ntrunc;
static int16_t create_type_seen, def_type_seen[2];
static uint16_t lock_mode_seen, lock_flags_seen;
static boolean lock_local_seen, map_dir_seen, map_rights_seen;
static void **lock_ctx_seen;
static uint32_t map_len_seen;
static int16_t map_area_seen;
static uid_t set_type_seen[2], set_acl_seen[2];

uint32_t FILE_$PRIV_CREATE(int16_t file_type, const uid_t *type_uid, uid_t *dir_uid,
                           uid_t *file_uid_ret, uint32_t initial_size,
                           uint16_t flags, uid_t *owner_info, status_$t *status_ret)
{
    (void)type_uid; (void)dir_uid; (void)initial_size; (void)flags; (void)owner_info;
    ncreate++;
    create_type_seen = file_type;
    file_uid_ret->high = 0x777;
    file_uid_ret->low = 0x888;
    *status_ret = create_st;
    return 0;
}
void FILE_$PRIV_LOCK(uid_t *file_uid, int16_t asid, uint16_t side,
                     uint16_t lock_mode, boolean local_only,
                     uint16_t flags, uint16_t key,
                     uint32_t rem_key, uint32_t rem_node, uint32_t rem_extra,
                     void **acl_ctx, uint16_t rem_wait,
                     uint32_t *slot_io, uint16_t *rights_out,
                     status_$t *status_ret)
{
    (void)file_uid; (void)asid; (void)side; (void)key; (void)rem_key;
    (void)rem_node; (void)rem_extra; (void)rem_wait; (void)rights_out;
    nlock++;
    lock_mode_seen = lock_mode;
    lock_local_seen = local_only;
    lock_flags_seen = flags;
    lock_ctx_seen = acl_ctx;
    *slot_io = 5;
    *status_ret = lock_st;
}
void *MST_$MAPS(int16_t asid, boolean direction, uid_t *uid, uint32_t start_va,
                uint32_t length, int16_t area_id, uint32_t area_size,
                boolean access_rights, void *map_info, status_$t *status)
{
    (void)asid; (void)uid; (void)start_va; (void)area_size; (void)map_info;
    nmap++;
    map_dir_seen = direction;
    map_rights_seen = access_rights;
    map_len_seen = length;
    map_area_seen = area_id;
    *status = map_st;
    return newdir;
}
void MST_$UNMAP_PRIVI(int16_t mode, uid_t *uid, uint32_t start, uint32_t size,
                      uint16_t asid, status_$t *status_ret)
{
    (void)mode; (void)uid; (void)start; (void)size; (void)asid;
    nunmap++;
    *status_ret = 0;
}
boolean FILE_$PRIV_UNLOCK(uid_t *file_uid, int32_t lock_slot,
                          uint16_t lock_mode, uint16_t asid,
                          boolean by_key, uint16_t key,
                          uint32_t rem_key, uint32_t rem_node,
                          uint32_t *dtv_out, status_$t *status_ret)
{
    (void)file_uid; (void)lock_slot; (void)lock_mode; (void)asid; (void)by_key;
    (void)key; (void)rem_key; (void)rem_node; (void)dtv_out;
    nunlock++;
    *status_ret = 0;
    return 0;
}
void dir_$old_init_buf(void *buffer) { (void)buffer; ninit++; }
void DIR_$OLD_SET_DEFAULT_ACL(uid_t *dir_uid, uid_t *acl_type, uid_t *acl_uid,
                              status_$t *status_ret)
{
    (void)dir_uid;
    if (nset < 2) { set_type_seen[nset] = *acl_type; set_acl_seen[nset] = *acl_uid; }
    nset++;
    *status_ret = 0x11;                 /* private status: ignored */
}
void ACL_$DEFAULT_ACL(uid_t *acl_ret, int16_t *acl_type)
{
    if (ndef < 2) def_type_seen[ndef] = *acl_type;
    ndef++;
    acl_ret->high = 0xDEF0 + (uint32_t)*acl_type;
    acl_ret->low = 0;
}
void AST_$COND_FLUSH(uid_t *uid, uint32_t *timestamp, status_$t *status)
{
    (void)uid; (void)timestamp;
    nflush++;
    *status = 0;
}
void AST_$TRUNCATE(uid_t *uid, uint32_t new_size, uint16_t flags,
                   boolean *result, status_$t *status)
{
    (void)uid; (void)new_size; (void)flags; (void)result;
    ntrunc++;
    *status = 0;
}

#include "../old_create_obj.c"

static uid_t parent = { 1, 2 };
static uid_t nu;

static void setup(void)
{
    dir_setup(7, 10, 3, 2);
    memset(newdir, 0, sizeof(newdir));
    create_st = lock_st = map_st = 0;
    ncreate = nlock = nmap = nunmap = nunlock = ninit = nset = ndef = nflush = ntrunc = 0;
}

TEST(inherits_parent_acls)
{
    status_$t st = 9;
    setup();
    w16(0x04, 0x12);
    w16(0x37E, 0x10);
    *(uint32_t *)(dirbuf + 0x382) = 0xD1;
    *(uint32_t *)(dirbuf + 0x38A) = 0xF1;
    dir_$old_create_obj(&parent, DVA, 1, &nu, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, create_type_seen);
    ASSERT_EQ(4, lock_mode_seen);
    ASSERT_EQ(0x88, lock_flags_seen);
    ASSERT_EQ(0, lock_local_seen);
    ASSERT_EQ(1, lock_ctx_seen == (void **)&NAME_$CONST_ZERO_L);
    ASSERT_EQ(0xFF, (uint8_t)map_dir_seen);
    ASSERT_EQ(0xFF, (uint8_t)map_rights_seen);
    ASSERT_EQ(0x7FFF, map_len_seen);
    ASSERT_EQ(0x16, map_area_seen);
    ASSERT_EQ(1, ninit);
    ASSERT_EQ(0x10, *(uint16_t *)(newdir + 0x37C));
    ASSERT_EQ(0x10, *(uint16_t *)(newdir + 0x37E));
    ASSERT_EQ(1, nunmap);
    ASSERT_EQ(1, nunlock);
    ASSERT_EQ(2, nset);
    ASSERT_EQ(0x0000FFFF, set_type_seen[0].high);
    ASSERT_EQ(0xF1, set_acl_seen[0].high);
    ASSERT_EQ(0x0000DDDD, set_type_seen[1].high);
    ASSERT_EQ(0xD1, set_acl_seen[1].high);
    ASSERT_EQ(0, ndef);
    ASSERT_EQ(1, nflush);
    ASSERT_EQ(0, ntrunc);
}

TEST(default_acls)
{
    status_$t st = 9;
    setup();
    w16(0x04, 0x13);
    w16(0x37E, 0x10);
    dir_$old_create_obj(&parent, DVA, 1, &nu, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(2, ndef);
    ASSERT_EQ(0, def_type_seen[0]);
    ASSERT_EQ(1, def_type_seen[1]);
    ASSERT_EQ(0xDEF0, set_acl_seen[0].high);
    ASSERT_EQ(0xDEF1, set_acl_seen[1].high);
}

TEST(map_fails)
{
    status_$t st = 9;
    setup();
    map_st = 0x00040003;
    dir_$old_create_obj(&parent, DVA, 1, &nu, &st);
    ASSERT_EQ(0x80040003, (uint32_t)st);
    ASSERT_EQ(0, nunmap);
    ASSERT_EQ(1, nunlock);
    ASSERT_EQ(0, nset);
    ASSERT_EQ(1, ntrunc);
}

TEST(create_fails)
{
    status_$t st = 9;
    setup();
    create_st = 0x000F0005;
    dir_$old_create_obj(&parent, DVA, 1, &nu, &st);
    ASSERT_EQ(0x800F0005, (uint32_t)st);
    ASSERT_EQ(0, nlock);
    ASSERT_EQ(0, nunlock);
    ASSERT_EQ(0, ntrunc);
}

int main(void)
{
    printf("dir_$old_create_obj tests\n");
    RUN_TEST(inherits_parent_acls);
    RUN_TEST(default_acls);
    RUN_TEST(map_fails);
    RUN_TEST(create_fails);
    TEST_SUMMARY();
}
