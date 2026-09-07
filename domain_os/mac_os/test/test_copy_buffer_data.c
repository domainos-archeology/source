/*
 * mac_os/test/test_copy_buffer_data.c - Unit tests for
 * MAC_OS_$COPY_BUFFER_DATA (0x00E0B522)
 *
 * The real mac_os/copy_buffer_data.c is #included below.  The function is a
 * nested procedure of MAC_OS_$SEND that reads and writes two uplevel
 * variables; the flattened version takes them by address, and these tests
 * check that they come back with the values the original leaves behind.
 *
 * Covered:
 *   - a copy that fits inside one chain entry leaves the chain alone and
 *     advances the offset (0x00E0B580)
 *   - a copy that consumes an entry exactly still only bumps the offset,
 *     because the "advance the chain" arm is taken on `bne`, not on `beq`
 *     (0x00E0B57E)
 *   - a copy that spans entries clears the offset and follows .next
 *   - the caller's destination pointer is NOT written back: D4 is loaded
 *     once at 0x00E0B536 and never stored
 *   - the loop stops on a short chain, leaving the request partly done
 *   - a zero-length request copies nothing
 */

#include <stdio.h>
#include <string.h>

#include "mac_os/mac_os_internal.h"
#include "os/os.h"
#include "arch/arch.h"

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
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_STR_PREFIX(expected, buf) do { \
    if (memcmp((expected), (buf), strlen(expected)) != 0) { \
        printf("FAILED\n    Expected \"%s\", got \"%.16s\" at line %d\n", \
               (expected), (const char *)(buf), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ============================================================================
 * Mocks
 * ============================================================================ */

static int data_copy_calls;

void OS_$DATA_COPY(const void *src, void *dst, uint32_t len)
{
    data_copy_calls++;
    memcpy(dst, src, len);
}

/* ============================================================================
 * Code under test
 * ============================================================================ */

#include "../copy_buffer_data.c"

/* ============================================================================
 * Fixtures
 * ============================================================================ */

/*
 * The record's address/next fields are 32-bit target VAs, so every object the
 * code under test dereferences through them has to live inside one arena and
 * ARCH_HOST_VA_BASE has to point at it (see arch/host/arch.h).
 */
static uint8_t arena[256];

#define ARENA_SRC_A   0x10
#define ARENA_SRC_B   0x20
#define ARENA_ENT_A   0x30
#define ARENA_ENT_B   0x40
#define ARENA_DEST    0x60

#define ARENA_PTR(off) ((void *)(arena + (off)))

static mac_os_$buf_desc_t *entry_a = (mac_os_$buf_desc_t *)0;
static mac_os_$buf_desc_t *entry_b = (mac_os_$buf_desc_t *)0;
static char *dest;

static void build_chain(void)
{
    ARCH_HOST_VA_BASE = (uintptr_t)arena;

    memset(arena, 0, sizeof(arena));
    data_copy_calls = 0;

    memcpy(ARENA_PTR(ARENA_SRC_A), "ABCDEFGH", 8);
    memcpy(ARENA_PTR(ARENA_SRC_B), "ijklmnop", 8);

    entry_a = (mac_os_$buf_desc_t *)ARENA_PTR(ARENA_ENT_A);
    entry_b = (mac_os_$buf_desc_t *)ARENA_PTR(ARENA_ENT_B);
    dest    = (char *)ARENA_PTR(ARENA_DEST);

    entry_a->length = 8;
    entry_a->address = ARENA_SRC_A;
    entry_a->next = ARENA_ENT_B;

    entry_b->length = 8;
    entry_b->address = ARENA_SRC_B;
    entry_b->next = 0;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(partial_entry_bumps_the_offset_only)
{
    mac_os_$buf_desc_t *chain;
    int16_t offset = 0;
    uint32_t dest_va;

    build_chain();
    chain = entry_a;
    dest_va = ARENA_DEST;

    MAC_OS_$COPY_BUFFER_DATA(&dest_va, 3, &chain, &offset);

    ASSERT_EQ(1, data_copy_calls);
    ASSERT_STR_PREFIX("ABC", dest);
    ASSERT_EQ(0, dest[3]);
    ASSERT_EQ(3, offset);
    ASSERT_EQ((uintptr_t)entry_a, (uintptr_t)chain);
}

TEST(exact_entry_does_not_advance_the_chain)
{
    mac_os_$buf_desc_t *chain;
    int16_t offset = 0;
    uint32_t dest_va;

    build_chain();
    chain = entry_a;
    dest_va = ARENA_DEST;

    MAC_OS_$COPY_BUFFER_DATA(&dest_va, 8, &chain, &offset);

    ASSERT_STR_PREFIX("ABCDEFGH", dest);
    /* 0x00E0B57E: sub.w D5w,D3w / bne -> advance.  Zero remaining takes the
     * other arm, so the offset moves and the chain pointer does not. */
    ASSERT_EQ(8, offset);
    ASSERT_EQ((uintptr_t)entry_a, (uintptr_t)chain);
}

TEST(spanning_entries_resets_offset_and_follows_next)
{
    mac_os_$buf_desc_t *chain;
    int16_t offset = 0;
    uint32_t dest_va;

    build_chain();
    chain = entry_a;
    dest_va = ARENA_DEST;

    MAC_OS_$COPY_BUFFER_DATA(&dest_va, 12, &chain, &offset);

    ASSERT_EQ(2, data_copy_calls);
    ASSERT_STR_PREFIX("ABCDEFGHijkl", dest);
    ASSERT_EQ(4, offset);
    ASSERT_EQ((uintptr_t)entry_b, (uintptr_t)chain);
}

TEST(destination_pointer_is_not_written_back)
{
    mac_os_$buf_desc_t *chain;
    int16_t offset = 0;
    uint32_t dest_va;

    build_chain();
    chain = entry_a;
    dest_va = ARENA_DEST;

    MAC_OS_$COPY_BUFFER_DATA(&dest_va, 5, &chain, &offset);

    ASSERT_EQ((uint32_t)ARENA_DEST, dest_va);
}

TEST(short_chain_stops_early)
{
    mac_os_$buf_desc_t *chain;
    int16_t offset = 0;
    uint32_t dest_va;

    build_chain();
    entry_a->next = 0;              /* one entry only */
    chain = entry_a;
    dest_va = ARENA_DEST;

    MAC_OS_$COPY_BUFFER_DATA(&dest_va, 12, &chain, &offset);

    ASSERT_EQ(1, data_copy_calls);
    ASSERT_STR_PREFIX("ABCDEFGH", dest);
    ASSERT_EQ(0, dest[8]);
    ASSERT_EQ(0, offset);
    ASSERT_EQ((uintptr_t)NULL, (uintptr_t)chain);
}

TEST(zero_length_copies_nothing)
{
    mac_os_$buf_desc_t *chain;
    int16_t offset = 2;
    uint32_t dest_va;

    build_chain();
    chain = entry_a;
    dest_va = ARENA_DEST;

    MAC_OS_$COPY_BUFFER_DATA(&dest_va, 0, &chain, &offset);

    ASSERT_EQ(0, data_copy_calls);
    ASSERT_EQ(2, offset);
    ASSERT_EQ((uintptr_t)entry_a, (uintptr_t)chain);
}

TEST(resumes_from_a_non_zero_offset)
{
    mac_os_$buf_desc_t *chain;
    int16_t offset = 5;
    uint32_t dest_va;

    build_chain();
    chain = entry_a;
    dest_va = ARENA_DEST;

    MAC_OS_$COPY_BUFFER_DATA(&dest_va, 5, &chain, &offset);

    ASSERT_EQ(2, data_copy_calls);
    ASSERT_STR_PREFIX("FGHij", dest);
    ASSERT_EQ(2, offset);
    ASSERT_EQ((uintptr_t)entry_b, (uintptr_t)chain);
}

int main(void)
{
    printf("test_copy_buffer_data:\n");

    RUN_TEST(partial_entry_bumps_the_offset_only);
    RUN_TEST(exact_entry_does_not_advance_the_chain);
    RUN_TEST(spanning_entries_resets_offset_and_follows_next);
    RUN_TEST(destination_pointer_is_not_written_back);
    RUN_TEST(short_chain_stops_early);
    RUN_TEST(zero_length_copies_nothing);
    RUN_TEST(resumes_from_a_non_zero_offset);

    printf("\n  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
