/*
 * Test cases for portable interrupt control macros
 *
 * These tests verify that the DISABLE_INTERRUPTS / ENABLE_INTERRUPTS
 * macros (and GET_SR / SET_SR) behave correctly on the host architecture.
 *
 * The host implementation tracks a logical interrupt disable depth counter
 * (__host_intr_disable_count) so that tests can verify proper nesting.
 *
 * Build:
 *   cc -std=c11 -Wall -Wextra -DARCH_HOST -I. -o /tmp/test_intr arch/test/test_intr.c
 *
 * (Run from domain_os/)
 */

#include <stdio.h>
#include <stdint.h>
#include <assert.h>

/* Provide the host interrupt counter storage */
int __host_intr_disable_count = 0;

/*
 * Include the host interrupt macros directly.
 * We use stdint.h for types (not base/base.h) to avoid conflicts
 * between base.h's custom typedefs and the host platform's stdint.h.
 */
#include "arch/host/intr.h"

/* ===========================================================================
 * Test: basic DISABLE_INTERRUPTS / ENABLE_INTERRUPTS
 * =========================================================================*/
static void test_basic_disable_enable(void)
{
    uint16_t saved_sr;

    assert(__host_intr_disable_count == 0);

    DISABLE_INTERRUPTS(saved_sr);
    assert(__host_intr_disable_count == 1);

    ENABLE_INTERRUPTS(saved_sr);
    assert(__host_intr_disable_count == 0);

    printf("test_basic_disable_enable: PASSED\n");
}

/* ===========================================================================
 * Test: nested DISABLE_INTERRUPTS / ENABLE_INTERRUPTS
 * =========================================================================*/
static void test_nested_disable_enable(void)
{
    uint16_t sr_outer, sr_inner;

    __host_intr_disable_count = 0;

    DISABLE_INTERRUPTS(sr_outer);
    assert(__host_intr_disable_count == 1);

    DISABLE_INTERRUPTS(sr_inner);
    assert(__host_intr_disable_count == 2);

    /* Restore inner - should go back to 1 */
    ENABLE_INTERRUPTS(sr_inner);
    assert(__host_intr_disable_count == 1);

    /* Restore outer - should go back to 0 */
    ENABLE_INTERRUPTS(sr_outer);
    assert(__host_intr_disable_count == 0);

    printf("test_nested_disable_enable: PASSED\n");
}

/* ===========================================================================
 * Test: GET_SR returns correct state
 * =========================================================================*/
static void test_get_sr(void)
{
    uint16_t sr;

    __host_intr_disable_count = 0;

    GET_SR(sr);
    assert(sr == 0);  /* Interrupts enabled -> SR has no IPL bits */

    __host_intr_disable_count = 1;
    GET_SR(sr);
    assert(sr == SR_IPL_DISABLE_ALL);  /* Interrupts disabled */

    __host_intr_disable_count = 0;
    printf("test_get_sr: PASSED\n");
}

/* ===========================================================================
 * Test: SET_SR controls interrupt state
 * =========================================================================*/
static void test_set_sr(void)
{
    __host_intr_disable_count = 0;

    SET_SR(SR_IPL_DISABLE_ALL);
    assert(__host_intr_disable_count > 0);

    SET_SR(0);
    assert(__host_intr_disable_count == 0);

    printf("test_set_sr: PASSED\n");
}

/* ===========================================================================
 * Test: SR constants have expected M68K values
 * =========================================================================*/
static void test_sr_constants(void)
{
    assert(SR_IPL_MASK == 0x0700);
    assert(SR_IPL_DISABLE_ALL == 0x0700);
    assert(SR_SUPERVISOR == 0x2000);
    assert(SR_TRACE == 0x8000);

    /* IPL disable bits should be contained in the mask */
    assert((SR_IPL_DISABLE_ALL & SR_IPL_MASK) == SR_IPL_DISABLE_ALL);

    printf("test_sr_constants: PASSED\n");
}

/* ===========================================================================
 * Test: the pattern used in time/ code works correctly
 *
 * Original code pattern:
 *   GET_SR(saved_sr);
 *   SET_SR(saved_sr | SR_IPL_DISABLE_ALL);
 *   // critical section
 *   SET_SR(saved_sr);
 *
 * This was the M68K-specific pattern. After our conversion to
 * DISABLE_INTERRUPTS/ENABLE_INTERRUPTS, verify the new portable
 * pattern behaves equivalently.
 * =========================================================================*/
static void test_time_pattern(void)
{
    uint16_t saved_sr;

    __host_intr_disable_count = 0;

    /* Portable pattern (post-conversion) */
    DISABLE_INTERRUPTS(saved_sr);
    assert(__host_intr_disable_count > 0);  /* Critical section: disabled */

    /* Simulate reading some data atomically */
    volatile int data = 42;
    (void)data;

    ENABLE_INTERRUPTS(saved_sr);
    assert(__host_intr_disable_count == 0);  /* Restored */

    printf("test_time_pattern: PASSED\n");
}

/* ===========================================================================
 * Test: the pattern used in mmu/ code works correctly
 *
 * mmu/install_list.c previously used:
 *   GET_SR(saved_sr);
 *   uint16_t disabled_sr = saved_sr | SR_IPL_DISABLE_ALL;
 *   SET_SR(disabled_sr);
 *   // install mappings...
 *   SET_SR(saved_sr);
 *
 * After conversion to DISABLE_INTERRUPTS/ENABLE_INTERRUPTS.
 * =========================================================================*/
static void test_mmu_pattern(void)
{
    uint16_t saved_sr;

    __host_intr_disable_count = 0;

    /* Portable pattern (post-conversion) */
    DISABLE_INTERRUPTS(saved_sr);
    assert(__host_intr_disable_count > 0);

    /* Simulate MMU operations */
    volatile int mmu_op = 0;
    mmu_op++;
    (void)mmu_op;

    ENABLE_INTERRUPTS(saved_sr);
    assert(__host_intr_disable_count == 0);

    printf("test_mmu_pattern: PASSED\n");
}

/* ===========================================================================
 * Test: multiple early return paths (like mmu/vtop.c)
 *
 * vtop.c has multiple exit points from a critical section, each
 * restoring interrupts. Verify this pattern works correctly.
 * =========================================================================*/
static void test_early_return_pattern(void)
{
    uint16_t saved_sr;
    int result = 0;

    __host_intr_disable_count = 0;

    DISABLE_INTERRUPTS(saved_sr);
    assert(__host_intr_disable_count > 0);

    /* Simulate early return path 1 */
    if (result == 0) {
        ENABLE_INTERRUPTS(saved_sr);
        assert(__host_intr_disable_count == 0);
    }

    /* Simulate: enter critical section again */
    DISABLE_INTERRUPTS(saved_sr);
    assert(__host_intr_disable_count > 0);

    /* Simulate early return path 2 (different branch) */
    result = 1;
    if (result == 1) {
        ENABLE_INTERRUPTS(saved_sr);
        assert(__host_intr_disable_count == 0);
    }

    printf("test_early_return_pattern: PASSED\n");
}

/* ===========================================================================
 * Test: saved_sr preserves state across DISABLE/ENABLE pairs
 *
 * Verify that the saved state from DISABLE_INTERRUPTS correctly
 * captures the pre-disable state and that ENABLE_INTERRUPTS
 * restores exactly that state.
 * =========================================================================*/
static void test_state_preservation(void)
{
    uint16_t sr1, sr2;

    __host_intr_disable_count = 0;

    /* First disable - saves state "0" (interrupts were enabled) */
    DISABLE_INTERRUPTS(sr1);
    assert(sr1 == 0);
    assert(__host_intr_disable_count == 1);

    /* Second disable - saves state "1" (already disabled once) */
    DISABLE_INTERRUPTS(sr2);
    assert(sr2 == 1);
    assert(__host_intr_disable_count == 2);

    /* Restore to state after first disable */
    ENABLE_INTERRUPTS(sr2);
    assert(__host_intr_disable_count == 1);

    /* Restore to fully enabled */
    ENABLE_INTERRUPTS(sr1);
    assert(__host_intr_disable_count == 0);

    printf("test_state_preservation: PASSED\n");
}

/* ===========================================================================
 * main
 * =========================================================================*/
int main(void)
{
    printf("Running portable interrupt control tests (ARCH_HOST)...\n\n");

    test_basic_disable_enable();
    test_nested_disable_enable();
    test_get_sr();
    test_set_sr();
    test_sr_constants();
    test_time_pattern();
    test_mmu_pattern();
    test_early_return_pattern();
    test_state_preservation();

    printf("\nAll tests PASSED!\n");
    return 0;
}
