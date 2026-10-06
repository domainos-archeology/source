/*
 * dir/test/test_old_read_entries.c - dir_$old_read_entries (0x00E579C0)
 *
 * Pins: max 0 or a zero cursor returns ok without locking or
 * ACL_$EXIT_SUPER; inline slots then block slots are packed as
 * (0x1A + len) & ~3 byte records with the entry position; the walk stops
 * at the entry count, at max_entries, or before a record that does not
 * fit, leaving the cursor on the next entry ({0,0} when done); the
 * illegal-cursor status is overwritten by NAME_$UNLOCK_DIR.
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

const int16_t name_$leaf_max_len_00e544ae = 0x0020;
static int nlock, nunlock, nexit;
static int16_t lock_mode_seen, lock_rights_seen;

void NAME_$LOCK_DIR(uid_t *dir_uid, uint32_t *handle_ret, int16_t lock_mode,
                    int16_t acl_rights, status_$t *status_ret)
{
    (void)dir_uid;
    nlock++;
    lock_mode_seen = lock_mode;
    lock_rights_seen = acl_rights;
    *handle_ret = DVA;
    *status_ret = 0;
}
void NAME_$UNLOCK_DIR(status_$t *st) { nunlock++; *st = 0; }
void ACL_$EXIT_SUPER(void) { nexit++; }
void UNMAP_CASE(char *name, int16_t *name_len, char *output,
                int16_t *max_out_len, int16_t *out_len, uint8_t *truncated)
{
    (void)max_out_len;
    memcpy(output, name, (size_t)*name_len);
    *out_len = *name_len;
    *truncated = 0;
}

#include "../old_read_entries.c"

static uint8_t buf[0x200] __attribute__((aligned(4)));
static uid_t d = { 1, 2 };

static void put(dir_$old_slot_t *s, const char *nm, uint32_t uidlo)
{
    s->type = 1;
    s->name_len = (uint8_t)strlen(nm);
    memcpy(s->name, nm, strlen(nm));
    s->uid.high = 0xAA;
    s->uid.low = uidlo;
    s->extra = 0xEE;
}

static void setup(void)
{
    dir_setup(7, 10, 2, 1);
    w16(0x04, 3);
    w16(0x16, 3);
    w16(0x18, 5);
    put((dir_$old_slot_t *)(dirbuf + 0x30 * 1 - 0x16), "ab", 1);
    put((dir_$old_slot_t *)(dirbuf + 0x30 * 3 - 0x16), "c", 3);
    blk(1)[0x36E] = 1;
    put(bslot(1, 1), "blk", 4);
    memset(buf, 0xCC, sizeof(buf));
    nlock = nunlock = nexit = 0;
}

TEST(reads_everything)
{
    uint16_t cur[2] = { 0, 1 };
    uint32_t n = 0; status_$t st = 9;
    dir_$old_readu_rec_t *r;
    setup();
    dir_$old_read_entries(&d, cur, 10, sizeof(buf), buf, &n, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(3, n);
    ASSERT_EQ(0, cur[0]);
    ASSERT_EQ(0, cur[1]);
    ASSERT_EQ(1, lock_mode_seen);
    ASSERT_EQ(4, lock_rights_seen);
    r = (dir_$old_readu_rec_t *)buf;
    ASSERT_EQ(0x1C, r->size);
    ASSERT_EQ(1, r->type);
    ASSERT_EQ(1, r->uid.low);
    ASSERT_EQ(0xEE, r->extra);
    ASSERT_EQ(0, r->cursor[0]);
    ASSERT_EQ(1, r->cursor[1]);
    ASSERT_EQ(2, r->name_len);
    ASSERT_EQ(0, memcmp(r->name, "ab", 3));
    r = (dir_$old_readu_rec_t *)(buf + 0x1C);
    ASSERT_EQ(0x18, r->size);
    ASSERT_EQ(3, r->uid.low);
    r = (dir_$old_readu_rec_t *)(buf + 0x34);
    ASSERT_EQ(0x1C, r->size);
    ASSERT_EQ(1, r->cursor[0]);
    ASSERT_EQ(1, r->cursor[1]);
    ASSERT_EQ(0, memcmp(r->name, "blk", 4));
    ASSERT_EQ(1, nexit);
}

TEST(buffer_full_keeps_cursor)
{
    uint16_t cur[2] = { 0, 1 };
    uint32_t n = 0; status_$t st = 9;
    setup();
    dir_$old_read_entries(&d, cur, 10, 0x30, buf, &n, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, n);
    ASSERT_EQ(0, cur[0]);
    ASSERT_EQ(3, cur[1]);
}

TEST(max_entries_stops)
{
    uint16_t cur[2] = { 0, 1 };
    uint32_t n = 0; status_$t st = 9;
    setup();
    dir_$old_read_entries(&d, cur, 1, sizeof(buf), buf, &n, &st);
    ASSERT_EQ(1, n);
    ASSERT_EQ(0, cur[0]);
    ASSERT_EQ(2, cur[1]);
}

TEST(zero_cursor_no_lock)
{
    uint16_t cur[2] = { 0, 0 };
    uint32_t n = 7; status_$t st = 9;
    setup();
    dir_$old_read_entries(&d, cur, 10, sizeof(buf), buf, &n, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, n);
    ASSERT_EQ(0, nlock);
    ASSERT_EQ(0, nexit);
}

TEST(illegal_cursor_status_lost)
{
    uint16_t cur[2] = { 0, 9 };
    uint32_t n = 0; status_$t st = 9;
    setup();
    dir_$old_read_entries(&d, cur, 10, sizeof(buf), buf, &n, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, n);
    ASSERT_EQ(9, cur[1]);
    ASSERT_EQ(1, nunlock);
}

int main(void)
{
    printf("dir_$old_read_entries tests\n");
    RUN_TEST(reads_everything);
    RUN_TEST(buffer_full_keeps_cursor);
    RUN_TEST(max_entries_stops);
    RUN_TEST(zero_cursor_no_lock);
    RUN_TEST(illegal_cursor_status_lost);
    TEST_SUMMARY();
}
