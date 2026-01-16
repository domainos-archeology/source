/*
 * DMA Subsystem Tests
 *
 * These tests validate the DMA subsystem initialization routines.
 * Since we can't access real hardware, we mock the DMA controller
 * memory-mapped registers with test buffers.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

/* Include the register definitions directly for testing */
#define M68450_REG_CSR      0x00
#define M68450_REG_CCR      0x07
#define M68450_REG_DCR      0x04
#define M68450_REG_SCR      0x06
#define M68450_REG_CPR      0x2D

#define M68450_CCR_SAB      0x10

/* Mock DMA channel buffer - large enough for one channel */
#define CHANNEL_SIZE        0x40
static uint8_t mock_channel[CHANNEL_SIZE];

/*
 * Prototype for the function under test (matches dma.h)
 */
void DMA_$INIT_M68450_CHANNEL(uint8_t *chan_virtual_address, int16_t channel_number);


/*
 * Test DMA_$INIT_M68450_CHANNEL with channel 0
 */
static void test_init_channel_0(void) {
    printf("Testing DMA_$INIT_M68450_CHANNEL (channel 0)...\n");

    /* Clear the mock channel buffer */
    memset(mock_channel, 0, sizeof(mock_channel));

    /* Initialize channel 0 */
    DMA_$INIT_M68450_CHANNEL(mock_channel, 0);

    /* Verify CCR was set to software abort (0x10) */
    assert(mock_channel[M68450_REG_CCR] == M68450_CCR_SAB);
    printf("  CCR = 0x%02X (expected 0x10) OK\n", mock_channel[M68450_REG_CCR]);

    /* Verify CSR was set to 0xFF (clear all status bits) */
    assert(mock_channel[M68450_REG_CSR] == 0xFF);
    printf("  CSR = 0x%02X (expected 0xFF) OK\n", mock_channel[M68450_REG_CSR]);

    /* Verify DCR was set to 0x28 */
    assert(mock_channel[M68450_REG_DCR] == 0x28);
    printf("  DCR = 0x%02X (expected 0x28) OK\n", mock_channel[M68450_REG_DCR]);

    /* Verify SCR was set to 0x04 */
    assert(mock_channel[M68450_REG_SCR] == 0x04);
    printf("  SCR = 0x%02X (expected 0x04) OK\n", mock_channel[M68450_REG_SCR]);

    /* Verify CPR was set to channel number (0) */
    assert(mock_channel[M68450_REG_CPR] == 0);
    printf("  CPR = 0x%02X (expected 0x00) OK\n", mock_channel[M68450_REG_CPR]);

    printf("  PASSED\n");
}


/*
 * Test DMA_$INIT_M68450_CHANNEL with channel 3
 */
static void test_init_channel_3(void) {
    printf("Testing DMA_$INIT_M68450_CHANNEL (channel 3)...\n");

    /* Clear the mock channel buffer */
    memset(mock_channel, 0, sizeof(mock_channel));

    /* Initialize channel 3 */
    DMA_$INIT_M68450_CHANNEL(mock_channel, 3);

    /* Verify CCR was set to software abort (0x10) */
    assert(mock_channel[M68450_REG_CCR] == M68450_CCR_SAB);

    /* Verify CSR was set to 0xFF */
    assert(mock_channel[M68450_REG_CSR] == 0xFF);

    /* Verify DCR was set to 0x28 */
    assert(mock_channel[M68450_REG_DCR] == 0x28);

    /* Verify SCR was set to 0x04 */
    assert(mock_channel[M68450_REG_SCR] == 0x04);

    /* Verify CPR was set to channel number (3) */
    assert(mock_channel[M68450_REG_CPR] == 3);
    printf("  CPR = 0x%02X (expected 0x03) OK\n", mock_channel[M68450_REG_CPR]);

    printf("  PASSED\n");
}


/*
 * Test register offset definitions match M68450 datasheet
 */
static void test_register_offsets(void) {
    printf("Testing M68450 register offset definitions...\n");

    /* These offsets are from the Motorola MC68450 datasheet */
    assert(M68450_REG_CSR == 0x00);  /* Channel Status Register */
    assert(M68450_REG_DCR == 0x04);  /* Device Control Register */
    assert(M68450_REG_SCR == 0x06);  /* Sequence Control Register */
    assert(M68450_REG_CCR == 0x07);  /* Channel Control Register */
    assert(M68450_REG_CPR == 0x2D);  /* Channel Priority Register */

    printf("  All register offsets match M68450 datasheet\n");
    printf("  PASSED\n");
}


/*
 * Test that other registers are not modified during init
 */
static void test_unmodified_registers(void) {
    printf("Testing that unrelated registers are not modified...\n");

    /* Fill with sentinel value */
    memset(mock_channel, 0xAA, sizeof(mock_channel));

    /* Initialize channel 0 */
    DMA_$INIT_M68450_CHANNEL(mock_channel, 0);

    /* Check some registers that should NOT be modified */
    /* Register at offset 0x01 (CER - Channel Error Register) */
    assert(mock_channel[0x01] == 0xAA);

    /* Register at offset 0x05 (OCR - Operation Control Register) */
    assert(mock_channel[0x05] == 0xAA);

    /* Registers at offset 0x08-0x0F (transfer counters, addresses) */
    for (int i = 0x08; i <= 0x0F; i++) {
        if (i != M68450_REG_CSR && i != M68450_REG_CCR &&
            i != M68450_REG_DCR && i != M68450_REG_SCR) {
            assert(mock_channel[i] == 0xAA);
        }
    }

    printf("  PASSED\n");
}


int main(void) {
    printf("=== DMA Subsystem Tests ===\n\n");

    test_register_offsets();
    test_init_channel_0();
    test_init_channel_3();
    test_unmodified_registers();

    printf("\n=== All tests passed ===\n");
    return 0;
}
