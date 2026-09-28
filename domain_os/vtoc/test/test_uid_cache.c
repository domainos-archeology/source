/*
 * vtoc/test/test_uid_cache.c - unit tests for vtoc_$uid_cache_insert
 * (0x00E382A8), vtoc_$uid_cache_lookup (0x00E38324) and vtoc_$hash_uid
 * (0x00E383B0)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

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

#include "vtoc/vtoc_internal.h"

vtoc_$data_t vtoc_$data;
vtoc_$uid_cache_bucket_t vtoc_$uid_cache[VTOC_UID_CACHE_BUCKETS];

static int      mock_hash_calls;
static uint32_t mock_hash_result;
static uint16_t *mock_hash_size_p;

uint32_t UID_$HASH(uid_t *uid, uint16_t *table_size)
{
    (void)uid;
    mock_hash_calls++;
    mock_hash_size_p = table_size;
    return mock_hash_result;
}

#include "vtoc/uid_cache.c"

#define VOL 2

static void reset_state(void)
{
    memset(&vtoc_$data, 0, sizeof(vtoc_$data));
    memset(vtoc_$uid_cache, 0, sizeof(vtoc_$uid_cache));
    mock_hash_calls = 0;
    mock_hash_result = 0x12345;
    mock_hash_size_p = 0;
}

/* bucket = (uid.high & 0xFFFF) % 101; first insert lands in entry 0 with
 * vol at +0x0C and age 0 (0x00E38304..0x00E38316) */
static void test_insert_uses_low_word_of_high_mod_101(void)
{
    uid_t uid = { 0xAAAA0065u, 0x11111111u };    /* 0x65 = 101 -> bucket 0 */
    uid_t uid2 = { 0xAAAA0066u, 0x22222222u };   /* bucket 1 */
    vtoc_$uid_cache_entry_t *e;

    vtoc_$uid_cache_insert(&uid, VOL, 0x1230);
    vtoc_$uid_cache_insert(&uid2, 5, 0x4560);

    e = &vtoc_$uid_cache[0].entries[0];
    ASSERT_EQ(0xAAAA0065u, e->uid.high);
    ASSERT_EQ(0x11111111u, e->uid.low);
    ASSERT_EQ(0x1230, e->block_info);
    ASSERT_EQ(VOL, e->vol);
    ASSERT_EQ(0, e->age);
    e = &vtoc_$uid_cache[1].entries[0];
    ASSERT_EQ(0x4560, e->block_info);
    ASSERT_EQ(5, e->vol);
}

/* a valid entry with the same UID is left alone (0x00E382EE) */
static void test_insert_existing_valid_uid_is_noop(void)
{
    uid_t uid = { 0x00000003u, 0x1u };
    vtoc_$uid_cache_entry_t *e = &vtoc_$uid_cache[3].entries[2];

    e->uid = uid;
    e->block_info = 0x999;
    e->vol = 7;
    e->age = 4;

    vtoc_$uid_cache_insert(&uid, VOL, 0x1230);

    ASSERT_EQ(0x999, e->block_info);
    ASSERT_EQ(7, e->vol);
    ASSERT_EQ(4, e->age);
    ASSERT_EQ(0, vtoc_$uid_cache[3].entries[0].vol);   /* nothing else written */
}

/* the entry with the strictly largest age is replaced; ties keep the
 * earliest (0x00E382F0 bcc) */
static void test_insert_replaces_oldest(void)
{
    uid_t uid = { 0x00000003u, 0x1u };
    vtoc_$uid_cache_bucket_t *b = &vtoc_$uid_cache[3];
    int i;

    for (i = 0; i < 4; i++) {
        b->entries[i].uid.high = 0x100 + i;
        b->entries[i].vol = 1;
    }
    b->entries[1].age = 9;
    b->entries[3].age = 9;

    vtoc_$uid_cache_insert(&uid, VOL, 0x1230);

    ASSERT_EQ(0x00000003u, b->entries[1].uid.high);
    ASSERT_EQ(0, b->entries[1].age);
    ASSERT_EQ(0x103, b->entries[3].uid.high);     /* the tie kept entry 1 */
}

/* all ages 0: the default victim is entry 0 */
static void test_insert_default_victim_is_entry_zero(void)
{
    uid_t uid = { 0x00000003u, 0x1u };
    vtoc_$uid_cache_bucket_t *b = &vtoc_$uid_cache[3];
    int i;

    for (i = 0; i < 4; i++) {
        b->entries[i].uid.high = 0x100 + i;
        b->entries[i].vol = 1;
    }
    vtoc_$uid_cache_insert(&uid, VOL, 0x1230);
    ASSERT_EQ(0x00000003u, b->entries[0].uid.high);
}

/* a hit copies vol -> *flags and block_info out, zeroes the age, ages
 * the other three, returns 0xFF (0x00E3835C..0x00E3839C) */
static void test_lookup_hit_refreshes_and_ages_others(void)
{
    uid_t uid = { 0x00000003u, 0x1u };
    vtoc_$uid_cache_bucket_t *b = &vtoc_$uid_cache[3];
    uint16_t flags = 0xDEAD;
    uint32_t info = 0xDEADBEEF;
    uint8_t r;

    b->entries[2].uid = uid;
    b->entries[2].block_info = 0x777;
    b->entries[2].vol = 6;
    b->entries[2].age = 5;
    b->entries[0].age = 0xFFFD;
    b->entries[1].age = 0xFFFE;
    b->entries[3].age = 0xFFFF;

    r = vtoc_$uid_cache_lookup(&uid, &flags, &info, 0);

    ASSERT_EQ(0xFF, r);
    ASSERT_EQ(6, flags);
    ASSERT_EQ(0x777, info);
    ASSERT_EQ(0, b->entries[2].age);
    ASSERT_EQ(6, b->entries[2].vol);
    ASSERT_EQ(0xFFFE, b->entries[0].age);         /* incremented */
    ASSERT_EQ(0xFFFE, b->entries[1].age);         /* saturated */
    ASSERT_EQ(0xFFFF, b->entries[3].age);         /* untouched */
}

/* a negative remove byte empties the entry: vol 0, age 0xFFFF
 * (`move.l #0xffff,(0xc,A0)` at 0x00E38376), still reporting the hit */
static void test_lookup_remove(void)
{
    uid_t uid = { 0x00000003u, 0x1u };
    vtoc_$uid_cache_entry_t *e = &vtoc_$uid_cache[3].entries[1];
    uint16_t flags = 0xDEAD;
    uint32_t info = 0;
    uint8_t r;

    e->uid = uid;
    e->block_info = 0x777;
    e->vol = 6;

    r = vtoc_$uid_cache_lookup(&uid, &flags, &info, (char)0xFF);

    ASSERT_EQ(0xFF, r);
    ASSERT_EQ(0, e->vol);
    ASSERT_EQ(0xFFFF, e->age);
    ASSERT_EQ(0, flags);                          /* read AFTER the clear */
    ASSERT_EQ(0x777, info);
}

/* an invalid (vol 0) entry with the same UID is a miss; outputs untouched */
static void test_lookup_miss(void)
{
    uid_t uid = { 0x00000003u, 0x1u };
    vtoc_$uid_cache_entry_t *e = &vtoc_$uid_cache[3].entries[1];
    uint16_t flags = 0xDEAD;
    uint32_t info = 0xBEEF;
    uint8_t r;

    e->uid = uid;
    e->block_info = 0x777;
    e->vol = 0;

    r = vtoc_$uid_cache_lookup(&uid, &flags, &info, 0);

    ASSERT_EQ(0, r);
    ASSERT_EQ(0xDEAD, flags);
    ASSERT_EQ(0xBEEF, info);
    ASSERT_EQ(1, e->age);
}

/* ---- vtoc_$hash_uid ---- */

static void setup_vol(uint16_t hash_type, uint16_t hash_size, int8_t format)
{
    vtoc_$vol_t *vol = VTOC_VOL(VOL);
    vol->hash_type = hash_type;
    vol->hash_size = hash_size;
    vol->parts[0].count = 4;  vol->parts[0].base = 0x1000;
    vol->parts[1].count = 3;  vol->parts[1].base = 0x2000;
    vol->parts[2].count = 0;  vol->parts[2].base = 0x3000;
    vtoc_$data.format[VOL] = format;
}

/* type 3, old format: hash = fold(uid.high) % size, resolved through the
 * partition table (0x00E38420..0x00E384AA) */
static void test_hash_type3_old_format(void)
{
    uid_t uid = { 0x00010005u, 0 };     /* fold = 1 ^ 5 = 4 */
    uint16_t bucket = 0xDEAD;
    uint32_t block = 0;
    status_$t status = 0x77;

    setup_vol(3, 100, 0);
    vtoc_$hash_uid(&uid, VOL, &bucket, &block, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(0xDEAD, bucket);                    /* not written, old format */
    ASSERT_EQ(0x2000, block);                     /* 4 - 4 = 0 into part 1 */
    ASSERT_EQ(0, mock_hash_calls);
}

/* type 2, new format: (uid.high*2 + sign(uid.low)) folded, mod size; the
 * low two bits are the bucket (0x00E383DC..0x00E38402, 0x00E38450) */
static void test_hash_type2_new_format(void)
{
    uid_t uid = { 0x00000009u, 0x80000000u };   /* 18 + 1 = 19 */
    uint16_t bucket = 0xDEAD;
    uint32_t block = 0;
    status_$t status = 0x77;

    setup_vol(2, 100, (int8_t)0xFF);
    vtoc_$hash_uid(&uid, VOL, &bucket, &block, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(3, bucket);                         /* 19 & 3 */
    ASSERT_EQ(0x2000, block);                     /* 19 >> 2 = 4 -> part 1 + 0 */
}

/* type 0: UID_$HASH(uid, &hash_size) supplies the hash */
static void test_hash_type0_uses_uid_hash(void)
{
    uid_t uid = { 1, 2 };
    uint16_t bucket = 0;
    uint32_t block = 0;
    status_$t status = 0x77;

    setup_vol(0, 50, 0);
    mock_hash_result = 0x00050002;                /* low word 2 */
    vtoc_$hash_uid(&uid, VOL, &bucket, &block, &status);

    ASSERT_EQ(1, mock_hash_calls);
    ASSERT_EQ((unsigned long)&VTOC_VOL(VOL)->hash_size, (unsigned long)mock_hash_size_p);
    ASSERT_EQ(0x1002, block);
}

/* past every partition: block stays 0 -> 0x80020002 (0x00E384B2) */
static void test_hash_beyond_partitions_is_mismatch(void)
{
    uid_t uid = { 0x00000007u, 0 };             /* fold = 7 */
    uint16_t bucket = 0;
    uint32_t block = 0x55;
    status_$t status = 0x77;

    setup_vol(3, 100, 0);
    vtoc_$hash_uid(&uid, VOL, &bucket, &block, &status);

    ASSERT_EQ((uint32_t)status_$VTOC_uid_mismatch, (uint32_t)status);
    ASSERT_EQ(0, block);
}

int main(void)
{
    printf("vtoc uid cache / hash tests:\n");
    RUN_TEST(insert_uses_low_word_of_high_mod_101);
    RUN_TEST(insert_existing_valid_uid_is_noop);
    RUN_TEST(insert_replaces_oldest);
    RUN_TEST(insert_default_victim_is_entry_zero);
    RUN_TEST(lookup_hit_refreshes_and_ages_others);
    RUN_TEST(lookup_remove);
    RUN_TEST(lookup_miss);
    RUN_TEST(hash_type3_old_format);
    RUN_TEST(hash_type2_new_format);
    RUN_TEST(hash_type0_uses_uid_hash);
    RUN_TEST(hash_beyond_partitions_is_mismatch);
    printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
