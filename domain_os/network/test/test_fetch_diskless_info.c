/*
 * network/test/test_fetch_diskless_info.c
 *
 * Unit tests for network_$fetch_diskless_info (0x00E3366C).
 *
 * Tests the three command paths (cmd=2, cmd=8, cmd=0x37) and error handling.
 * Mocks ASKNODE_$INTERNET_INFO, HINT_$ADDI, RIP_$UPDATE_INT, and CRASH_SYSTEM
 * to verify the function's control flow and data transformations.
 *
 * Build (host compiler):
 *   cc -std=c11 -Wall -Wextra -I. -o /tmp/test_fetch_diskless \
 *      network/test/test_fetch_diskless_info.c
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>

/* ============================================================================
 * Test framework
 * ============================================================================ */

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

#define ASSERT_NE(not_expected, actual) do { \
    if ((not_expected) == (actual)) { \
        printf("FAILED\n    Did not expect: 0x%lx at line %d\n", \
               (unsigned long)(not_expected), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ============================================================================
 * Minimal type stubs for building outside the kernel
 * ============================================================================ */

typedef long status_$t;
#define status_$ok 0

typedef struct uid_t {
    uint32_t high;
    uint32_t low;
} uid_t;

typedef struct {
    uint16_t high;
    uint32_t low;
} clock_t_test;

/* Timezone record (12 bytes, matching cal_$timezone_rec_t without boot_volx) */
typedef struct {
    short utc_delta;
    char tz_name[4];
    uint16_t drift_high;
    uint32_t drift_low;
} cal_$timezone_rec_t;

/* XNS address for RIP */
typedef struct rip_$xns_addr_t {
    uint32_t network;
    uint8_t  host[6];
} rip_$xns_addr_t;

/* ASKNODE request type constants */
#define ASKNODE_REQ_BOOT_TIME 0x02
#define ASKNODE_REQ_TIMEZONE  0x08

/* ============================================================================
 * Mock globals
 * ============================================================================ */

uid_t UID_$NIL = { 0, 0 };
uint32_t TIME_$CLOCKH = 0;
cal_$timezone_rec_t CAL_$TIMEZONE;
uint32_t ROUTE_$PORT = 0;

/* ============================================================================
 * Mock tracking structures
 * ============================================================================ */

/* ASKNODE_$INTERNET_INFO mock */
static struct {
    int call_count;
    uint16_t last_req_type;
    uint32_t last_node_id;
    /* Response data to fill in */
    status_$t return_status;     /* status written to *status_ret */
    uint32_t result[6];          /* result buffer to copy */
} mock_asknode;

/* HINT_$ADDI mock */
static struct {
    int call_count;
    uid_t last_uid;
    uint32_t last_addresses[2];
} mock_hint;

/* RIP_$UPDATE_INT mock */
static struct {
    int call_count;
    uint32_t last_network;
    rip_$xns_addr_t last_source;
    uint16_t last_hop_count;
    uint8_t last_port_index;
    int8_t last_flags;
} mock_rip;

/* CRASH_SYSTEM mock */
static struct {
    int call_count;
    status_$t last_status;
} mock_crash;

/* For longjmp on CRASH_SYSTEM (since real one is noreturn) */
static jmp_buf crash_jmpbuf;
static int crash_expected = 0;

static void reset_mocks(void) {
    memset(&mock_asknode, 0, sizeof(mock_asknode));
    memset(&mock_hint, 0, sizeof(mock_hint));
    memset(&mock_rip, 0, sizeof(mock_rip));
    memset(&mock_crash, 0, sizeof(mock_crash));
    memset(&CAL_$TIMEZONE, 0, sizeof(CAL_$TIMEZONE));
    TIME_$CLOCKH = 0;
    ROUTE_$PORT = 0;
    crash_expected = 0;
}

/* ============================================================================
 * Mock implementations
 * ============================================================================ */

uint32_t ASKNODE_$INTERNET_INFO(uint16_t *req_type, uint32_t *node_id,
                                int32_t *req_len, uid_t *param,
                                uint16_t *resp_len, uint32_t *result,
                                status_$t *status)
{
    mock_asknode.call_count++;
    mock_asknode.last_req_type = *req_type;
    mock_asknode.last_node_id = *node_id;

    (void)req_len;
    (void)param;
    (void)resp_len;

    /* Copy the pre-configured result data */
    memcpy(result, mock_asknode.result, sizeof(mock_asknode.result));
    *status = mock_asknode.return_status;

    return 0;
}

void HINT_$ADDI(uid_t *uid_ptr, uint32_t *addresses) {
    mock_hint.call_count++;
    mock_hint.last_uid = *uid_ptr;
    mock_hint.last_addresses[0] = addresses[0];
    mock_hint.last_addresses[1] = addresses[1];
}

void RIP_$UPDATE_INT(uint32_t network, rip_$xns_addr_t *source,
                     uint16_t hop_count, uint8_t port_index,
                     int8_t flags, status_$t *status_ret)
{
    mock_rip.call_count++;
    mock_rip.last_network = network;
    mock_rip.last_source = *source;
    mock_rip.last_hop_count = hop_count;
    mock_rip.last_port_index = port_index;
    mock_rip.last_flags = flags;
    *status_ret = status_$ok;
}

void CRASH_SYSTEM(const status_$t *status_p) {
    mock_crash.call_count++;
    mock_crash.last_status = *status_p;
    if (crash_expected) {
        longjmp(crash_jmpbuf, 1);
    }
    /* If not expected, just record it and return (won't be accurate
     * since the real function is noreturn, but allows test to continue) */
}

/* ============================================================================
 * Guard headers and include the source under test
 * ============================================================================ */

/* Guard all kernel headers - types and mocks are provided above */
#define BASE_H
#define NETWORK_H
#define NETWORK_INTERNAL_H
#define ASKNODE_H
#define ASKNODE_INTERNAL_H
#define CAL_H
#define HINT_H
#define HINT_INTERNAL_H
#define MISC_H
#define RIP_H
#define RIP_INTERNAL_H
#define ROUTE_H
#define ROUTE_INTERNAL_H
#define TIME_H
#define ML_H
#define PROC1_H
#define PROC1_CONFIG_H
#define MMAP_H
#define EC_H
#define UID_H
#define ARCH_H
#define AST_H
#define ACL_H
#define FILE_H
#define DISK_H
#define OS_H
#define OS_INTERNAL_H
#define MATH_H
#define MEM_H
#define MMU_H
#define MST_H
#define PROC2_H
#define VTOC_H
#include "../fetch_diskless_info.c"

/* ============================================================================
 * Tests: cmd=2 (Boot Time)
 * ============================================================================ */

TEST(cmd2_sets_clockh) {
    reset_mocks();

    /* Set up ASKNODE to return success with clock value at result[3] */
    mock_asknode.return_status = status_$ok;
    mock_asknode.result[1] = status_$ok;      /* response status */
    mock_asknode.result[3] = 0x12345678;      /* clock high word */

    network_$fetch_diskless_info(ASKNODE_REQ_BOOT_TIME, 0x000ABCDE);

    ASSERT_EQ(1, mock_asknode.call_count);
    ASSERT_EQ(ASKNODE_REQ_BOOT_TIME, mock_asknode.last_req_type);
    ASSERT_EQ(0x000ABCDE, mock_asknode.last_node_id);
    ASSERT_EQ(0x12345678, TIME_$CLOCKH);
    ASSERT_EQ(0, mock_crash.call_count);
}

TEST(cmd2_zero_clock) {
    reset_mocks();

    mock_asknode.return_status = status_$ok;
    mock_asknode.result[1] = status_$ok;
    mock_asknode.result[3] = 0;

    TIME_$CLOCKH = 0xDEADDEAD;
    network_$fetch_diskless_info(ASKNODE_REQ_BOOT_TIME, 0x00012345);

    ASSERT_EQ(0, TIME_$CLOCKH);
}

/* ============================================================================
 * Tests: cmd=8 (Timezone)
 * ============================================================================ */

TEST(cmd8_copies_timezone) {
    reset_mocks();

    mock_asknode.return_status = status_$ok;
    mock_asknode.result[1] = status_$ok;

    /*
     * result[2..4] (12 bytes) contain the timezone record.
     * We set known values and verify they're copied to CAL_$TIMEZONE.
     */
    mock_asknode.result[2] = 0x012C4553; /* utc_delta=0x012C(300), tz_name="ES" */
    mock_asknode.result[3] = 0x54000042; /* tz_name="T\0", drift_high=0x0042 */
    mock_asknode.result[4] = 0x00001234; /* drift_low=0x00001234 */

    network_$fetch_diskless_info(ASKNODE_REQ_TIMEZONE, 0x000ABCDE);

    ASSERT_EQ(1, mock_asknode.call_count);
    ASSERT_EQ(0, mock_crash.call_count);

    /* Verify the 12 bytes were copied correctly */
    uint32_t *tz = (uint32_t *)&CAL_$TIMEZONE;
    ASSERT_EQ(0x012C4553, tz[0]);
    ASSERT_EQ(0x54000042, tz[1]);
    ASSERT_EQ(0x00001234, tz[2]);
}

/* ============================================================================
 * Tests: cmd=0x37 (Routing Update)
 * ============================================================================ */

TEST(cmd37_updates_routing_when_port_changes) {
    reset_mocks();

    uint32_t new_port = 0xAABBCCDD;
    uint32_t node = 0x00012345;

    ROUTE_$PORT = 0x11223344;  /* Different from response port */

    mock_asknode.return_status = status_$ok;
    mock_asknode.result[1] = status_$ok;      /* response status */
    mock_asknode.result[2] = new_port;        /* response port at result[2] */

    network_$fetch_diskless_info(0x37, node);

    ASSERT_EQ(0, mock_crash.call_count);

    /* Should have called HINT_$ADDI */
    ASSERT_EQ(1, mock_hint.call_count);
    ASSERT_EQ(0, mock_hint.last_uid.high);    /* UID_$NIL.high */
    ASSERT_EQ(node, mock_hint.last_uid.low);  /* node address in low bits */
    ASSERT_EQ(new_port, mock_hint.last_addresses[0]);
    ASSERT_EQ(node, mock_hint.last_addresses[1]);

    /* Should have called RIP_$UPDATE_INT */
    ASSERT_EQ(1, mock_rip.call_count);
    ASSERT_EQ(new_port, mock_rip.last_network);
    ASSERT_EQ(new_port, mock_rip.last_source.network);
    ASSERT_EQ(1, mock_rip.last_hop_count);
    ASSERT_EQ(0, mock_rip.last_port_index);
    ASSERT_EQ(0, mock_rip.last_flags);
}

TEST(cmd37_skips_when_port_same) {
    reset_mocks();

    uint32_t port = 0xAABBCCDD;
    ROUTE_$PORT = port;  /* Same as what response will contain */

    mock_asknode.return_status = status_$ok;
    mock_asknode.result[1] = status_$ok;
    mock_asknode.result[2] = port;

    network_$fetch_diskless_info(0x37, 0x00012345);

    /* Should NOT have called HINT or RIP since port unchanged */
    ASSERT_EQ(0, mock_hint.call_count);
    ASSERT_EQ(0, mock_rip.call_count);
    ASSERT_EQ(0, mock_crash.call_count);
}

TEST(cmd37_tolerates_asknode_error) {
    reset_mocks();

    mock_asknode.return_status = 0x00110005; /* some error */
    ROUTE_$PORT = 0x11111111;

    network_$fetch_diskless_info(0x37, 0x00012345);

    /* Should NOT crash (cmd=0x37 tolerates errors) */
    ASSERT_EQ(0, mock_crash.call_count);
    /* Should NOT update routing since status != ok */
    ASSERT_EQ(0, mock_hint.call_count);
    ASSERT_EQ(0, mock_rip.call_count);
}

TEST(cmd37_tolerates_response_error) {
    reset_mocks();

    mock_asknode.return_status = status_$ok;
    mock_asknode.result[1] = 0x00110005;  /* response status = error */
    mock_asknode.result[2] = 0xDEADBEEF;
    ROUTE_$PORT = 0x11111111;

    network_$fetch_diskless_info(0x37, 0x00012345);

    /* Should NOT crash and should NOT update routing */
    ASSERT_EQ(0, mock_crash.call_count);
    ASSERT_EQ(0, mock_hint.call_count);
    ASSERT_EQ(0, mock_rip.call_count);
}

/* ============================================================================
 * Tests: Error handling
 * ============================================================================ */

TEST(cmd2_crashes_on_asknode_error) {
    reset_mocks();
    crash_expected = 1;

    mock_asknode.return_status = 0xDEAD0001;

    if (setjmp(crash_jmpbuf) == 0) {
        network_$fetch_diskless_info(ASKNODE_REQ_BOOT_TIME, 0x00012345);
        /* If we get here without crash, that's a failure */
        if (mock_crash.call_count == 0) {
            printf("FAILED\n    Expected CRASH_SYSTEM to be called\n");
            tests_failed++;
            return;
        }
    }
    /* Reached here via longjmp from CRASH_SYSTEM */
    ASSERT_EQ(1, mock_crash.call_count);
    ASSERT_EQ(0xDEAD0001, (uint32_t)mock_crash.last_status);
}

TEST(cmd8_crashes_on_response_error) {
    reset_mocks();
    crash_expected = 1;

    mock_asknode.return_status = status_$ok;
    mock_asknode.result[1] = 0xBAD00002;  /* response status = error */

    if (setjmp(crash_jmpbuf) == 0) {
        network_$fetch_diskless_info(ASKNODE_REQ_TIMEZONE, 0x00012345);
        if (mock_crash.call_count == 0) {
            printf("FAILED\n    Expected CRASH_SYSTEM to be called\n");
            tests_failed++;
            return;
        }
    }
    ASSERT_EQ(1, mock_crash.call_count);
    ASSERT_EQ(0xBAD00002, (uint32_t)mock_crash.last_status);
}

/* ============================================================================
 * Tests: cmd=0x37 UID construction
 * ============================================================================ */

TEST(cmd37_uid_construction) {
    reset_mocks();

    uint32_t node = 0x000FFFFF;  /* Maximum 20-bit node address */
    uint32_t new_port = 0x55667788;
    ROUTE_$PORT = 0x11223344;

    mock_asknode.return_status = status_$ok;
    mock_asknode.result[1] = status_$ok;
    mock_asknode.result[2] = new_port;

    network_$fetch_diskless_info(0x37, node);

    /* UID should be {0, node} since UID_$NIL is all zeros */
    ASSERT_EQ(0, mock_hint.last_uid.high);
    ASSERT_EQ(node, mock_hint.last_uid.low);
}

TEST(cmd37_source_node_in_host) {
    reset_mocks();

    uint32_t node = 0x00054321;
    uint32_t new_port = 0x99887766;
    ROUTE_$PORT = 0x11111111;

    mock_asknode.return_status = status_$ok;
    mock_asknode.result[1] = status_$ok;
    mock_asknode.result[2] = new_port;

    network_$fetch_diskless_info(0x37, node);

    /* Verify the source address passed to RIP_$UPDATE_INT */
    ASSERT_EQ(new_port, mock_rip.last_source.network);

    /* host[2..5] should contain node address in the low 20 bits */
    uint32_t host_low =
        ((uint32_t)mock_rip.last_source.host[2] << 24) |
        ((uint32_t)mock_rip.last_source.host[3] << 16) |
        ((uint32_t)mock_rip.last_source.host[4] << 8) |
        ((uint32_t)mock_rip.last_source.host[5]);

    /* The low 20 bits should match the node address */
    ASSERT_EQ(node & 0x000FFFFF, host_low & 0x000FFFFF);
}

/* ============================================================================
 * Tests: Unknown command
 * ============================================================================ */

TEST(unknown_cmd_no_action) {
    reset_mocks();

    mock_asknode.return_status = status_$ok;
    mock_asknode.result[1] = status_$ok;

    /* cmd=0x10 is not one of the handled cases */
    network_$fetch_diskless_info(0x10, 0x00012345);

    ASSERT_EQ(0, mock_crash.call_count);
    ASSERT_EQ(0, mock_hint.call_count);
    ASSERT_EQ(0, mock_rip.call_count);
    ASSERT_EQ(0, TIME_$CLOCKH);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("=== network_$fetch_diskless_info tests ===\n\n");

    printf("cmd=2 (Boot Time) tests:\n");
    RUN_TEST(cmd2_sets_clockh);
    RUN_TEST(cmd2_zero_clock);

    printf("\ncmd=8 (Timezone) tests:\n");
    RUN_TEST(cmd8_copies_timezone);

    printf("\ncmd=0x37 (Routing Update) tests:\n");
    RUN_TEST(cmd37_updates_routing_when_port_changes);
    RUN_TEST(cmd37_skips_when_port_same);
    RUN_TEST(cmd37_tolerates_asknode_error);
    RUN_TEST(cmd37_tolerates_response_error);
    RUN_TEST(cmd37_uid_construction);
    RUN_TEST(cmd37_source_node_in_host);

    printf("\nError handling tests:\n");
    RUN_TEST(cmd2_crashes_on_asknode_error);
    RUN_TEST(cmd8_crashes_on_response_error);

    printf("\nUnknown command tests:\n");
    RUN_TEST(unknown_cmd_no_action);

    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
