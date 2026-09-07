/*
 * netlog/test/test_netlog.c - Layout tests for the NETLOG records.
 *
 * These use the REAL netlog/netlog.h and netlog/netlog_internal.h types (the
 * file previously carried a private copy of netlog_entry_t, so it could not
 * catch a change to the shipped one) and check the two derived quantities the
 * disassembly pins down:
 *   - 39 entries of 26 bytes fit in the 1KB wired page NETLOG_$CNTL allocates
 *     (0x00E71C14 `cmpi.w #0x27,(0x6e,A1)`),
 *   - NETLOG_ENTRY_ADDR turns the 1-based counter value into the record the
 *     original writes, which begins at (count - 1) * 26 because every store
 *     at 0x00E71BCE..0x00E71C02 uses a negative displacement from
 *     `current_buf_ptr + count*26`.
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#define TEST_ASSERT(cond, msg) do {                                           \
    if (!(cond)) {                                                            \
        printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                \
        failures++;                                                           \
    } else {                                                                  \
        printf("PASS: %s\n", msg);                                            \
        passes++;                                                             \
    }                                                                         \
} while (0)

static int failures = 0;
static int passes = 0;

#include "netlog/netlog_internal.h"

/* netlog_internal.h declares these; the layout tests do not call any code. */
netlog_data_t netlog_data;

static void test_entry_size(void)
{
    TEST_ASSERT(sizeof(netlog_entry_t) == 26,
                "netlog_entry_t is 26 bytes");
    TEST_ASSERT(NETLOG_ENTRY_SIZE == 26,
                "NETLOG_ENTRY_SIZE is 26");
}

static void test_entry_offsets(void)
{
    TEST_ASSERT(offsetof(netlog_entry_t, kind) == 0x00, "kind at 0x00");
    TEST_ASSERT(offsetof(netlog_entry_t, process_id) == 0x01, "process_id at 0x01");
    TEST_ASSERT(offsetof(netlog_entry_t, timestamp) == 0x02, "timestamp at 0x02");
    TEST_ASSERT(offsetof(netlog_entry_t, uid_high) == 0x06, "uid_high at 0x06");
    TEST_ASSERT(offsetof(netlog_entry_t, uid_low) == 0x0A, "uid_low at 0x0A");
    TEST_ASSERT(offsetof(netlog_entry_t, param3) == 0x0E, "param3 at 0x0E");
    TEST_ASSERT(offsetof(netlog_entry_t, param4) == 0x10, "param4 at 0x10");
    TEST_ASSERT(offsetof(netlog_entry_t, param5) == 0x12, "param5 at 0x12");
    TEST_ASSERT(offsetof(netlog_entry_t, param6) == 0x14, "param6 at 0x14");
    TEST_ASSERT(offsetof(netlog_entry_t, param7) == 0x16, "param7 at 0x16");
    TEST_ASSERT(offsetof(netlog_entry_t, param8) == 0x18, "param8 at 0x18");
}

static void test_data_offsets(void)
{
    TEST_ASSERT(offsetof(netlog_data_t, page_counts) == 0x70,
                "page_counts at 0x70");
    TEST_ASSERT(offsetof(netlog_data_t, current_buf_ptr) == 0x74,
                "current_buf_ptr at 0x74 (so page_counts holds exactly two)");
    TEST_ASSERT(sizeof(netlog_data.page_counts) == 4,
                "page_counts is two words wide");
    TEST_ASSERT(offsetof(netlog_data_t, buffer_va) == 0x54 &&
                sizeof(netlog_data.buffer_va) == 12,
                "buffer_va is three longwords at 0x54 (element 0 unused)");
}

static void test_page_capacity(void)
{
    TEST_ASSERT(NETLOG_ENTRIES_PER_PAGE == 0x27,
                "NETLOG_ENTRIES_PER_PAGE is 0x27 (39)");
    TEST_ASSERT(NETLOG_ENTRIES_PER_PAGE * NETLOG_ENTRY_SIZE == 1014,
                "39 * 26 = 1014 bytes, which fits a 1KB page");
    TEST_ASSERT((NETLOG_ENTRIES_PER_PAGE + 1) * NETLOG_ENTRY_SIZE > 1024,
                "a 40th entry would not fit");
}

/* NETLOG_ENTRY_ADDR takes the 1-based counter value. */
static void test_entry_address(void)
{
    char base[1024];

    TEST_ASSERT((char *)NETLOG_ENTRY_ADDR(base, 1) == base,
                "counter value 1 addresses offset 0");
    TEST_ASSERT((char *)NETLOG_ENTRY_ADDR(base, 2) == base + 26,
                "counter value 2 addresses offset 26");
    TEST_ASSERT((char *)NETLOG_ENTRY_ADDR(base, 39) == base + 988,
                "counter value 39 addresses offset 988");
    TEST_ASSERT((char *)NETLOG_ENTRY_ADDR(base, 39) + NETLOG_ENTRY_SIZE ==
                base + 1014,
                "the 39th record ends at 1014, inside the page");
}

/* NETLOG_$CNTL's flag arithmetic (0x00E71AF0..0x00E71B14). */
static void test_kind_masks(void)
{
    uint32_t kinds = 0x00300007;

    TEST_ASSERT((kinds & ((1u << 21) | (1u << 20))) != 0,
                "bits 20/21 select server logging");
    TEST_ASSERT((kinds & 0xFFCFFFFFu) != 0,
                "andi.l #-0x300001 keeps the non-server bits");
    TEST_ASSERT((0x00100000u & 0xFFCFFFFFu) == 0,
                "a server-only mask leaves no general bits");
}

int main(void)
{
    printf("=== NETLOG layout tests ===\n\n");

    test_entry_size();
    test_entry_offsets();
    test_data_offsets();
    test_page_capacity();
    test_entry_address();
    test_kind_masks();

    printf("\n=== Results: %d passed, %d failed ===\n", passes, failures);
    return failures > 0 ? 1 : 0;
}
