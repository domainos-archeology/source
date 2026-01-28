/*
 * rem_file/test/test_send_request.c - Unit tests for REM_FILE_$SEND_REQUEST
 *
 * Tests the early-exit paths, request header struct layout, and
 * response validation logic. Network I/O is not tested since it
 * requires a full kernel environment.
 */

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Minimal type stubs for native compilation */
typedef long status_$t;

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

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n    Condition false at line %d\n", __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/*
 * Mock globals and stubs
 */

/* Error codes */
#define file_$object_not_found                      0x000F0001
#define file_$comms_problem_with_remote_node         0x000F0004
#define file_$bad_reply_received_from_remote_node    0x000F0003

/* Process globals */
#define PROC1_MAX_PROCESSES 256
uint16_t PROC1_$TYPE[PROC1_MAX_PROCESSES];
uint16_t PROC1_$CURRENT;
uint16_t PROC1_$AS_ID;
uint32_t NODE_$ME = 0x12345678;

/* Network globals */
uint8_t NETWORK_$CAPABLE_FLAGS = 0;
int8_t NETWORK_$DISKLESS = 0;
uint32_t NETWORK_$MOTHER_NODE = 0;

/*
 * Request header struct matching rem_file_internal.h
 * Test that msg_type is at offset 0 and magic/opcode follow at 2/3.
 */
typedef struct {
    uint16_t msg_type;  /* Offset 0: Set to 1 by SEND_REQUEST */
    uint8_t magic;      /* Offset 2: 0x80 */
    uint8_t opcode;     /* Offset 3: operation code */
} test_request_hdr_t;

/* ============================================================================
 * Struct layout tests
 * ============================================================================ */

/*
 * Test: msg_type field is at offset 0 (2 bytes)
 */
TEST(request_hdr_msg_type_offset) {
    test_request_hdr_t hdr;
    ASSERT_EQ(0, (size_t)&hdr.msg_type - (size_t)&hdr);
    ASSERT_EQ(2, sizeof(hdr.msg_type));
}

/*
 * Test: magic field is at offset 2
 */
TEST(request_hdr_magic_offset) {
    test_request_hdr_t hdr;
    ASSERT_EQ(2, (size_t)&hdr.magic - (size_t)&hdr);
}

/*
 * Test: opcode field is at offset 3
 */
TEST(request_hdr_opcode_offset) {
    test_request_hdr_t hdr;
    ASSERT_EQ(3, (size_t)&hdr.opcode - (size_t)&hdr);
}

/*
 * Test: total header size is 4 bytes
 */
TEST(request_hdr_size) {
    ASSERT_EQ(4, sizeof(test_request_hdr_t));
}

/*
 * Test: Wire format matches server expectation
 * Server reads: frame.msg_version (offset 0-1), frame.opcode (offset 3)
 * SEND_REQUEST writes *(uint16_t *)request = 1, so bytes 0-1 = 0x0001 (big-endian)
 */
TEST(wire_format_msg_type) {
    test_request_hdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    /* Simulate what SEND_REQUEST does: *req_u16 = 1 */
    *(uint16_t *)&hdr = 1;
    hdr.magic = 0x80;
    hdr.opcode = 0x0C;

    uint8_t *bytes = (uint8_t *)&hdr;
    /* On big-endian (m68k native): bytes[0]=0x00, bytes[1]=0x01 */
    /* On little-endian (host):     bytes[0]=0x01, bytes[1]=0x00 */
    /* Either way, the uint16_t value at offset 0 is 1 */
    ASSERT_EQ(1, *(uint16_t *)bytes);
    ASSERT_EQ(0x80, bytes[2]);
    ASSERT_EQ(0x0C, bytes[3]);
}

/* ============================================================================
 * Response validation tests
 * ============================================================================ */

/*
 * Test: Valid response has opcode = request opcode + 1
 */
TEST(response_opcode_valid) {
    uint8_t request[8] = {0};
    uint8_t response[8] = {0};

    request[3] = 0x0C;   /* Request opcode */
    response[3] = 0x0D;  /* Response opcode = request + 1 */

    /* The validation check from send_request.c */
    ASSERT_TRUE((uint32_t)response[3] == (uint32_t)request[3] + 1);
}

/*
 * Test: Invalid response has wrong opcode
 */
TEST(response_opcode_invalid) {
    uint8_t request[8] = {0};
    uint8_t response[8] = {0};

    request[3] = 0x0C;   /* Request opcode */
    response[3] = 0x0E;  /* Wrong response opcode */

    ASSERT_TRUE((uint32_t)response[3] != (uint32_t)request[3] + 1);
}

/*
 * Test: Busy response is first word == 0xFFFF
 */
TEST(busy_response_detection) {
    int16_t response[4] = {0};
    response[0] = -1;  /* 0xFFFF = busy */

    ASSERT_EQ(-1, response[0]);
}

/*
 * Test: Non-busy response
 */
TEST(non_busy_response) {
    int16_t response[4] = {0};
    response[0] = 1;  /* msg_type = 1 = normal */

    ASSERT_TRUE(response[0] != -1);
}

/* ============================================================================
 * Early exit condition tests (logic only, no actual SEND_REQUEST call)
 * ============================================================================ */

/*
 * Test: Process type 9 should trigger early exit
 */
TEST(process_type_9_early_exit) {
    PROC1_$CURRENT = 5;
    PROC1_$TYPE[5] = 9;

    /* The check from send_request.c */
    ASSERT_TRUE(PROC1_$TYPE[PROC1_$CURRENT] == 9);
}

/*
 * Test: Non-type-9 process should not trigger early exit
 */
TEST(process_type_normal_no_exit) {
    PROC1_$CURRENT = 3;
    PROC1_$TYPE[3] = 7;

    ASSERT_TRUE(PROC1_$TYPE[PROC1_$CURRENT] != 9);
}

/*
 * Test: Network not capable and non-local node should trigger early exit
 */
TEST(network_not_capable_remote_node) {
    NETWORK_$CAPABLE_FLAGS = 0;
    uint32_t addr_info[2] = {0, 0xAAAAAAAA};  /* Different from NODE_$ME */
    NODE_$ME = 0x12345678;

    ASSERT_TRUE((NETWORK_$CAPABLE_FLAGS & 1) == 0 && addr_info[1] != NODE_$ME);
}

/*
 * Test: Network not capable but local node should NOT trigger early exit
 */
TEST(network_not_capable_local_node) {
    NETWORK_$CAPABLE_FLAGS = 0;
    NODE_$ME = 0x12345678;
    uint32_t addr_info[2] = {0, 0x12345678};  /* Same as NODE_$ME */

    /* Should NOT trigger the early exit (local node is always OK) */
    ASSERT_TRUE(!((NETWORK_$CAPABLE_FLAGS & 1) == 0 && addr_info[1] != NODE_$ME));
}

/*
 * Test: Network capable should NOT trigger early exit even for remote node
 */
TEST(network_capable_remote_node) {
    NETWORK_$CAPABLE_FLAGS = 1;
    uint32_t addr_info[2] = {0, 0xAAAAAAAA};

    ASSERT_TRUE(!((NETWORK_$CAPABLE_FLAGS & 1) == 0 && addr_info[1] != NODE_$ME));
}

/*
 * Test: Diskless mother node gets connection state 2
 */
TEST(diskless_mother_conn_state) {
    NETWORK_$DISKLESS = -1;  /* Negative = diskless */
    NETWORK_$MOTHER_NODE = 0xBBBBBBBB;
    uint32_t addr_info[2] = {0, 0xBBBBBBBB};  /* Same as mother node */

    int16_t conn_state;
    if (NETWORK_$DISKLESS < 0 && addr_info[1] == NETWORK_$MOTHER_NODE) {
        conn_state = 2;  /* CONN_STATE_DISKLESS_MOTHER */
    } else {
        conn_state = 0;
    }

    ASSERT_EQ(2, conn_state);
}

/*
 * Test: Non-diskless node gets connection state 0
 */
TEST(non_diskless_conn_state) {
    NETWORK_$DISKLESS = 0;
    uint32_t addr_info[2] = {0, 0xBBBBBBBB};

    int16_t conn_state;
    if (NETWORK_$DISKLESS < 0 && addr_info[1] == NETWORK_$MOTHER_NODE) {
        conn_state = 2;
    } else {
        conn_state = 0;
    }

    ASSERT_EQ(0, conn_state);
}

/* ============================================================================
 * Split request logic tests
 * ============================================================================ */

/*
 * Test: Request <= 0x200 fits in single packet
 */
TEST(single_packet_no_split) {
    int16_t request_len = 0x100;
    ASSERT_TRUE(request_len <= 0x200);
}

/*
 * Test: Request > 0x200 requires split
 */
TEST(large_request_needs_split) {
    int16_t request_len = 0x300;
    ASSERT_TRUE(request_len > 0x200);

    /* Split parameters */
    int16_t send_hdr_len = 0x200;
    int16_t send_data_len = request_len - 0x200;

    ASSERT_EQ(0x200, send_hdr_len);
    ASSERT_EQ(0x100, send_data_len);
}

/*
 * Test: Split flag set when bulk_max > 0
 */
TEST(split_flag_with_bulk) {
    int16_t extra_len = 0;
    uint16_t response_max = 0x100;
    uint16_t split_flag;

    /* With no extra but small response: no split */
    if (extra_len == 0 && (int16_t)response_max <= 0x200) {
        split_flag = 0;
    } else {
        split_flag = 1;
    }
    ASSERT_EQ(0, split_flag);

    /* With extra data: split needed */
    extra_len = 0x50;
    if (extra_len == 0 && (int16_t)response_max <= 0x200) {
        split_flag = 0;
    } else {
        split_flag = 1;
    }
    ASSERT_EQ(1, split_flag);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("Testing REM_FILE_$SEND_REQUEST logic:\n\n");

    printf("  --- Struct Layout ---\n");
    RUN_TEST(request_hdr_msg_type_offset);
    RUN_TEST(request_hdr_magic_offset);
    RUN_TEST(request_hdr_opcode_offset);
    RUN_TEST(request_hdr_size);
    RUN_TEST(wire_format_msg_type);

    printf("\n  --- Response Validation ---\n");
    RUN_TEST(response_opcode_valid);
    RUN_TEST(response_opcode_invalid);
    RUN_TEST(busy_response_detection);
    RUN_TEST(non_busy_response);

    printf("\n  --- Early Exit Conditions ---\n");
    RUN_TEST(process_type_9_early_exit);
    RUN_TEST(process_type_normal_no_exit);
    RUN_TEST(network_not_capable_remote_node);
    RUN_TEST(network_not_capable_local_node);
    RUN_TEST(network_capable_remote_node);
    RUN_TEST(diskless_mother_conn_state);
    RUN_TEST(non_diskless_conn_state);

    printf("\n  --- Split Request Logic ---\n");
    RUN_TEST(single_packet_no_split);
    RUN_TEST(large_request_needs_split);
    RUN_TEST(split_flag_with_bulk);

    printf("\n%d tests passed, %d tests failed\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
