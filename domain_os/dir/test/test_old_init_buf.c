/*
 * dir/test/test_old_init_buf.c - Unit tests for dir_$old_init_buf
 *
 * Tests the old-format directory buffer initialization function.
 * dir_$old_init_buf is essentially a pure function (only reads UID_$NIL),
 * so we provide the global and test directly.
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ_BYTE(expected, actual) do { \
    if ((uint8_t)(expected) != (uint8_t)(actual)) { \
        printf("FAILED\n    Expected: 0x%02x, Got: 0x%02x at line %d\n", \
               (unsigned)(uint8_t)(expected), (unsigned)(uint8_t)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* Provide the uid_t type and UID_$NIL global */
typedef struct {
    uint32_t high;
    uint32_t low;
} uid_t;

uid_t UID_$NIL = { 0, 0 };

/* Provide status_$t stub */
typedef uint32_t status_$t;

/* Prevent real headers from being included */
#define DIR_INTERNAL_H
#define DIR_H
#define BASE_H

/* Provide UID_$NIL-dependent stubs that the source needs */

/* Pull in the implementation directly */
#include "../old_init_buf.c"

/* Buffer size large enough for the full old-format directory buffer */
#define DIR_OLD_BUF_SIZE 0x400

/* Test: header fields are written correctly (5 uint16_t values at offset 0x00) */
TEST(header_fields)
{
    uint8_t buf[DIR_OLD_BUF_SIZE];
    memset(buf, 0xFF, sizeof(buf));

    dir_$old_init_buf(buf);

    /* version = 1 */
    ASSERT_EQ(1, *(uint16_t *)(buf + 0x00));
    /* num_hash_buckets = 43 */
    ASSERT_EQ(43, *(uint16_t *)(buf + 0x02));
    /* max_inline_entries = 18 */
    ASSERT_EQ(18, *(uint16_t *)(buf + 0x04));
    /* field_06 = 0x01AD */
    ASSERT_EQ(0x01AD, *(uint16_t *)(buf + 0x06));
    /* field_08 = 3 */
    ASSERT_EQ(3, *(uint16_t *)(buf + 0x08));
}

/* Test: parent UID set to UID_$NIL at offsets 0x0E and 0x12 */
TEST(parent_uid_nil)
{
    uint8_t buf[DIR_OLD_BUF_SIZE];
    memset(buf, 0xFF, sizeof(buf));

    dir_$old_init_buf(buf);

    ASSERT_EQ(0, *(uint32_t *)(buf + 0x0E));
    ASSERT_EQ(0, *(uint32_t *)(buf + 0x12));
}

/* Test: capacity set to 0x514 at offset 0x16 */
TEST(capacity)
{
    uint8_t buf[DIR_OLD_BUF_SIZE];
    memset(buf, 0xFF, sizeof(buf));

    dir_$old_init_buf(buf);

    ASSERT_EQ(0x514, *(uint32_t *)(buf + 0x16));
}

/* Test: entry count cleared at offset 0x0A */
TEST(entry_count_cleared)
{
    uint8_t buf[DIR_OLD_BUF_SIZE];
    memset(buf, 0xFF, sizeof(buf));

    dir_$old_init_buf(buf);

    ASSERT_EQ(0, *(uint32_t *)(buf + 0x0A));
}

/* Test: hash table entries are cleared (43 words at offset 0x3AA) */
TEST(hash_table_cleared)
{
    uint8_t buf[DIR_OLD_BUF_SIZE];
    memset(buf, 0xFF, sizeof(buf));

    dir_$old_init_buf(buf);

    for (int i = 0; i < 43; i++) {
        uint16_t val = *(uint16_t *)(buf + 0x3AA + i * 2);
        if (val != 0) {
            printf("FAILED\n    Hash entry %d: expected 0x0000, got 0x%04x at line %d\n",
                   i, val, __LINE__);
            tests_failed++;
            return;
        }
    }
}

/* Test: active flag byte cleared in all 18 inline entry slots */
TEST(entry_active_flags_cleared)
{
    uint8_t buf[DIR_OLD_BUF_SIZE];
    memset(buf, 0xFF, sizeof(buf));

    dir_$old_init_buf(buf);

    for (int i = 0; i < 18; i++) {
        uint8_t val = buf[0x30 + i * 0x30 + 0x11];
        if (val != 0) {
            printf("FAILED\n    Entry %d active flag: expected 0x00, got 0x%02x at line %d\n",
                   i, val, __LINE__);
            tests_failed++;
            return;
        }
    }
}

/* Test: initialized flag set to 1 at offset 0x37A */
TEST(initialized_flag)
{
    uint8_t buf[DIR_OLD_BUF_SIZE];
    memset(buf, 0xFF, sizeof(buf));

    dir_$old_init_buf(buf);

    ASSERT_EQ_BYTE(1, buf[0x37A]);
}

/* Test: reserved byte cleared at offset 0x37B */
TEST(reserved_37b_cleared)
{
    uint8_t buf[DIR_OLD_BUF_SIZE];
    memset(buf, 0xFF, sizeof(buf));

    dir_$old_init_buf(buf);

    ASSERT_EQ_BYTE(0, buf[0x37B]);
}

/* Test: info block header cleared (4 bytes at 0x37C + 2 bytes at 0x380) */
TEST(info_block_header_cleared)
{
    uint8_t buf[DIR_OLD_BUF_SIZE];
    memset(buf, 0xFF, sizeof(buf));

    dir_$old_init_buf(buf);

    /* info_write_len and info_read_len */
    ASSERT_EQ(0, *(uint32_t *)(buf + 0x37C));
    /* field_380 */
    ASSERT_EQ(0, *(uint16_t *)(buf + 0x380));
}

/* Test: info block data area cleared (40 bytes at 0x382-0x3A9) */
TEST(info_block_data_cleared)
{
    uint8_t buf[DIR_OLD_BUF_SIZE];
    memset(buf, 0xFF, sizeof(buf));

    dir_$old_init_buf(buf);

    for (int i = 0; i < 40; i++) {
        uint8_t val = buf[0x382 + i];
        if (val != 0) {
            printf("FAILED\n    Info data byte %d (offset 0x%03x): expected 0x00, got 0x%02x at line %d\n",
                   i, 0x382 + i, val, __LINE__);
            tests_failed++;
            return;
        }
    }
}

/* Test: bytes NOT touched by init_buf are still 0xFF (the function only
 * initializes specific fields, not the entire buffer) */
TEST(untouched_bytes_preserved)
{
    uint8_t buf[DIR_OLD_BUF_SIZE];
    memset(buf, 0xFF, sizeof(buf));

    dir_$old_init_buf(buf);

    /* Bytes 0x1A-0x2F are in the gap and should be untouched */
    for (int i = 0x1A; i < 0x30; i++) {
        if (buf[i] != 0xFF) {
            printf("FAILED\n    Gap byte at offset 0x%02x: expected 0xFF, got 0x%02x at line %d\n",
                   i, buf[i], __LINE__);
            tests_failed++;
            return;
        }
    }

    /* Entry slot bytes other than the active flag should be untouched.
     * Check offset 0x10 (just before active flag) in first entry */
    ASSERT_EQ_BYTE(0xFF, buf[0x30 + 0x10]);
    /* Check offset 0x12 (just after active flag) in first entry */
    ASSERT_EQ_BYTE(0xFF, buf[0x30 + 0x12]);
}

/* Test: info block data uses 1-indexed Pascal convention.
 * The data loop clears indices 1..40 (offsets 0x382..0x3A9).
 * Byte 0x381 is the low byte of the clr.w at 0x380 so it IS zeroed
 * by the word clear, but is NOT part of the info data loop. */
TEST(pascal_one_indexed_info)
{
    uint8_t buf[DIR_OLD_BUF_SIZE];
    memset(buf, 0xFF, sizeof(buf));

    dir_$old_init_buf(buf);

    /* 0x380-0x381 are cleared by the word clear at 0x380 */
    ASSERT_EQ_BYTE(0x00, buf[0x380]);
    ASSERT_EQ_BYTE(0x00, buf[0x381]);
    /* 0x382 is the first byte cleared by the info data loop */
    ASSERT_EQ_BYTE(0x00, buf[0x382]);
    /* 0x3A9 is the last byte cleared by the info data loop (index 40) */
    ASSERT_EQ_BYTE(0x00, buf[0x3A9]);
    /* 0x3AA is the start of the hash table, also cleared */
    ASSERT_EQ(0, *(uint16_t *)(buf + 0x3AA));
}

int main(void)
{
    printf("dir_$old_init_buf tests:\n");

    RUN_TEST(header_fields);
    RUN_TEST(parent_uid_nil);
    RUN_TEST(capacity);
    RUN_TEST(entry_count_cleared);
    RUN_TEST(hash_table_cleared);
    RUN_TEST(entry_active_flags_cleared);
    RUN_TEST(initialized_flag);
    RUN_TEST(reserved_37b_cleared);
    RUN_TEST(info_block_header_cleared);
    RUN_TEST(info_block_data_cleared);
    RUN_TEST(untouched_bytes_preserved);
    RUN_TEST(pascal_one_indexed_info);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
