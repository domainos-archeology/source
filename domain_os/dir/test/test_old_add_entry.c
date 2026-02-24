/*
 * dir/test/test_old_add_entry.c - Unit tests for dir_$old_add_entry
 *
 * Tests the old-format directory entry addition function by mocking
 * the helper functions (dir_$old_find_entry, dir_$old_find_free_inline_slot,
 * dir_$old_hash_name, dir_$old_find_overflow_slot).
 *
 * On 64-bit hosts, uint32_t cannot hold a real pointer, so we provide
 * a test-local copy of the function body using uintptr_t for the handle
 * parameter. The logic is identical; only the handle width differs.
 */

/* Suppress POSIX uid_t so we can define Apollo's uid_t struct */
#define uid_t posix_uid_t
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#undef uid_t

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

/* Provide the Apollo uid_t type */
typedef struct {
    uint32_t high;
    uint32_t low;
} uid_t;

/* Provide status_$t */
typedef uint32_t status_$t;
#define status_$ok                  0
#define status_$name_already_exists 0xE0003
#define status_$directory_is_full   0xE0002

/*
 * Use uintptr_t for handle to allow testing on 64-bit hosts.
 * On the m68k target, uintptr_t == uint32_t so this is equivalent.
 */
typedef uintptr_t test_handle_t;

/*
 * Mock control variables
 */

static int8_t mock_find_entry_result;
static int mock_find_entry_called;

static int8_t dir_$old_find_entry(test_handle_t handle, uint8_t *name,
                                   uint16_t name_len, int32_t *entry_ret,
                                   uint16_t *slot_idx, uint16_t *chain_level)
{
    mock_find_entry_called = 1;
    (void)handle; (void)name; (void)name_len;
    (void)slot_idx; (void)chain_level;
    return mock_find_entry_result;
}

static int8_t mock_find_inline_result;
static uint16_t mock_find_inline_slot;
static int mock_find_inline_called;

static int8_t dir_$old_find_free_inline_slot(test_handle_t handle,
                                              uint16_t inline_count,
                                              uint16_t *slot_out)
{
    mock_find_inline_called = 1;
    *slot_out = mock_find_inline_slot;
    (void)handle; (void)inline_count;
    return mock_find_inline_result;
}

static uint16_t mock_hash_result;
static int mock_hash_called;

static uint16_t dir_$old_hash_name(uint8_t *name, uint16_t name_len,
                                    uint16_t num_buckets)
{
    mock_hash_called = 1;
    (void)name; (void)name_len; (void)num_buckets;
    return mock_hash_result;
}

static int8_t mock_find_overflow_result;
static uint16_t mock_find_overflow_bucket;
static uint16_t mock_find_overflow_sub_slot;
static int mock_find_overflow_called;

static int8_t dir_$old_find_overflow_slot(test_handle_t handle, uint16_t hash,
                                           int8_t flags, uint16_t *bucket_out,
                                           uint16_t *sub_slot_out)
{
    mock_find_overflow_called = 1;
    *bucket_out = mock_find_overflow_bucket;
    *sub_slot_out = mock_find_overflow_sub_slot;
    (void)handle; (void)hash; (void)flags;
    return mock_find_overflow_result;
}

static void reset_mocks(void)
{
    mock_find_entry_result = 0;
    mock_find_entry_called = 0;
    mock_find_inline_result = 0;
    mock_find_inline_slot = 0;
    mock_find_inline_called = 0;
    mock_hash_result = 0;
    mock_hash_called = 0;
    mock_find_overflow_result = 0;
    mock_find_overflow_bucket = 0;
    mock_find_overflow_sub_slot = 0;
    mock_find_overflow_called = 0;
}

/*
 * Test-local implementation of dir_$old_add_entry.
 *
 * This is the same logic as the production code in old_add_entry.c,
 * but with test_handle_t (uintptr_t) instead of uint32_t for the
 * handle parameter, so it works on 64-bit hosts where pointers
 * don't fit in 32 bits.
 */
static void dir_$old_add_entry(uid_t *dir_uid, test_handle_t handle,
                                uint8_t *name, uint16_t name_len,
                                uint16_t type, void *uid_data,
                                uint16_t flags, uint8_t *result,
                                status_$t *status_ret)
{
    char *dir_base = (char *)handle;
    uint32_t *uid_ptr = (uint32_t *)uid_data;
    int8_t found;
    uint16_t slot_idx;
    uint16_t chain_level;
    uint16_t hash;
    int8_t flag_byte = (int8_t)(flags >> 8);

    *(uintptr_t *)result = 0;

    found = dir_$old_find_entry(handle, name, name_len,
                                (int32_t *)result, &slot_idx, &chain_level);
    if (found < 0) {
        *status_ret = status_$name_already_exists;
        return;
    }

    if (flag_byte >= 0 &&
        *(int16_t *)(dir_base + 0x16) == *(int16_t *)(dir_base + 0x18)) {
        *status_ret = status_$directory_is_full;
        return;
    }

    found = dir_$old_find_free_inline_slot(handle,
                *(uint16_t *)(dir_base + 0x04), &slot_idx);

    if (found < 0) {
        char *entry = dir_base + (uint32_t)slot_idx * 0x30;
        uint16_t i;

        *(uint8_t *)(entry + 0x11) = (uint8_t)type;
        *(uint32_t *)(entry + 0x12) = uid_ptr[0];
        *(uint32_t *)(entry + 0x16) = uid_ptr[1];

        if (name_len != 0) {
            int16_t count = name_len - 1;
            i = 1;
            do {
                *(uint8_t *)(entry - 0x17 + (uint32_t)i) =
                    *(uint8_t *)(name - 1 + (uint32_t)i);
                i++;
                count--;
            } while (count != -1);
        }

        i = name_len + 1;
        if (i < 0x21) {
            int16_t pad_count = 0x20 - i;
            do {
                *(uint8_t *)(entry - 0x17 + (uint32_t)i) = 0x20;
                i++;
                pad_count--;
            } while (pad_count != -1);
        }

        *(uint8_t *)(entry + 0x10) = (uint8_t)name_len;
        *status_ret = status_$ok;
        *(int16_t *)(dir_base + 0x16) += 1;
        *(uintptr_t *)result = (uintptr_t)(dir_base - 0x16 +
                                (uint32_t)slot_idx * 0x30);
        return;
    }

    hash = dir_$old_hash_name(name, name_len, *(uint16_t *)(dir_base + 0x02));

    found = dir_$old_find_overflow_slot(handle, hash, flag_byte,
                                         &slot_idx, &chain_level);

    if (found < 0) {
        char *bucket = dir_base + (uint32_t)slot_idx * 0x96;
        char *sub_entry = bucket + (uint32_t)chain_level * 0x30;
        uint16_t i;

        *(uint8_t *)(sub_entry + 0x367) = (uint8_t)type;
        *(uint8_t *)(bucket + 0x36e) += 1;
        *(uint32_t *)(sub_entry + 0x368) = uid_ptr[0];
        *(uint32_t *)(sub_entry + 0x36c) = uid_ptr[1];

        if (name_len != 0) {
            int16_t count = name_len - 1;
            i = 1;
            do {
                *(uint8_t *)(sub_entry + (uint32_t)i + 0x33f) =
                    *(uint8_t *)(name - 1 + (uint32_t)i);
                i++;
                count--;
            } while (count != -1);
        }

        i = name_len + 1;
        if (i < 0x21) {
            int16_t pad_count = 0x20 - i;
            do {
                *(uint8_t *)(sub_entry + (uint32_t)i + 0x33f) = 0x20;
                i++;
                pad_count--;
            } while (pad_count != -1);
        }

        *(uint8_t *)(sub_entry + 0x366) = (uint8_t)name_len;
        *status_ret = status_$ok;
        *(int16_t *)(dir_base + 0x16) += 1;
        *(uintptr_t *)result = (uintptr_t)(sub_entry + 0x340);
        return;
    }

    *status_ret = status_$directory_is_full;
}

/*
 * Test buffer
 */
#define DIR_BUF_SIZE 0x10000
static uint8_t dir_buf[DIR_BUF_SIZE];

static test_handle_t handle_val(void)
{
    return (test_handle_t)(uintptr_t)dir_buf;
}

static void setup_dir_buf(void)
{
    memset(dir_buf, 0, sizeof(dir_buf));
    *(uint16_t *)(dir_buf + 0x04) = 18;    /* inline_count */
    *(uint16_t *)(dir_buf + 0x02) = 43;    /* num_hash_buckets */
    *(int16_t *)(dir_buf + 0x16) = 0;      /* entry_count */
    *(int16_t *)(dir_buf + 0x18) = 0x514;  /* max_entry_count */
    *(int16_t *)(dir_buf + 0x08) = 3;      /* overflow_entries_per_bucket */
}

/* Test: duplicate name returns status_$name_already_exists */
TEST(duplicate_name)
{
    setup_dir_buf();
    reset_mocks();
    mock_find_entry_result = (int8_t)0xFF;

    uid_t dir_uid = {0x1234, 0x5678};
    uint8_t name[] = "test";
    uint32_t uid_data[2] = {0xAABBCCDD, 0x11223344};
    uintptr_t result = 0;
    status_$t status = 0;

    dir_$old_add_entry(&dir_uid, handle_val(), name, 4, 1,
                       uid_data, 0, (uint8_t *)&result, &status);

    ASSERT_EQ(status_$name_already_exists, status);
    ASSERT_EQ(1, mock_find_entry_called);
}

/* Test: directory full when flags >= 0 and entry_count == max */
TEST(directory_full_no_replace)
{
    setup_dir_buf();
    reset_mocks();
    mock_find_entry_result = 0;

    *(int16_t *)(dir_buf + 0x16) = 0x514;
    *(int16_t *)(dir_buf + 0x18) = 0x514;

    uid_t dir_uid = {0, 0};
    uint8_t name[] = "test";
    uint32_t uid_data[2] = {0, 0};
    uintptr_t result = 0;
    status_$t status = 0;

    dir_$old_add_entry(&dir_uid, handle_val(), name, 4, 1,
                       uid_data, 0, (uint8_t *)&result, &status);

    ASSERT_EQ(status_$directory_is_full, status);
    ASSERT_EQ(0, mock_find_inline_called);
}

/* Test: successful inline entry addition */
TEST(inline_add_success)
{
    setup_dir_buf();
    reset_mocks();
    mock_find_entry_result = 0;
    mock_find_inline_result = (int8_t)0xFF;
    mock_find_inline_slot = 2;

    uid_t dir_uid = {0, 0};
    uint8_t name[] = "HELLO";
    uint32_t uid_data[2] = {0xDEADBEEF, 0xCAFEBABE};
    uintptr_t result = 0;
    status_$t status = 0xFFFF;

    dir_$old_add_entry(&dir_uid, handle_val(), name, 5, 7,
                       uid_data, 0, (uint8_t *)&result, &status);

    ASSERT_EQ(status_$ok, status);

    /* Entry at slot 2: offset = 2 * 0x30 = 0x60 */
    char *entry = (char *)dir_buf + 0x60;

    ASSERT_EQ_BYTE(5, *(uint8_t *)(entry + 0x10));  /* name_len */
    ASSERT_EQ_BYTE(7, *(uint8_t *)(entry + 0x11));  /* type */
    ASSERT_EQ(0xDEADBEEF, *(uint32_t *)(entry + 0x12));  /* uid high */
    ASSERT_EQ(0xCAFEBABE, *(uint32_t *)(entry + 0x16));  /* uid low */

    /* Name bytes at entry - 0x16 */
    ASSERT_EQ_BYTE('H', *(uint8_t *)(entry - 0x16));
    ASSERT_EQ_BYTE('E', *(uint8_t *)(entry - 0x15));
    ASSERT_EQ_BYTE('L', *(uint8_t *)(entry - 0x14));
    ASSERT_EQ_BYTE('L', *(uint8_t *)(entry - 0x13));
    ASSERT_EQ_BYTE('O', *(uint8_t *)(entry - 0x12));

    /* Space padding from position 6 through 32 */
    for (int i = 6; i <= 0x20; i++) {
        ASSERT_EQ_BYTE(0x20, *(uint8_t *)(entry - 0x17 + i));
    }

    ASSERT_EQ(1, *(int16_t *)(dir_buf + 0x16));  /* entry count */
    ASSERT_EQ((uintptr_t)(dir_buf + 0x4A), result);  /* result ptr */
}

/* Test: successful overflow entry addition */
TEST(overflow_add_success)
{
    setup_dir_buf();
    reset_mocks();
    mock_find_entry_result = 0;
    mock_find_inline_result = 0;
    mock_hash_result = 5;
    mock_find_overflow_result = (int8_t)0xFF;
    mock_find_overflow_bucket = 3;
    mock_find_overflow_sub_slot = 1;

    uid_t dir_uid = {0, 0};
    uint8_t name[] = "AB";
    uint32_t uid_data[2] = {0x11111111, 0x22222222};
    uintptr_t result = 0;
    status_$t status = 0xFFFF;

    dir_$old_add_entry(&dir_uid, handle_val(), name, 2, 4,
                       uid_data, 0, (uint8_t *)&result, &status);

    ASSERT_EQ(status_$ok, status);

    char *bucket = (char *)dir_buf + 3 * 0x96;
    char *sub_entry = bucket + 0x30;

    ASSERT_EQ_BYTE(2, *(uint8_t *)(sub_entry + 0x366));  /* name_len */
    ASSERT_EQ_BYTE(4, *(uint8_t *)(sub_entry + 0x367));  /* type */
    ASSERT_EQ(0x11111111, *(uint32_t *)(sub_entry + 0x368));  /* uid high */
    ASSERT_EQ(0x22222222, *(uint32_t *)(sub_entry + 0x36c));  /* uid low */

    /* Name at sub_entry + 0x340 */
    ASSERT_EQ_BYTE('A', *(uint8_t *)(sub_entry + 0x340));
    ASSERT_EQ_BYTE('B', *(uint8_t *)(sub_entry + 0x341));

    /* Space padding */
    for (int i = 3; i <= 0x20; i++) {
        ASSERT_EQ_BYTE(0x20, *(uint8_t *)(sub_entry + 0x33f + i));
    }

    ASSERT_EQ_BYTE(1, *(uint8_t *)(bucket + 0x36e));  /* chain count */
    ASSERT_EQ(1, *(int16_t *)(dir_buf + 0x16));  /* entry count */
    ASSERT_EQ((uintptr_t)(sub_entry + 0x340), result);
    ASSERT_EQ(1, mock_hash_called);
}

/* Test: no inline and no overflow returns directory full */
TEST(all_full)
{
    setup_dir_buf();
    reset_mocks();
    mock_find_entry_result = 0;
    mock_find_inline_result = 0;
    mock_find_overflow_result = 0;

    uid_t dir_uid = {0, 0};
    uint8_t name[] = "test";
    uint32_t uid_data[2] = {0, 0};
    uintptr_t result = 0;
    status_$t status = 0;

    dir_$old_add_entry(&dir_uid, handle_val(), name, 4, 1,
                       uid_data, 0, (uint8_t *)&result, &status);

    ASSERT_EQ(status_$directory_is_full, status);
    ASSERT_EQ(1, mock_find_inline_called);
    ASSERT_EQ(1, mock_find_overflow_called);
}

/* Test: result pointer is cleared even on error */
TEST(result_cleared_on_error)
{
    setup_dir_buf();
    reset_mocks();
    mock_find_entry_result = (int8_t)0xFF;

    uid_t dir_uid = {0, 0};
    uint8_t name[] = "test";
    uint32_t uid_data[2] = {0, 0};
    uintptr_t result = 0x12345678;
    status_$t status = 0;

    dir_$old_add_entry(&dir_uid, handle_val(), name, 4, 1,
                       uid_data, 0, (uint8_t *)&result, &status);

    ASSERT_EQ(0, result);
}

/* Test: empty name (name_len == 0) - all space-padded */
TEST(empty_name)
{
    setup_dir_buf();
    reset_mocks();
    mock_find_entry_result = 0;
    mock_find_inline_result = (int8_t)0xFF;
    mock_find_inline_slot = 1;

    uid_t dir_uid = {0, 0};
    uint8_t name[] = "";
    uint32_t uid_data[2] = {0xAAAAAAAA, 0xBBBBBBBB};
    uintptr_t result = 0;
    status_$t status = 0;

    dir_$old_add_entry(&dir_uid, handle_val(), name, 0, 2,
                       uid_data, 0, (uint8_t *)&result, &status);

    ASSERT_EQ(status_$ok, status);

    char *entry = (char *)dir_buf + 0x30;
    ASSERT_EQ_BYTE(0, *(uint8_t *)(entry + 0x10));
    ASSERT_EQ_BYTE(0x20, *(uint8_t *)(entry - 0x16));
    ASSERT_EQ_BYTE(0x20, *(uint8_t *)(entry + 0x03));
}

/* Test: max-length name (32 chars) - no space padding */
TEST(max_length_name)
{
    setup_dir_buf();
    reset_mocks();
    mock_find_entry_result = 0;
    mock_find_inline_result = (int8_t)0xFF;
    mock_find_inline_slot = 1;

    uid_t dir_uid = {0, 0};
    uint8_t name[33];
    memset(name, 'X', 32);
    name[32] = 0;
    uint32_t uid_data[2] = {0, 0};
    uintptr_t result = 0;
    status_$t status = 0;

    dir_$old_add_entry(&dir_uid, handle_val(), name, 32, 1,
                       uid_data, 0, (uint8_t *)&result, &status);

    ASSERT_EQ(status_$ok, status);

    char *entry = (char *)dir_buf + 0x30;
    for (int i = 0; i < 32; i++) {
        ASSERT_EQ_BYTE('X', *(uint8_t *)(entry - 0x16 + i));
    }
}

/* Test: replace mode (flags high byte < 0) bypasses entry_count == max check */
TEST(replace_mode_bypasses_full_check)
{
    setup_dir_buf();
    reset_mocks();
    mock_find_entry_result = 0;
    mock_find_inline_result = (int8_t)0xFF;
    mock_find_inline_slot = 1;

    *(int16_t *)(dir_buf + 0x16) = 0x514;
    *(int16_t *)(dir_buf + 0x18) = 0x514;

    uid_t dir_uid = {0, 0};
    uint8_t name[] = "test";
    uint32_t uid_data[2] = {0, 0};
    uintptr_t result = 0;
    status_$t status = 0;

    dir_$old_add_entry(&dir_uid, handle_val(), name, 4, 1,
                       uid_data, 0x8000, (uint8_t *)&result, &status);

    ASSERT_EQ(status_$ok, status);
    ASSERT_EQ(1, mock_find_inline_called);
}

int main(void)
{
    printf("dir_$old_add_entry tests:\n");

    RUN_TEST(duplicate_name);
    RUN_TEST(directory_full_no_replace);
    RUN_TEST(inline_add_success);
    RUN_TEST(overflow_add_success);
    RUN_TEST(all_full);
    RUN_TEST(result_cleared_on_error);
    RUN_TEST(empty_name);
    RUN_TEST(max_length_name);
    RUN_TEST(replace_mode_bypasses_full_check);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
