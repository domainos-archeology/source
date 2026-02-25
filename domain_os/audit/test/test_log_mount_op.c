/*
 * audit/test/test_log_mount_op.c - Unit tests for AUDIT_$LOG_MOUNT_OP
 *
 * Tests the mount operation audit event logger by mocking AUDIT_$LOG_EVENT
 * and verifying:
 *   - Event header (type=4, subtype from param)
 *   - Event data layout (uid + mount_uid + extra = 20 bytes)
 *   - Success flag (0 when status==0, 1 when status!=0)
 *   - Data length is always 20 (0x14)
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

/* ================================================================
 * Mock infrastructure
 * ================================================================ */

/* Prevent inclusion of kernel headers */
#define BASE_H
#define AUDIT_H
#define OS_H

/* Types needed */
typedef uint32_t status_$t;
typedef struct { uint32_t high; uint32_t low; } apollo_uid_t;
#define uid_t apollo_uid_t

#define status_$ok 0

/* Capture AUDIT_$LOG_EVENT call arguments */
static int log_event_call_count = 0;

/* Event header captured */
static uint16_t captured_event_class;
static uint16_t captured_event_subtype;
static uint32_t captured_event_reserved;

/* Success flag captured */
static uint16_t captured_success_flag;

/* Status pointer captured */
static uint32_t captured_status;

/* Event data captured (max 128 bytes) */
static uint8_t captured_event_data[128];
static uint16_t captured_data_len;

void AUDIT_$LOG_EVENT(uid_t *event_uid, uint16_t *event_flags,
                      uint32_t *status, char *data, uint16_t *data_len)
{
    log_event_call_count++;

    /* Capture event header (laid out as: event_class(u16), subtype(u16), reserved(u32)) */
    uint16_t *hdr = (uint16_t *)event_uid;
    captured_event_class = hdr[0];
    captured_event_subtype = hdr[1];
    captured_event_reserved = ((uint32_t *)event_uid)[1];

    /* Capture other args */
    captured_success_flag = *event_flags;
    captured_status = *status;
    captured_data_len = *data_len;

    /* Capture event data */
    if (*data_len <= sizeof(captured_event_data)) {
        memcpy(captured_event_data, data, *data_len);
    }
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
    memset(captured_event_data, 0, sizeof(captured_event_data));
}

/* Include the source under test */
#include "../log_mount_op.c"

/* ================================================================
 * Tests
 * ================================================================ */

/*
 * Test: Event header has correct type and subtype
 */
TEST(event_header_correct)
{
    reset_mocks();
    uid_t uid = { 0x11111111, 0x22222222 };
    uid_t mount_uid = { 0x33333333, 0x44444444 };

    audit_$log_mount_op(0x1C, 0, &uid, &mount_uid, 0);

    ASSERT_EQ(1, log_event_call_count);
    ASSERT_EQ(4, captured_event_class);
    ASSERT_EQ(0x1C, captured_event_subtype);
    ASSERT_EQ(0, captured_event_reserved);
}

/*
 * Test: Different audit subtype (drop mount = 0x1D)
 */
TEST(event_header_drop_mount)
{
    reset_mocks();
    uid_t uid = { 0x11111111, 0x22222222 };
    uid_t mount_uid = { 0x33333333, 0x44444444 };

    audit_$log_mount_op(0x1D, 0, &uid, &mount_uid, 0);

    ASSERT_EQ(4, captured_event_class);
    ASSERT_EQ(0x1D, captured_event_subtype);
}

/*
 * Test: Success flag is 0 when status is 0
 */
TEST(success_flag_zero_on_success)
{
    reset_mocks();
    uid_t uid = { 0x11111111, 0x22222222 };
    uid_t mount_uid = { 0x33333333, 0x44444444 };

    audit_$log_mount_op(0x1C, 0, &uid, &mount_uid, 0);

    ASSERT_EQ(0, captured_success_flag);
}

/*
 * Test: Success flag is 1 when status is non-zero
 */
TEST(success_flag_one_on_failure)
{
    reset_mocks();
    uid_t uid = { 0x11111111, 0x22222222 };
    uid_t mount_uid = { 0x33333333, 0x44444444 };

    audit_$log_mount_op(0x1C, 0x12345, &uid, &mount_uid, 0);

    ASSERT_EQ(1, captured_success_flag);
}

/*
 * Test: Success flag is 1 when status is negative
 */
TEST(success_flag_one_on_negative_status)
{
    reset_mocks();
    uid_t uid = { 0x11111111, 0x22222222 };
    uid_t mount_uid = { 0x33333333, 0x44444444 };

    audit_$log_mount_op(0x1C, (status_$t)-1, &uid, &mount_uid, 0);

    ASSERT_EQ(1, captured_success_flag);
}

/*
 * Test: Data length is always 20 (0x14)
 */
TEST(data_len_is_20)
{
    reset_mocks();
    uid_t uid = { 0x11111111, 0x22222222 };
    uid_t mount_uid = { 0x33333333, 0x44444444 };

    audit_$log_mount_op(0x1C, 0, &uid, &mount_uid, 0x12345678);

    ASSERT_EQ(20, captured_data_len);
}

/*
 * Test: Event data layout - UIDs and extra value in correct positions
 *
 * Expected layout (20 bytes):
 *   [0x00..0x03] uid.high
 *   [0x04..0x07] uid.low
 *   [0x08..0x0B] mount_uid.high
 *   [0x0C..0x0F] mount_uid.low
 *   [0x10..0x13] extra
 */
TEST(event_data_layout)
{
    reset_mocks();
    uid_t uid = { 0xAAAABBBB, 0xCCCCDDDD };
    uid_t mount_uid = { 0x11112222, 0x33334444 };
    uint32_t extra = 0xDEADBEEF;

    audit_$log_mount_op(0x1C, 0, &uid, &mount_uid, extra);

    ASSERT_EQ(20, captured_data_len);

    /* Check uid at offset 0 */
    uint32_t *data32 = (uint32_t *)captured_event_data;
    ASSERT_EQ(0xAAAABBBB, data32[0]);  /* uid.high */
    ASSERT_EQ(0xCCCCDDDD, data32[1]);  /* uid.low */

    /* Check mount_uid at offset 8 */
    ASSERT_EQ(0x11112222, data32[2]);  /* mount_uid.high */
    ASSERT_EQ(0x33334444, data32[3]);  /* mount_uid.low */

    /* Check extra at offset 16 */
    ASSERT_EQ(0xDEADBEEF, data32[4]);  /* extra */
}

/*
 * Test: Status value is passed through correctly
 */
TEST(status_passed_through)
{
    reset_mocks();
    uid_t uid = { 0x11111111, 0x22222222 };
    uid_t mount_uid = { 0x33333333, 0x44444444 };

    audit_$log_mount_op(0x1C, 0xABCD1234, &uid, &mount_uid, 0);

    ASSERT_EQ(0xABCD1234, captured_status);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    printf("AUDIT_$LOG_MOUNT_OP tests:\n");

    RUN_TEST(event_header_correct);
    RUN_TEST(event_header_drop_mount);
    RUN_TEST(success_flag_zero_on_success);
    RUN_TEST(success_flag_one_on_failure);
    RUN_TEST(success_flag_one_on_negative_status);
    RUN_TEST(data_len_is_20);
    RUN_TEST(event_data_layout);
    RUN_TEST(status_passed_through);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
