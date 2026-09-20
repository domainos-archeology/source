/*
 * disk/test/test_as_io_setup.c - Unit tests for AS_IO_SETUP (0x00E6B74C)
 *
 * disk/as_io_setup.c is #included below with MST_$WIRE and
 * CACHE_$FLUSH_VIRTUAL mocked and a host copy of the disk module data area
 * (DISK_VOL() indexes DISK_VOLUME_BASE, so the host build must be able to
 * place the volume table: the tests go through the same macro the code
 * does).
 */

#include <stdio.h>
#include <string.h>

#include "disk/disk_internal.h"
#include "mst/mst.h"
#include "cache/cache.h"
#include "proc1/proc1.h"

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

/* ============================================================================
 * Mocks
 * ============================================================================ */

uint8_t DISK_$DATA[DISK_$DATA_SIZE];
uint16_t PROC1_$CURRENT = 7;

/* DISK_VOLUME_BASE is a fixed address on m68k; point DISK_VOL() at the
 * host copy of the module data instead. */
#undef DISK_VOLUME_BASE
#define DISK_VOLUME_BASE (DISK_$DATA)

static int wire_calls;
static int flush_calls;
static uint32_t wire_last_vpn;
static status_$t wire_status;
static uint32_t wire_result;

uint32_t MST_$WIRE(uint32_t vpn, status_$t *status_ret)
{
    wire_calls++;
    wire_last_vpn = vpn;
    *status_ret = wire_status;
    return wire_result;
}

void CACHE_$FLUSH_VIRTUAL(void) { flush_calls++; }

#include "../as_io_setup.c"

static void reset(void)
{
    memset(DISK_$DATA, 0, sizeof DISK_$DATA);
    wire_calls = flush_calls = 0;
    wire_status = 0;
    wire_result = 0x1234;
    PROC1_$CURRENT = 7;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(volume_index_0_is_invalid)
{
    uint16_t idx = 0;
    status_$t st = 0;
    reset();
    (void)AS_IO_SETUP(&idx, 0x400, &st);
    ASSERT_EQ(status_$invalid_volume_index, st);
    ASSERT_EQ(0, wire_calls);
    ASSERT_EQ(0, flush_calls);
}

TEST(volume_index_11_is_invalid)
{
    uint16_t idx = 11;
    status_$t st = 0;
    reset();
    (void)AS_IO_SETUP(&idx, 0x400, &st);
    ASSERT_EQ(status_$invalid_volume_index, st);
}

/* btst.l D0,D1 counts bits modulo 32: index 33 tests bit 1 and passes the
 * mask check, then falls to the alignment check. */
TEST(volume_index_wraps_modulo_32)
{
    uint16_t idx = 33;
    status_$t st = 0;
    reset();
    (void)AS_IO_SETUP(&idx, 0x401, &st);
    ASSERT_EQ(status_$disk_buffer_not_page_aligned, st);
}

TEST(unaligned_buffer_rejected_before_mount_check)
{
    uint16_t idx = 3;
    status_$t st = 0;
    reset();
    (void)AS_IO_SETUP(&idx, 0x10200, &st);
    ASSERT_EQ(status_$disk_buffer_not_page_aligned, st);
    ASSERT_EQ(0, wire_calls);
}

TEST(not_assigned_rejected)
{
    uint16_t idx = 3;
    status_$t st = 0;
    reset();
    DISK_VOL(3)->mount_state = 1;
    DISK_VOL(3)->mount_proc = 7;
    (void)AS_IO_SETUP(&idx, 0x10000, &st);
    ASSERT_EQ(status_$volume_not_properly_mounted, st);
    ASSERT_EQ(0, wire_calls);
}

TEST(assigned_to_other_process_rejected)
{
    uint16_t idx = 3;
    status_$t st = 0;
    reset();
    DISK_VOL(3)->mount_state = 2;
    DISK_VOL(3)->mount_proc = 8;
    (void)AS_IO_SETUP(&idx, 0x10000, &st);
    ASSERT_EQ(status_$volume_not_properly_mounted, st);
}

TEST(success_wires_and_flushes)
{
    uint16_t idx = 10;
    status_$t st = 0x11111111;
    uint32_t r;
    reset();
    DISK_VOL(10)->mount_state = 2;
    DISK_VOL(10)->mount_proc = 7;
    wire_result = 0x00ABC000;
    r = AS_IO_SETUP(&idx, 0x20400, &st);
    ASSERT_EQ(0x00ABC000, r);
    ASSERT_EQ(1, wire_calls);
    ASSERT_EQ(0x20400, wire_last_vpn);
    ASSERT_EQ(0, st);
    ASSERT_EQ(1, flush_calls);
}

/* A nonzero LOW status word gets bit 31 set; the cache is still flushed. */
TEST(wire_error_sets_bit_31)
{
    uint16_t idx = 1;
    status_$t st = 0;
    reset();
    DISK_VOL(1)->mount_state = 2;
    DISK_VOL(1)->mount_proc = 7;
    wire_status = 0x00040005;
    (void)AS_IO_SETUP(&idx, 0x400, &st);
    ASSERT_EQ((status_$t)0x80040005, st);
    ASSERT_EQ(1, flush_calls);
}

/* A status whose low word is zero (subsystem code only) is left alone. */
TEST(wire_status_with_zero_low_word_untouched)
{
    uint16_t idx = 1;
    status_$t st = 0;
    reset();
    DISK_VOL(1)->mount_state = 2;
    DISK_VOL(1)->mount_proc = 7;
    wire_status = 0x00040000;
    (void)AS_IO_SETUP(&idx, 0x400, &st);
    ASSERT_EQ(0x00040000, st);
}

int main(void)
{
    printf("test_as_io_setup:\n");
    RUN_TEST(volume_index_0_is_invalid);
    RUN_TEST(volume_index_11_is_invalid);
    RUN_TEST(volume_index_wraps_modulo_32);
    RUN_TEST(unaligned_buffer_rejected_before_mount_check);
    RUN_TEST(not_assigned_rejected);
    RUN_TEST(assigned_to_other_process_rejected);
    RUN_TEST(success_wires_and_flushes);
    RUN_TEST(wire_error_sets_bit_31);
    RUN_TEST(wire_status_with_zero_low_word_untouched);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed != 0;
}
