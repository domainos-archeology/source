/*
 * area/test/test_get_aste.c - unit tests for area_$get_aste (0x00E09A6A)
 * and area_$wait_pite_in_trans (0x00E0778E)
 *
 * The ASTE table, segment map and area table are host objects; the AST,
 * DBUF, EC and ML callees and area_$rpmap_get are mocked.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_state(void);

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "area/area_internal.h"
#include "dbuf/dbuf.h"
#include "pmap/pmap.h"
#include "anon/anon.h"

#define TABLE_N 8
static area_$entry_t mock_area_table[TABLE_N];
#undef AREA_TABLE_BASE
#define AREA_TABLE_BASE ((uintptr_t)mock_area_table)
#undef AREA_ENTRY_SIZE
#define AREA_ENTRY_SIZE ((int)sizeof(area_$entry_t))

area_$globals_t AREA_$GLOBALS;
uid_t ANON_$UID;
MODULE_DATA_DEFINE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);
MODULE_DATA_DEFINE(ast_$data_t, AST_$DATA, 0x00E1DC80);
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);

/* ---- mocks ----------------------------------------------------------- */

static int locks, unlocks, waitns, advances, intrans_waits, frees;
static int dbuf_gets, dbuf_sets, rpmap_gets;
static uint16_t next_aste;              /* 1-based slot ALLOCATE hands out */
static area_$seg_slot_t *watch_slot;    /* cleared by the EC wait mock */
static aste_t *intrans_aste;            /* cleared by AST wait mock */
static uint8_t blk[0x400];
static status_$t dbuf_status, rpmap_status;
static uint16_t dbuf_vol, dbuf_type, dbuf_flags;
static int32_t dbuf_block;
static uid_t *dbuf_uid;
static uint16_t set_flags;
static uint16_t rpmap_seg;
static ec_$eventcount_t *advance_ec;

void ML_$LOCK(int16_t id) { (void)id; locks++; }
void ML_$UNLOCK(int16_t id) { (void)id; unlocks++; }
void EC_$ADVANCE(ec_$eventcount_t *ec) { advances++; advance_ec = ec; }

uint16_t EC_$WAITN(ec_$eventcount_t **ecs, int32_t *wait_val, int16_t n)
{
    (void)ecs; (void)wait_val; (void)n;
    waitns++;
    if (watch_slot != NULL) {
        watch_slot->state &= (uint8_t)~AREA_SLOT_IN_TRANS;
    }
    return 0;
}

void AST_$WAIT_FOR_AST_INTRANS(void)
{
    intrans_waits++;
    if (intrans_aste != NULL) {
        intrans_aste->flags &= (uint16_t)~ASTE_FLAG_IN_TRANS;
    }
}

aste_t *AST_$ALLOCATE_ASTE(void)
{
    aste_t *a = AST_ASTE_ENTRY(next_aste);
    a->seg_index = next_aste;
    a->fm_block = 0xABCDEF05;
    a->flags = ASTE_FLAG_IN_TRANS | ASTE_FLAG_DIRTY | ASTE_FLAG_REMOTE;
    return a;
}

void AST_$FREE_ASTE(aste_t *aste) { (void)aste; frees++; }

void *DBUF_$GET_BLOCK(uint16_t vol_idx, int32_t block, uid_t *uid,
                      uint32_t block_hint, uint16_t block_type,
                      uint16_t flags, status_$t *status)
{
    (void)block_hint;
    dbuf_gets++;
    dbuf_vol = vol_idx; dbuf_block = block; dbuf_uid = uid;
    dbuf_type = block_type; dbuf_flags = flags;
    *status = dbuf_status;
    return blk;
}

void DBUF_$SET_BUFF(void *buffer, uint16_t flags, status_$t *status)
{
    (void)buffer;
    dbuf_sets++;
    set_flags = flags;
    *status = status_$ok;
}

void *area_$rpmap_get(area_$entry_t *entry, uint16_t seg_idx, int8_t dirty,
                      int8_t flag, status_$t *status_p)
{
    (void)entry; (void)dirty; (void)flag;
    rpmap_gets++;
    rpmap_seg = seg_idx;
    *status_p = rpmap_status;
    return blk;
}

#include "../wait_pite_in_trans.c"
#include "../get_aste.c"

static area_$seg_slot_t slot;

static void reset_state(void)
{
    int i;
    memset(mock_area_table, 0, sizeof(mock_area_table));
    memset(&AREA_$GLOBALS, 0, sizeof(AREA_$GLOBALS));
    memset(&PMAP_$SEGMAP, 0, sizeof(PMAP_$SEGMAP));
    memset(&AST_$AOT, 0, sizeof(AST_$AOT));
    memset(&AST_$DATA, 0, sizeof(AST_$DATA));
    memset(&slot, 0, sizeof(slot));
    for (i = 0; i < 0x400; i++) {
        blk[i] = (uint8_t)i;
    }
    locks = unlocks = waitns = advances = intrans_waits = frees = 0;
    dbuf_gets = dbuf_sets = rpmap_gets = 0;
    dbuf_status = rpmap_status = status_$ok;
    next_aste = 9;
    watch_slot = NULL;
    intrans_aste = NULL;
    mock_area_table[1].volx = 3;
}

/* ---- tests ----------------------------------------------------------- */

static void test_existing_aste_is_wired(void)
{
    status_$t st = 0x55;
    aste_t *a = AST_ASTE_ENTRY(4), *b = AST_ASTE_ENTRY(6);
    aste_t *r;

    slot.state = AREA_SLOT_HAS_ASTE;
    slot.aste_index = 4;
    a->segment = 10; a->next = b;
    b->segment = 11; b->wire_count = 2;
    r = area_$get_aste(2, &slot, 11, 0, 0, &st);
    ASSERT_EQ((unsigned long)b, (unsigned long)r);
    ASSERT_EQ(0, st);
    ASSERT_EQ(3, b->wire_count);
    ASSERT_EQ(ASTE_FLAG_LOCKED, b->flags);
    ASSERT_EQ(0, advances);             /* early exit skips the release */
}

static void test_waits_for_slot_and_aste(void)
{
    status_$t st;
    aste_t *a = AST_ASTE_ENTRY(4);

    slot.state = AREA_SLOT_HAS_ASTE | AREA_SLOT_IN_TRANS;
    slot.aste_index = 4;
    a->segment = 10;
    a->flags = ASTE_FLAG_IN_TRANS;
    watch_slot = &slot;
    intrans_aste = a;
    area_$get_aste(2, &slot, 10, 0, 0, &st);
    ASSERT_EQ(1, waitns);
    ASSERT_EQ(1, unlocks);              /* the PITE wait drops lock 0x12 */
    ASSERT_EQ(1, locks);
    ASSERT_EQ(1, intrans_waits);
    ASSERT_EQ(1, a->wire_count);
}

static void test_held_skips_wait(void)
{
    status_$t st;
    aste_t *a = AST_ASTE_ENTRY(4);

    slot.state = AREA_SLOT_HAS_ASTE | AREA_SLOT_IN_TRANS;
    slot.aste_index = 4;
    a->segment = 10;
    area_$get_aste(2, &slot, 10, -1, 0, &st);
    ASSERT_EQ(0, waitns);
    ASSERT_EQ(1, a->wire_count);
}

static void test_not_found_no_create(void)
{
    status_$t st;
    struct aste_t *r;

    r = area_$get_aste(2, &slot, 10, 0, 0, &st);
    ASSERT_EQ(0x00030004, st);
    /* A2 = slot + 1 from the wait set-up */
    ASSERT_EQ((unsigned long)&slot.state, (unsigned long)r);
    ASSERT_EQ(0, advances);
}

static void test_create_new_segment(void)
{
    status_$t st;
    aste_t *r;
    uint32_t *row = (uint32_t *)&PMAP_SEGMAP_ROW(9)[0];

    row[5] = 0x1234;
    slot.state = 0x01;                  /* daddr 0x1ABCD */
    slot.aste_index = 0xABCD;
    r = area_$get_aste(2, &slot, 13, 0, -1, &st);
    ASSERT_EQ((unsigned long)AST_ASTE_ENTRY(9), (unsigned long)r);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, AST_$ASTE_AREA_CNT);
    ASSERT_EQ((unsigned long)&mock_area_table[1], (unsigned long)r->aote);
    ASSERT_EQ(13, r->segment);
    ASSERT_EQ(1, r->wire_count);
    ASSERT_EQ(ASTE_FLAG_LOCKED | ASTE_FLAG_AREA | ASTE_FLAG_DIRTY, r->flags);
    ASSERT_EQ(0, row[5]);
    ASSERT_EQ(1u << 5, slot.bits);
    ASSERT_EQ(0, (unsigned long)r->next);
    ASSERT_EQ((0x1ABCDu << 4) | 5, r->fm_block);
    ASSERT_EQ(AREA_SLOT_HAS_ASTE | 0x01, slot.state);
    ASSERT_EQ(9, slot.aste_index);
    ASSERT_EQ(1, advances);
    ASSERT_EQ((unsigned long)&AREA_$PITE_IN_TRANS_EC, (unsigned long)advance_ec);
}

static void test_create_local_reads_block(void)
{
    status_$t st;
    aste_t *r;
    uint32_t *row = (uint32_t *)&PMAP_SEGMAP_ROW(9)[0];
    aste_t *other = AST_ASTE_ENTRY(4);

    slot.bits = 1u << 2;
    slot.state = AREA_SLOT_HAS_ASTE;
    slot.aste_index = 4;
    other->segment = 99;
    other->fm_block = (0x777u << 4) | 1;
    r = area_$get_aste(2, &slot, 10, -1, -1, &st);  /* group 2 */
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, dbuf_gets);
    ASSERT_EQ(3, dbuf_vol);
    ASSERT_EQ(0x777, dbuf_block);
    ASSERT_EQ((unsigned long)&ANON_$UID, (unsigned long)dbuf_uid);
    ASSERT_EQ(1, dbuf_type);
    ASSERT_EQ(0, dbuf_flags);
    ASSERT_EQ(8, set_flags);
    ASSERT_EQ(*(uint32_t *)&blk[0x100], row[0]);
    ASSERT_EQ((unsigned long)other, (unsigned long)r->next);
    ASSERT_EQ((0x777u << 4) | 2, r->fm_block);
    ASSERT_EQ(ASTE_FLAG_LOCKED | ASTE_FLAG_AREA, r->flags);
    ASSERT_EQ(0, advances);             /* held: no release */
}

static void test_create_local_read_failure(void)
{
    status_$t st;
    struct aste_t *r;

    slot.bits = 1;
    dbuf_status = 0x00080001;
    r = area_$get_aste(2, &slot, 0, 0, -1, &st);
    ASSERT_EQ(0x00080001, st);
    ASSERT_EQ(1, frees);
    ASSERT_EQ((unsigned long)blk, (unsigned long)r);
    ASSERT_EQ(1, advances);
    ASSERT_EQ(0, slot.state & AREA_SLOT_IN_TRANS);
}

static void test_create_remote_uses_rpmap(void)
{
    status_$t st;
    aste_t *r;
    uint32_t *row = (uint32_t *)&PMAP_SEGMAP_ROW(9)[0];

    mock_area_table[1].remote_volx = 5;
    slot.bits = 1u << 3;
    r = area_$get_aste(2, &slot, 3, 0, -1, &st);
    ASSERT_EQ(1, rpmap_gets);
    ASSERT_EQ(3, rpmap_seg);
    ASSERT_EQ(0, dbuf_gets);
    ASSERT_EQ(*(uint32_t *)&blk[0x180], row[0]);
    ASSERT_EQ(ASTE_FLAG_LOCKED | ASTE_FLAG_AREA | ASTE_FLAG_REMOTE, r->flags);
}

static void test_create_remote_failure_returns_entry(void)
{
    status_$t st;
    struct aste_t *r;

    mock_area_table[1].remote_volx = 5;
    slot.bits = 1;
    rpmap_status = 0x00110001;
    r = area_$get_aste(2, &slot, 0, 0, -1, &st);
    ASSERT_EQ(1, frees);
    ASSERT_EQ((unsigned long)&mock_area_table[1], (unsigned long)r);
}

int main(void)
{
    printf("area_$get_aste tests:\n");
    RUN_TEST(existing_aste_is_wired);
    RUN_TEST(waits_for_slot_and_aste);
    RUN_TEST(held_skips_wait);
    RUN_TEST(not_found_no_create);
    RUN_TEST(create_new_segment);
    RUN_TEST(create_local_reads_block);
    RUN_TEST(create_local_read_failure);
    RUN_TEST(create_remote_uses_rpmap);
    RUN_TEST(create_remote_failure_returns_entry);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
