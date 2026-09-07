/*
 * acl/test/test_find_acl_slot.c - unit tests for acl_$find_acl_slot
 * (0x00E45E8E)
 *
 * The real acl/find_acl_slot.c and acl/cache_list.c are #included at the
 * bottom, so the LRU bookkeeping under test is the real
 * acl_$cache_list_insert / acl_$cache_list_remove (0x00E44C3C / 0x00E44C92).
 * UID_$HASH, acl_$load_acl_image and ACL_$DEF_ACLDATA are mocked.
 */

#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int current_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do {                     \
    printf("  %-46s ", #name);                  \
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

#include "acl/acl_internal.h"

/* ------------------------------------------------------------------ */
/* Globals the module owns                                              */
/* ------------------------------------------------------------------ */

uid_t UID_$NIL = { 0, 0 };

acl_$cache_dir_t  ACL_$CACHE_DIR[ACL_CACHE_SLOTS];
acl_$cache_link_t ACL_$CACHE_LRU_LINKS[ACL_CACHE_LINK_SLOTS];
acl_$cache_link_t ACL_$CACHE_HASH_LINKS[ACL_CACHE_LINK_SLOTS];
int16_t ACL_$CACHE_HASH_BUCKETS_TAB[ACL_CACHE_HASH_BUCKETS];
int16_t ACL_$CACHE_FREE_HEAD;
int16_t ACL_$CACHE_LRU_HEAD;

/* ------------------------------------------------------------------ */
/* Mocks                                                                */
/* ------------------------------------------------------------------ */

static uint16_t mock_hash;
static uint16_t mock_hash_modulus_seen;
static int      mock_hash_calls;

uint32_t UID_$HASH(uid_t *uid, uint16_t *table_size)
{
    (void)uid;
    mock_hash_modulus_seen = *table_size;
    mock_hash_calls++;
    /* The real routine leaves quotient:remainder in D0; only the low word is
     * used, so put something non-zero in the high half to prove it. */
    return 0xDEAD0000u | mock_hash;
}

static int      mock_load_calls;
static int16_t  mock_load_result;
static status_$t mock_load_status;
static int8_t   mock_load_flag;

int16_t acl_$load_acl_image(uid_t *acl_uid, int8_t *cached_flag_ret,
                            acl_$prot_data_t *prot, status_$t *status_ret)
{
    (void)acl_uid; (void)prot;
    mock_load_calls++;
    *cached_flag_ret = mock_load_flag;
    *status_ret = mock_load_status;
    return mock_load_result;
}

static int mock_def_acldata_calls;

void ACL_$DEF_ACLDATA(void *acl_data_out, void *uid_out)
{
    acl_$prot_data_t *p = (acl_$prot_data_t *)acl_data_out;
    uid_t            *u = (uid_t *)uid_out;

    mock_def_acldata_calls++;
    memset(p, 0, sizeof(*p));
    p->owner_rights  = 0x10;
    p->group_rights  = 0x10;
    p->org_rights    = 0x10;
    p->world_rights  = 0x0F;
    p->subsys_rights = 0x00;
    u->high = 0;
    u->low  = 0;
}

/* ------------------------------------------------------------------ */
/* Fixtures                                                             */
/* ------------------------------------------------------------------ */

#define BUCKET 7

static uid_t            want;
static acl_$prot_data_t prot;
static int8_t           cached_flag;
static status_$t        status;

static void reset_world(void)
{
    int i;

    memset(ACL_$CACHE_DIR, 0, sizeof(ACL_$CACHE_DIR));
    memset(ACL_$CACHE_LRU_LINKS, 0, sizeof(ACL_$CACHE_LRU_LINKS));
    memset(ACL_$CACHE_HASH_LINKS, 0, sizeof(ACL_$CACHE_HASH_LINKS));
    for (i = 0; i < ACL_CACHE_HASH_BUCKETS; i++) {
        ACL_$CACHE_HASH_BUCKETS_TAB[i] = ACL_CACHE_NO_SLOT;
    }
    ACL_$CACHE_LRU_HEAD  = ACL_CACHE_NO_SLOT;
    ACL_$CACHE_FREE_HEAD = ACL_CACHE_NO_SLOT;

    memset(&prot, 0, sizeof(prot));
    cached_flag = 0x55;
    status = 0x7F7F7F7F;

    want.high = 0x0BAD0000u;
    want.low  = 0x0000F00Du;

    mock_hash = BUCKET;
    mock_hash_calls = 0;
    mock_hash_modulus_seen = 0;
    mock_load_calls = 0;
    mock_load_result = ACL_CACHE_NO_SLOT;
    mock_load_status = 0;
    mock_load_flag = 0;
    mock_def_acldata_calls = 0;
}

/* Build a circular hash chain of `n` slots in bucket BUCKET. */
static void chain(const int16_t *slots, int n)
{
    int i;

    ACL_$CACHE_HASH_BUCKETS_TAB[BUCKET] = slots[0];
    for (i = 0; i < n; i++) {
        ACL_$CACHE_HASH_LINKS[slots[i]].next = slots[(i + 1) % n];
        ACL_$CACHE_HASH_LINKS[slots[(i + 1) % n]].prev = slots[i];
    }
}

static int16_t run(void)
{
    return acl_$find_acl_slot(&want, &cached_flag, &prot, &status);
}

/* ------------------------------------------------------------------ */

TEST(hash_uses_the_modulus_cell_at_00e45e8c)
{
    reset_world();
    (void)run();
    ASSERT_EQ(1, mock_hash_calls);
    /* 0x00E45E8C holds the word 0x003D. */
    ASSERT_EQ(61, mock_hash_modulus_seen);
}

TEST(status_is_cleared_on_entry)
{
    reset_world();
    mock_load_status = 0;
    mock_load_result = ACL_CACHE_NO_SLOT;
    (void)run();
    /* `clr.l (A0)` at 0x00E45EA8, before anything else. */
    ASSERT_EQ(0, status);
}

TEST(empty_bucket_loads_and_links_the_new_slot)
{
    reset_world();
    mock_load_result = 5;
    ASSERT_EQ(5, run());
    ASSERT_EQ(1, mock_load_calls);
    /* Only the insert runs on a miss (0x00E45EF0 skips 0x00E45F48). */
    ASSERT_EQ(5, ACL_$CACHE_LRU_HEAD);
    ASSERT_EQ(5, ACL_$CACHE_LRU_LINKS[5].next);
    ASSERT_EQ(5, ACL_$CACHE_LRU_LINKS[5].prev);
}

TEST(load_failure_status_returns_without_touching_the_lru)
{
    reset_world();
    mock_load_result  = 5;
    mock_load_status  = 0x00230001;         /* low word non-zero */
    ASSERT_EQ(5, run());
    ASSERT_EQ(ACL_CACHE_NO_SLOT, ACL_$CACHE_LRU_HEAD);
}

TEST(load_status_high_half_alone_is_not_a_failure)
{
    reset_world();
    mock_load_result = 5;
    /* `tst.w (0x2,A0)` only looks at the LOW word. */
    mock_load_status = 0x12340000;
    ASSERT_EQ(5, run());
    ASSERT_EQ(5, ACL_$CACHE_LRU_HEAD);
}

TEST(load_returning_no_slot_skips_the_lru_insert)
{
    reset_world();
    mock_load_result = ACL_CACHE_NO_SLOT;
    ASSERT_EQ(ACL_CACHE_NO_SLOT, run());
    ASSERT_EQ(ACL_CACHE_NO_SLOT, ACL_$CACHE_LRU_HEAD);
}

TEST(hit_on_the_bucket_head_returns_its_slot)
{
    static const int16_t s[] = { 3 };
    reset_world();
    chain(s, 1);
    ACL_$CACHE_DIR[3].acl_uid = want;
    ACL_$CACHE_DIR[3].cached_flag = 0x21;
    ASSERT_EQ(3, run());
    ASSERT_EQ(0, mock_load_calls);
    ASSERT_EQ(0x21, cached_flag);           /* 0x00E45F10 */
    ASSERT_EQ(0, mock_def_acldata_calls);   /* flag >= 0 -> `bpl` */
}

TEST(hit_further_down_the_chain)
{
    static const int16_t s[] = { 3, 9, 11 };
    reset_world();
    chain(s, 3);
    ACL_$CACHE_DIR[3].acl_uid  = (uid_t){ 1, 1 };
    ACL_$CACHE_DIR[9].acl_uid  = (uid_t){ 2, 2 };
    ACL_$CACHE_DIR[11].acl_uid = want;
    ASSERT_EQ(11, run());
    ASSERT_EQ(0, mock_load_calls);
}

TEST(chain_wraparound_is_a_miss)
{
    static const int16_t s[] = { 3, 9, 11 };
    reset_world();
    chain(s, 3);
    ACL_$CACHE_DIR[3].acl_uid  = (uid_t){ 1, 1 };
    ACL_$CACHE_DIR[9].acl_uid  = (uid_t){ 2, 2 };
    ACL_$CACHE_DIR[11].acl_uid = (uid_t){ 3, 3 };
    mock_load_result = 20;
    /* 0x00E45F3E: `cmp.w (0xaf0,A0),D2w` back at the head -> `moveq #-1`. */
    ASSERT_EQ(20, run());
    ASSERT_EQ(1, mock_load_calls);
}

TEST(uid_compare_needs_both_halves)
{
    static const int16_t s[] = { 3 };
    reset_world();
    chain(s, 1);
    ACL_$CACHE_DIR[3].acl_uid.high = want.high;
    ACL_$CACHE_DIR[3].acl_uid.low  = want.low ^ 1u;
    mock_load_result = 20;
    ASSERT_EQ(20, run());
}

TEST(negative_cached_flag_rebuilds_the_default_prot_record)
{
    static const int16_t s[] = { 3 };
    reset_world();
    chain(s, 1);
    ACL_$CACHE_DIR[3].acl_uid       = want;
    ACL_$CACHE_DIR[3].cached_flag   = (int8_t)0xFF;
    ACL_$CACHE_DIR[3].world_rights  = 0x0A;
    ACL_$CACHE_DIR[3].subsys_rights = 0x0C;
    prot.owner_rights = 0xEE;               /* clobbered by ACL_$DEF_ACLDATA */

    ASSERT_EQ(3, run());
    ASSERT_EQ(1, mock_def_acldata_calls);
    ASSERT_EQ(0x10, prot.owner_rights);
    /* 0x00E45F24 / 0x00E45F2A overwrite the two the image really carries. */
    ASSERT_EQ(0x0A, prot.world_rights);
    ASSERT_EQ(0x0C, prot.subsys_rights);
    ASSERT_EQ((int8_t)0xFF, cached_flag);
}

TEST(a_hit_moves_the_slot_to_the_front_of_the_lru)
{
    static const int16_t s[] = { 3, 9 };
    reset_world();
    chain(s, 2);
    ACL_$CACHE_DIR[3].acl_uid = (uid_t){ 1, 1 };
    ACL_$CACHE_DIR[9].acl_uid = want;

    /* Seed the LRU list as 9 -> 3 (head 9) by inserting 3 then 9. */
    acl_$cache_list_insert(&ACL_$CACHE_LRU_HEAD, ACL_$CACHE_LRU_LINKS, 3);
    acl_$cache_list_insert(&ACL_$CACHE_LRU_HEAD, ACL_$CACHE_LRU_LINKS, 9);
    /* Touch 3 by looking it up instead. */
    ACL_$CACHE_DIR[3].acl_uid = want;
    ACL_$CACHE_DIR[9].acl_uid = (uid_t){ 1, 1 };
    ACL_$CACHE_HASH_BUCKETS_TAB[BUCKET] = 3;

    ASSERT_EQ(3, run());
    /* remove (0x00E45F54) then insert (0x00E45F68). */
    ASSERT_EQ(3, ACL_$CACHE_LRU_HEAD);
    ASSERT_EQ(9, ACL_$CACHE_LRU_LINKS[3].next);
    ASSERT_EQ(9, ACL_$CACHE_LRU_LINKS[3].prev);
}

/* ------------------------------------------------------------------ */
/* The list primitives themselves                                       */
/* ------------------------------------------------------------------ */

TEST(list_insert_builds_a_ring_and_remove_empties_it)
{
    int16_t head = ACL_CACHE_NO_SLOT;
    reset_world();

    acl_$cache_list_insert(&head, ACL_$CACHE_LRU_LINKS, 4);
    ASSERT_EQ(4, head);
    ASSERT_EQ(4, ACL_$CACHE_LRU_LINKS[4].next);
    ASSERT_EQ(4, ACL_$CACHE_LRU_LINKS[4].prev);

    acl_$cache_list_insert(&head, ACL_$CACHE_LRU_LINKS, 6);
    ASSERT_EQ(6, head);
    ASSERT_EQ(4, ACL_$CACHE_LRU_LINKS[6].next);
    ASSERT_EQ(4, ACL_$CACHE_LRU_LINKS[6].prev);
    ASSERT_EQ(6, ACL_$CACHE_LRU_LINKS[4].next);
    ASSERT_EQ(6, ACL_$CACHE_LRU_LINKS[4].prev);

    acl_$cache_list_remove(&head, ACL_$CACHE_LRU_LINKS, 6);
    ASSERT_EQ(4, head);                     /* 0x00E44CDC */
    acl_$cache_list_remove(&head, ACL_$CACHE_LRU_LINKS, 4);
    ASSERT_EQ(ACL_CACHE_NO_SLOT, head);     /* 0x00E44CD6 */
}

TEST(list_remove_on_an_empty_list_is_a_no_op)
{
    int16_t head = ACL_CACHE_NO_SLOT;
    reset_world();
    ACL_$CACHE_LRU_LINKS[4].next = 0x1234;
    acl_$cache_list_remove(&head, ACL_$CACHE_LRU_LINKS, 4);
    ASSERT_EQ(ACL_CACHE_NO_SLOT, head);     /* 0x00E44CA6 `beq` */
    ASSERT_EQ(0x1234, ACL_$CACHE_LRU_LINKS[4].next);
}

TEST(list_remove_of_a_non_head_leaves_the_head_alone)
{
    int16_t head = ACL_CACHE_NO_SLOT;
    reset_world();
    acl_$cache_list_insert(&head, ACL_$CACHE_LRU_LINKS, 1);
    acl_$cache_list_insert(&head, ACL_$CACHE_LRU_LINKS, 2);
    acl_$cache_list_insert(&head, ACL_$CACHE_LRU_LINKS, 3);
    acl_$cache_list_remove(&head, ACL_$CACHE_LRU_LINKS, 1);
    ASSERT_EQ(3, head);
    ASSERT_EQ(2, ACL_$CACHE_LRU_LINKS[3].next);
    ASSERT_EQ(3, ACL_$CACHE_LRU_LINKS[2].next);
}

int main(void)
{
    printf("acl_$find_acl_slot (0x00E45E8E) / acl_$cache_list_* (0x00E44C3C)\n");

    RUN_TEST(hash_uses_the_modulus_cell_at_00e45e8c);
    RUN_TEST(status_is_cleared_on_entry);
    RUN_TEST(empty_bucket_loads_and_links_the_new_slot);
    RUN_TEST(load_failure_status_returns_without_touching_the_lru);
    RUN_TEST(load_status_high_half_alone_is_not_a_failure);
    RUN_TEST(load_returning_no_slot_skips_the_lru_insert);
    RUN_TEST(hit_on_the_bucket_head_returns_its_slot);
    RUN_TEST(hit_further_down_the_chain);
    RUN_TEST(chain_wraparound_is_a_miss);
    RUN_TEST(uid_compare_needs_both_halves);
    RUN_TEST(negative_cached_flag_rebuilds_the_default_prot_record);
    RUN_TEST(a_hit_moves_the_slot_to_the_front_of_the_lru);
    RUN_TEST(list_insert_builds_a_ring_and_remove_empties_it);
    RUN_TEST(list_remove_on_an_empty_list_is_a_no_op);
    RUN_TEST(list_remove_of_a_non_head_leaves_the_head_alone);

    printf("\n%d tests, %d failed\n", tests_passed + tests_failed, tests_failed);
    return tests_failed != 0;
}

#include "../find_acl_slot.c"
#include "../cache_list.c"
