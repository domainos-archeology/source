/*
 * audit/test/test_log_dir_op.c - Unit tests for AUDIT_$LOG_DIR_OP (0x00E4BE16)
 *
 * AUDIT_$LOG_EVENT and OS_$DATA_COPY are mocked; the tests pin
 *   - the event header {4, subtype, 0}                    (0x00E4BE26-0x00E4BE30)
 *   - the signed clamp of name_len                        (0x00E4BE34-0x00E4BE3A)
 *   - the UID copies and the NUL at name[len]             (0x00E4BE46-0x00E4BE76)
 *   - data_len = 0x10 + len + 1                           (0x00E4BE7A-0x00E4BE88)
 *   - the success flag from the by-value status           (0x00E4BE8C-0x00E4BE98)
 *   - the copy is skipped for a zero length               (0x00E4BE5A)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

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

/* Prevent inclusion of kernel headers */
#define BASE_H
#define AUDIT_H
#define AUDIT_INTERNAL_H
#define OS_H

typedef uint32_t status_$t;
typedef struct { uint32_t high; uint32_t low; } apollo_uid_t;
#define uid_t apollo_uid_t

#define status_$ok 0

/* ---- AUDIT_$LOG_EVENT mock ---- */
static int      log_event_call_count;
static uint16_t captured_event_class;
static uint16_t captured_event_subtype;
static uint32_t captured_event_reserved;
static uint16_t captured_success_flag;
static uint32_t captured_status;
static uint8_t  captured_event_data[0x110];
static uint16_t captured_data_len;

void AUDIT_$LOG_EVENT(uid_t *event_uid, uint16_t *event_flags,
                      status_$t *status, char *data, const uint16_t *data_len)
{
    uint16_t *hdr = (uint16_t *)event_uid;

    log_event_call_count++;
    captured_event_class    = hdr[0];
    captured_event_subtype  = hdr[1];
    captured_event_reserved = ((uint32_t *)event_uid)[1];
    captured_success_flag   = *event_flags;
    captured_status         = *status;
    captured_data_len       = *data_len;
    memcpy(captured_event_data, data, sizeof(captured_event_data));
}

/* ---- OS_$DATA_COPY mock: records the longword length it was given ---- */
static int      copy_call_count;
static uint32_t copy_len_seen;

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    copy_call_count++;
    copy_len_seen = len;
    memcpy(dst, src, len);
}

static void reset_mocks(void)
{
    log_event_call_count = 0;
    captured_event_class = 0;
    captured_event_subtype = 0;
    captured_event_reserved = 0;
    captured_success_flag = 0xFFFF;
    captured_status = 0xFFFFFFFF;
    captured_data_len = 0;
    memset(captured_event_data, 0xAA, sizeof(captured_event_data));
    copy_call_count = 0;
    copy_len_seen = 0xFFFFFFFF;
}

#include "../log_dir_op.c"

static uid_t dir_uid  = { 0x11111111, 0x22222222 };
static uid_t file_uid = { 0x33333333, 0x44444444 };

TEST(header_and_uids)
{
    reset_mocks();
    AUDIT_$LOG_DIR_OP(0x12, 0, &dir_uid, &file_uid, 0, "");

    ASSERT_EQ(1, log_event_call_count);
    ASSERT_EQ(4, captured_event_class);
    ASSERT_EQ(0x12, captured_event_subtype);
    ASSERT_EQ(0, captured_event_reserved);

    uint32_t w;
    memcpy(&w, &captured_event_data[0x00], 4); ASSERT_EQ(0x11111111, w);
    memcpy(&w, &captured_event_data[0x04], 4); ASSERT_EQ(0x22222222, w);
    memcpy(&w, &captured_event_data[0x08], 4); ASSERT_EQ(0x33333333, w);
    memcpy(&w, &captured_event_data[0x0C], 4); ASSERT_EQ(0x44444444, w);
}

TEST(name_copied_and_terminated)
{
    reset_mocks();
    AUDIT_$LOG_DIR_OP(0x13, 0, &dir_uid, &file_uid, 5, "hello-world");

    ASSERT_EQ(1, copy_call_count);
    ASSERT_EQ(5, copy_len_seen);                     /* longword count */
    ASSERT_EQ(0, memcmp(&captured_event_data[0x10], "hello", 5));
    ASSERT_EQ(0, captured_event_data[0x15]);         /* NUL at name[len] */
    ASSERT_EQ(0x10 + 5 + 1, captured_data_len);
}

TEST(zero_length_skips_copy)
{
    reset_mocks();
    AUDIT_$LOG_DIR_OP(0x12, 0, &dir_uid, &file_uid, 0, "ignored");

    ASSERT_EQ(0, copy_call_count);
    ASSERT_EQ(0, captured_event_data[0x10]);
    ASSERT_EQ(0x11, captured_data_len);
}

TEST(negative_length_clamps_to_zero)
{
    reset_mocks();
    AUDIT_$LOG_DIR_OP(0x12, 0, &dir_uid, &file_uid, (uint16_t)-3, "ignored");

    ASSERT_EQ(0, copy_call_count);
    ASSERT_EQ(0, captured_event_data[0x10]);
    ASSERT_EQ(0x11, captured_data_len);
}

TEST(status_ok_gives_flag_zero)
{
    reset_mocks();
    AUDIT_$LOG_DIR_OP(0x12, 0, &dir_uid, &file_uid, 1, "x");
    ASSERT_EQ(0, captured_success_flag);
    ASSERT_EQ(0, captured_status);
}

TEST(status_error_gives_flag_one_and_forwards_status)
{
    reset_mocks();
    AUDIT_$LOG_DIR_OP(0x12, 0x00040001, &dir_uid, &file_uid, 1, "x");
    ASSERT_EQ(1, captured_success_flag);
    ASSERT_EQ(0x00040001, captured_status);
}

TEST(max_length_name)
{
    static char big[255];
    memset(big, 'z', sizeof(big));
    reset_mocks();
    AUDIT_$LOG_DIR_OP(0x12, 0, &dir_uid, &file_uid, 255, big);

    ASSERT_EQ(255, copy_len_seen);
    ASSERT_EQ('z', captured_event_data[0x10 + 254]);
    ASSERT_EQ(0, captured_event_data[0x10 + 255]);
    ASSERT_EQ(0x10 + 255 + 1, captured_data_len);
}

int main(void)
{
    printf("AUDIT_$LOG_DIR_OP tests\n");
    RUN_TEST(header_and_uids);
    RUN_TEST(name_copied_and_terminated);
    RUN_TEST(zero_length_skips_copy);
    RUN_TEST(negative_length_clamps_to_zero);
    RUN_TEST(status_ok_gives_flag_zero);
    RUN_TEST(status_error_gives_flag_one_and_forwards_status);
    RUN_TEST(max_length_name);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
