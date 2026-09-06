/*
 * audit/test/test_log_prot_op.c - Unit tests for AUDIT_$LOG_PROT_OP
 *
 * Tests the protection operation audit event logger by mocking
 * AUDIT_$LOG_EVENT and OS_$DATA_COPY, and verifying:
 *   - Event header (type=4, hardcoded subtype=0x14)
 *   - Event data layout (prot_data(44) + uid(8) + subject_uid(8) +
 *     acl_uid(8) + prot_flags(2) = 70 bytes)
 *   - Success flag (0 when status==0, 1 when status!=0)
 *   - Data length is always 70 (0x46)
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

#define ASSERT_MEM_EQ(expected, actual, len) do { \
    if (memcmp((expected), (actual), (len)) != 0) { \
        printf("FAILED\n    Memory mismatch at line %d\n", __LINE__); \
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
#define AUDIT_INTERNAL_H
#define OS_H

/* Types needed */
typedef uint32_t status_$t;
typedef struct { uint32_t high; uint32_t low; } apollo_uid_t;
#define uid_t apollo_uid_t

#define status_$ok 0

/* Mock OS_$DATA_COPY - just forward to memcpy */
void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    memcpy(dst, src, len);
}

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
#include "../log_prot_op.c"

/* ================================================================
 * Tests
 * ================================================================ */

/*
 * Test: Event header has correct type and hardcoded subtype 0x14
 */
TEST(event_header_correct)
{
    reset_mocks();
    uid_t uid = { 0x11111111, 0x22222222 };
    uint8_t prot_data[44];
    memset(prot_data, 0xAA, sizeof(prot_data));
    uid_t acl_uid = { 0x33333333, 0x44444444 };
    uid_t subject_uid = { 0x55555555, 0x66666666 };

    audit_$log_prot_op(0, &uid, prot_data, &acl_uid, &subject_uid, 0);

    ASSERT_EQ(1, log_event_call_count);
    ASSERT_EQ(4, captured_event_class);
    ASSERT_EQ(0x14, captured_event_subtype);
    ASSERT_EQ(0, captured_event_reserved);
}

/*
 * Test: Subtype is always 0x14 regardless of parameters
 * (Unlike other log_*_op functions, this one hardcodes the subtype)
 */
TEST(subtype_always_0x14)
{
    reset_mocks();
    uid_t uid = { 0, 0 };
    uint8_t prot_data[44] = {0};
    uid_t acl_uid = { 0, 0 };
    uid_t subject_uid = { 0, 0 };

    /* Call with various status values - subtype stays 0x14 */
    audit_$log_prot_op(0x12345, &uid, prot_data, &acl_uid, &subject_uid, 0xFF);

    ASSERT_EQ(0x14, captured_event_subtype);
}

/*
 * Test: Success flag is 0 when status is 0
 */
TEST(success_flag_zero_on_success)
{
    reset_mocks();
    uid_t uid = { 0, 0 };
    uint8_t prot_data[44] = {0};
    uid_t acl_uid = { 0, 0 };
    uid_t subject_uid = { 0, 0 };

    audit_$log_prot_op(0, &uid, prot_data, &acl_uid, &subject_uid, 0);

    ASSERT_EQ(0, captured_success_flag);
}

/*
 * Test: Success flag is 1 when status is non-zero
 */
TEST(success_flag_one_on_failure)
{
    reset_mocks();
    uid_t uid = { 0, 0 };
    uint8_t prot_data[44] = {0};
    uid_t acl_uid = { 0, 0 };
    uid_t subject_uid = { 0, 0 };

    audit_$log_prot_op(0xBEEF, &uid, prot_data, &acl_uid, &subject_uid, 0);

    ASSERT_EQ(1, captured_success_flag);
}

/*
 * Test: Data length is always 70 (0x46)
 */
TEST(data_len_is_70)
{
    reset_mocks();
    uid_t uid = { 0, 0 };
    uint8_t prot_data[44] = {0};
    uid_t acl_uid = { 0, 0 };
    uid_t subject_uid = { 0, 0 };

    audit_$log_prot_op(0, &uid, prot_data, &acl_uid, &subject_uid, 0);

    ASSERT_EQ(70, captured_data_len);
}

/*
 * Test: Protection data is copied correctly (44 bytes at offset 0)
 */
TEST(prot_data_copied)
{
    reset_mocks();
    uid_t uid = { 0, 0 };
    uint8_t prot_data[44];
    /* Fill with recognizable pattern */
    for (int i = 0; i < 44; i++) {
        prot_data[i] = (uint8_t)(i + 1);
    }
    uid_t acl_uid = { 0, 0 };
    uid_t subject_uid = { 0, 0 };

    audit_$log_prot_op(0, &uid, prot_data, &acl_uid, &subject_uid, 0);

    /* Prot data should be at offset 0 in event data */
    ASSERT_MEM_EQ(prot_data, captured_event_data, 44);
}

/*
 * Test: Full event data layout verification
 *
 * Expected layout (70 bytes):
 *   [0x00..0x2B] prot_data (44 bytes)
 *   [0x2C..0x2F] uid.high
 *   [0x30..0x33] uid.low
 *   [0x34..0x37] subject_uid.high
 *   [0x38..0x3B] subject_uid.low
 *   [0x3C..0x3F] acl_uid.high
 *   [0x40..0x43] acl_uid.low
 *   [0x44..0x45] prot_flags
 */
TEST(event_data_full_layout)
{
    reset_mocks();
    uid_t uid = { 0xAAAABBBB, 0xCCCCDDDD };
    uint8_t prot_data[44];
    memset(prot_data, 0x42, sizeof(prot_data));
    uid_t acl_uid = { 0x11112222, 0x33334444 };
    uid_t subject_uid = { 0x55556666, 0x77778888 };
    uint16_t prot_flags = 0xABCD;

    audit_$log_prot_op(0, &uid, prot_data, &acl_uid, &subject_uid, prot_flags);

    ASSERT_EQ(70, captured_data_len);

    /* Check prot_data at offset 0..43 */
    uint8_t expected_prot[44];
    memset(expected_prot, 0x42, sizeof(expected_prot));
    ASSERT_MEM_EQ(expected_prot, captured_event_data, 44);

    /* Check uid at offset 44 (0x2C) */
    uint32_t *data32 = (uint32_t *)(captured_event_data + 44);
    ASSERT_EQ(0xAAAABBBB, data32[0]);  /* uid.high */
    ASSERT_EQ(0xCCCCDDDD, data32[1]);  /* uid.low */

    /* Check subject_uid at offset 52 (0x34) */
    ASSERT_EQ(0x55556666, data32[2]);  /* subject_uid.high */
    ASSERT_EQ(0x77778888, data32[3]);  /* subject_uid.low */

    /* Check acl_uid at offset 60 (0x3C) */
    ASSERT_EQ(0x11112222, data32[4]);  /* acl_uid.high */
    ASSERT_EQ(0x33334444, data32[5]);  /* acl_uid.low */

    /* Check prot_flags at offset 68 (0x44) */
    uint16_t *data16 = (uint16_t *)(captured_event_data + 68);
    ASSERT_EQ(0xABCD, data16[0]);     /* prot_flags */
}

/*
 * Test: Status value is passed through correctly
 */
TEST(status_passed_through)
{
    reset_mocks();
    uid_t uid = { 0, 0 };
    uint8_t prot_data[44] = {0};
    uid_t acl_uid = { 0, 0 };
    uid_t subject_uid = { 0, 0 };

    audit_$log_prot_op(0xDEADBEEF, &uid, prot_data, &acl_uid, &subject_uid, 0);

    ASSERT_EQ(0xDEADBEEF, captured_status);
}

/*
 * Test: UID parameter order in event data
 * Verifies that subject_uid (param 5) comes before acl_uid (param 4)
 * in the event data, matching the original assembly ordering.
 */
TEST(uid_order_subject_before_acl)
{
    reset_mocks();
    uid_t uid = { 0x01010101, 0x02020202 };
    uint8_t prot_data[44] = {0};
    uid_t acl_uid = { 0x03030303, 0x04040404 };       /* param 4 */
    uid_t subject_uid = { 0x05050505, 0x06060606 };    /* param 5 */

    audit_$log_prot_op(0, &uid, prot_data, &acl_uid, &subject_uid, 0);

    uint32_t *data32 = (uint32_t *)(captured_event_data + 44);

    /* uid first */
    ASSERT_EQ(0x01010101, data32[0]);
    ASSERT_EQ(0x02020202, data32[1]);

    /* subject_uid second (from param 5, NOT param 4) */
    ASSERT_EQ(0x05050505, data32[2]);
    ASSERT_EQ(0x06060606, data32[3]);

    /* acl_uid third (from param 4, NOT param 5) */
    ASSERT_EQ(0x03030303, data32[4]);
    ASSERT_EQ(0x04040404, data32[5]);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    printf("AUDIT_$LOG_PROT_OP tests:\n");

    RUN_TEST(event_header_correct);
    RUN_TEST(subtype_always_0x14);
    RUN_TEST(success_flag_zero_on_success);
    RUN_TEST(success_flag_one_on_failure);
    RUN_TEST(data_len_is_70);
    RUN_TEST(prot_data_copied);
    RUN_TEST(event_data_full_layout);
    RUN_TEST(status_passed_through);
    RUN_TEST(uid_order_subject_before_acl);

    printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
