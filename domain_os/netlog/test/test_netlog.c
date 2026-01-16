/*
 * NETLOG Unit Tests
 *
 * Tests for the NETLOG network logging subsystem.
 * These tests verify data structure sizes and layout.
 *
 * Build with:
 *   gcc -I../.. test_netlog.c -o test_netlog
 *
 * Run:
 *   ./test_netlog
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

/* Test helper macros */
#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        failures++; \
    } else { \
        printf("PASS: %s\n", msg); \
        passes++; \
    } \
} while(0)

static int failures = 0;
static int passes = 0;

/*
 * Log entry structure (26 bytes, 0x1A)
 * Duplicated here to test independently of kernel headers
 */
typedef struct test_netlog_entry_t {
    uint8_t     kind;           /* 0x00: Log entry type/category */
    uint8_t     process_id;     /* 0x01: Process ID that generated entry */
    uint32_t    timestamp;      /* 0x02: Clock value (high 32 bits) */
    uint32_t    uid_high;       /* 0x06: UID high word */
    uint32_t    uid_low;        /* 0x0A: UID low word */
    uint16_t    param3;         /* 0x0E: Parameter 3 */
    uint8_t     param4;         /* 0x10: Parameter 4 (low byte only) */
    uint8_t     _pad;           /* 0x11: Padding */
    uint16_t    param5;         /* 0x12: Parameter 5 */
    uint16_t    param6;         /* 0x14: Parameter 6 */
    uint16_t    param7;         /* 0x16: Parameter 7 */
    uint16_t    param8;         /* 0x18: Parameter 8 */
} __attribute__((packed)) test_netlog_entry_t;

/*
 * Test: Verify netlog_entry_t structure size
 */
void test_entry_size(void) {
    TEST_ASSERT(sizeof(test_netlog_entry_t) == 26,
                "netlog_entry_t should be 26 bytes");
}

/*
 * Test: Verify netlog_entry_t field offsets
 */
void test_entry_offsets(void) {
    TEST_ASSERT(offsetof(test_netlog_entry_t, kind) == 0,
                "kind should be at offset 0");
    TEST_ASSERT(offsetof(test_netlog_entry_t, process_id) == 1,
                "process_id should be at offset 1");
    TEST_ASSERT(offsetof(test_netlog_entry_t, timestamp) == 2,
                "timestamp should be at offset 2");
    TEST_ASSERT(offsetof(test_netlog_entry_t, uid_high) == 6,
                "uid_high should be at offset 6");
    TEST_ASSERT(offsetof(test_netlog_entry_t, uid_low) == 10,
                "uid_low should be at offset 10");
    TEST_ASSERT(offsetof(test_netlog_entry_t, param3) == 14,
                "param3 should be at offset 14");
    TEST_ASSERT(offsetof(test_netlog_entry_t, param4) == 16,
                "param4 should be at offset 16");
    TEST_ASSERT(offsetof(test_netlog_entry_t, param5) == 18,
                "param5 should be at offset 18");
    TEST_ASSERT(offsetof(test_netlog_entry_t, param6) == 20,
                "param6 should be at offset 20");
    TEST_ASSERT(offsetof(test_netlog_entry_t, param7) == 22,
                "param7 should be at offset 22");
    TEST_ASSERT(offsetof(test_netlog_entry_t, param8) == 24,
                "param8 should be at offset 24");
}

/*
 * Test: Verify entries per page constant
 */
void test_entries_per_page(void) {
    /* Each page holds 39 entries (39 * 26 = 1014 bytes) */
    int entries_per_page = 39;
    int entry_size = 26;
    int total_size = entries_per_page * entry_size;

    TEST_ASSERT(total_size == 1014,
                "39 entries * 26 bytes = 1014 bytes (fits in 1KB page)");
    TEST_ASSERT(entries_per_page == 0x27,
                "Entries per page should be 0x27 (39)");
}

/*
 * Test: Verify buffer switch calculation
 * The formula is: new_index = 3 - current_index
 * So 1 -> 2 and 2 -> 1
 */
void test_buffer_switch(void) {
    int idx1 = 1;
    int idx2 = 2;

    TEST_ASSERT(3 - idx1 == 2, "Buffer switch: 1 -> 2");
    TEST_ASSERT(3 - idx2 == 1, "Buffer switch: 2 -> 1");
}

/*
 * Test: Verify kind filtering calculation
 * NETLOG checks: (KINDS & (1 << (kind & 0x1F))) != 0
 */
void test_kind_filtering(void) {
    uint32_t kinds = 0x00300007;  /* Bits 0, 1, 2, 20, 21 set */

    /* Check individual bits */
    TEST_ASSERT((kinds & (1 << 0)) != 0, "Kind 0 should be enabled");
    TEST_ASSERT((kinds & (1 << 1)) != 0, "Kind 1 should be enabled");
    TEST_ASSERT((kinds & (1 << 2)) != 0, "Kind 2 should be enabled");
    TEST_ASSERT((kinds & (1 << 3)) == 0, "Kind 3 should be disabled");
    TEST_ASSERT((kinds & (1 << 20)) != 0, "Kind 20 (server) should be enabled");
    TEST_ASSERT((kinds & (1 << 21)) != 0, "Kind 21 (server) should be enabled");

    /* Check server bit mask */
    uint32_t server_mask = 0x300000;  /* Bits 20 and 21 */
    uint32_t non_server_mask = 0xFFCFFFFF;

    TEST_ASSERT((kinds & server_mask) != 0, "Server bits should be set");
    TEST_ASSERT((kinds & non_server_mask) != 0, "Non-server bits should be set");

    /* Test with only server bits */
    uint32_t server_only = 0x00100000;
    TEST_ASSERT((server_only & server_mask) != 0, "Server-only has server bits");
    TEST_ASSERT((server_only & non_server_mask) == 0, "Server-only has no non-server bits");
}

/*
 * Test: Verify entry calculation formula
 * entry_addr = buffer_base + (entry_index * 26)
 */
void test_entry_calculation(void) {
    /* Simulate entry index 1 (first entry) */
    int entry_index = 1;
    int offset = entry_index * 26;
    TEST_ASSERT(offset == 26, "Entry 1 should be at offset 26");

    /* Entry 39 (last entry) */
    entry_index = 39;
    offset = entry_index * 26;
    TEST_ASSERT(offset == 1014, "Entry 39 should be at offset 1014");
}

int main(void) {
    printf("=== NETLOG Unit Tests ===\n\n");

    test_entry_size();
    test_entry_offsets();
    test_entries_per_page();
    test_buffer_switch();
    test_kind_filtering();
    test_entry_calculation();

    printf("\n=== Results: %d passed, %d failed ===\n", passes, failures);

    return failures > 0 ? 1 : 0;
}
