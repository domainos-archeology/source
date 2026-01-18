/*
 * Test cases for PARITY_$CHK_IO
 *
 * These tests verify the I/O parity check functionality which allows
 * I/O subsystems to check if their buffer addresses were involved
 * in a parity error during DMA.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

/* Mock parity state */
static uint32_t mock_err_ppn;
static uint16_t mock_err_status;
static int8_t   mock_during_dma;

/* Macros to use mock state */
#define PARITY_$ERR_PPN         mock_err_ppn
#define PARITY_$ERR_STATUS      mock_err_status
#define PARITY_$DURING_DMA      mock_during_dma

/*
 * Test implementation of PARITY_$CHK_IO
 */
uint32_t test_PARITY_$CHK_IO(uint32_t ppn1, uint32_t ppn2)
{
    uint32_t result;

    /*
     * Check if there's a pending parity error during DMA.
     * The DMA flag is bit 3 (0x08) of the status word.
     */
    if ((PARITY_$ERR_STATUS & 0x08) == 0) {
        /* No DMA parity error pending */
        result = ppn2 & 0xFFFF0000;
        goto done;
    }

    /* Check if first PPN matches */
    if (ppn1 == PARITY_$ERR_PPN) {
        result = 1;
        goto clear_and_done;
    }

    /* Check if second PPN matches */
    if (ppn2 == PARITY_$ERR_PPN) {
        result = 2;
        goto clear_and_done;
    }

    /* Neither matches */
    result = ppn2 & 0xFFFF0000;
    goto done;

clear_and_done:
    PARITY_$ERR_PPN = 0;
    PARITY_$DURING_DMA = 0;

done:
    return result;
}

/* Reset test state */
void reset_state(void)
{
    mock_err_ppn = 0;
    mock_err_status = 0;
    mock_during_dma = 0;
}

/* Test: No DMA error returns high word of ppn2 */
void test_no_dma_error(void)
{
    reset_state();

    /* Status bit 3 not set - no DMA error */
    mock_err_status = 0x00;
    mock_err_ppn = 0x100;

    uint32_t result = test_PARITY_$CHK_IO(0x100, 0xABCD0000);
    assert(result == 0xABCD0000);  /* Returns high word of ppn2 */

    /* Error state should be unchanged */
    assert(mock_err_ppn == 0x100);

    printf("test_no_dma_error: PASSED\n");
}

/* Test: First address matches */
void test_first_address_match(void)
{
    reset_state();

    mock_err_status = 0x08;  /* DMA error flag set */
    mock_err_ppn = 0x100;
    mock_during_dma = -1;

    uint32_t result = test_PARITY_$CHK_IO(0x100, 0x200);
    assert(result == 1);

    /* State should be cleared */
    assert(mock_err_ppn == 0);
    assert(mock_during_dma == 0);

    printf("test_first_address_match: PASSED\n");
}

/* Test: Second address matches */
void test_second_address_match(void)
{
    reset_state();

    mock_err_status = 0x08;  /* DMA error flag set */
    mock_err_ppn = 0x200;
    mock_during_dma = -1;

    uint32_t result = test_PARITY_$CHK_IO(0x100, 0x200);
    assert(result == 2);

    /* State should be cleared */
    assert(mock_err_ppn == 0);
    assert(mock_during_dma == 0);

    printf("test_second_address_match: PASSED\n");
}

/* Test: Neither address matches */
void test_no_match(void)
{
    reset_state();

    mock_err_status = 0x08;  /* DMA error flag set */
    mock_err_ppn = 0x300;
    mock_during_dma = -1;

    uint32_t result = test_PARITY_$CHK_IO(0x100, 0x12340200);
    assert(result == 0x12340000);  /* Returns high word of ppn2 */

    /* State should NOT be cleared */
    assert(mock_err_ppn == 0x300);
    assert(mock_during_dma == -1);

    printf("test_no_match: PASSED\n");
}

/* Test: Multiple calls with same error */
void test_multiple_calls(void)
{
    reset_state();

    mock_err_status = 0x08;
    mock_err_ppn = 0x100;
    mock_during_dma = -1;

    /* First call - no match */
    uint32_t result = test_PARITY_$CHK_IO(0x200, 0x300);
    assert(result == 0);
    assert(mock_err_ppn == 0x100);  /* State preserved */

    /* Second call - match on first */
    result = test_PARITY_$CHK_IO(0x100, 0x300);
    assert(result == 1);
    assert(mock_err_ppn == 0);  /* State cleared */

    /* Third call - no DMA error now */
    mock_err_status = 0x00;  /* DMA flag cleared */
    result = test_PARITY_$CHK_IO(0x100, 0x300);
    assert(result == 0);  /* No match, no DMA flag */

    printf("test_multiple_calls: PASSED\n");
}

/* Test: Status flag bit position */
void test_status_flag_bits(void)
{
    reset_state();

    mock_err_ppn = 0x100;
    mock_during_dma = -1;

    /* Bit 3 (0x08) is the DMA flag */
    mock_err_status = 0x07;  /* All lower bits set, but not bit 3 */
    assert((test_PARITY_$CHK_IO(0x100, 0) & 0xFFFF) == 0);

    mock_err_status = 0x08;  /* Only bit 3 set */
    assert(test_PARITY_$CHK_IO(0x100, 0) == 1);

    /* Reset for next test */
    reset_state();
    mock_err_ppn = 0x100;
    mock_during_dma = -1;

    mock_err_status = 0xF7;  /* All bits except bit 3 */
    assert((test_PARITY_$CHK_IO(0x100, 0) & 0xFFFF) == 0);

    mock_err_status = 0xFF;  /* All bits including bit 3 */
    assert(test_PARITY_$CHK_IO(0x100, 0) == 1);

    printf("test_status_flag_bits: PASSED\n");
}

int main(void)
{
    printf("Running PARITY_$CHK_IO tests...\n\n");

    test_no_dma_error();
    test_first_address_match();
    test_second_address_match();
    test_no_match();
    test_multiple_calls();
    test_status_flag_bits();

    printf("\nAll tests PASSED!\n");
    return 0;
}
