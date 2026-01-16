/*
 * DMA Subsystem Public Header
 *
 * Definitions and functions for the Motorola M68450 DMA Controller (DMAC)
 * used in the Apollo DN300/DN400 series workstations.
 */

#ifndef DMA_DMA_H
#define DMA_DMA_H

#include "base/base.h"

/*
 * M68450 (MC68450) DMA Controller Register Offsets
 *
 * The M68450 is a 4-channel DMA controller. Each channel has its own
 * register set at a 0x40 byte offset from the base address.
 */

/* Channel Status Register - Read to get status, write 0xFF to clear */
#define M68450_REG_CSR      0x00

/* Channel Error Register - Contains error information when errors occur */
#define M68450_REG_CER      0x01

/* Device Control Register - Controls device interface timing and characteristics */
#define M68450_REG_DCR      0x04

/* Operation Control Register - Controls DMA operation mode */
#define M68450_REG_OCR      0x05

/* Sequence Control Register - Controls memory/device access sequencing */
#define M68450_REG_SCR      0x06

/* Channel Control Register - Main control register for channel operation */
#define M68450_REG_CCR      0x07

/* Memory Transfer Counter (high word) */
#define M68450_REG_MTCH     0x0A

/* Memory Transfer Counter (low word) */
#define M68450_REG_MTCL     0x0B

/* Memory Address Register (high word) */
#define M68450_REG_MARH     0x0C

/* Memory Address Register (low word) */
#define M68450_REG_MARL     0x0E

/* Device Address Register (high word) */
#define M68450_REG_DARH     0x14

/* Device Address Register (low word) */
#define M68450_REG_DARL     0x16

/* Base Transfer Counter (high word) */
#define M68450_REG_BTCH     0x1A

/* Base Transfer Counter (low word) */
#define M68450_REG_BTCL     0x1B

/* Base Address Register (high word) */
#define M68450_REG_BARH     0x1C

/* Base Address Register (low word) */
#define M68450_REG_BARL     0x1E

/* Normal Interrupt Vector */
#define M68450_REG_NIV      0x25

/* Error Interrupt Vector */
#define M68450_REG_EIV      0x27

/* Memory Function Code Register */
#define M68450_REG_MFC      0x29

/* Channel Priority Register */
#define M68450_REG_CPR      0x2D

/* Device Function Code Register */
#define M68450_REG_DFC      0x31

/* Base Function Code Register */
#define M68450_REG_BFC      0x39

/* General Control Register (shared across all channels, at channel 0 base + 0x3F) */
#define M68450_REG_GCR      0x3F


/*
 * Channel Control Register (CCR) bit definitions
 */
#define M68450_CCR_STR      0x80    /* Start channel operation */
#define M68450_CCR_CNT      0x40    /* Continue operation (after halt) */
#define M68450_CCR_HLT      0x20    /* Halt channel operation */
#define M68450_CCR_SAB      0x10    /* Software abort */
#define M68450_CCR_INT      0x08    /* Interrupt enable */


/*
 * Device Control Register (DCR) bit definitions
 */
#define M68450_DCR_XRM_MASK 0xC0    /* External Request Mode */
#define M68450_DCR_DTYP_MASK 0x30   /* Device Type */
#define M68450_DCR_DPS      0x08    /* Device Port Size (0=8-bit, 1=16-bit) */
#define M68450_DCR_PCL_MASK 0x03    /* Peripheral Control Lines */


/*
 * Sequence Control Register (SCR) bit definitions
 */
#define M68450_SCR_MAC_MASK 0x0C    /* Memory Address Count mode */
#define M68450_SCR_DAC_MASK 0x03    /* Device Address Count mode */


/*
 * DN300/DN400 DMAC Channel Virtual Addresses
 *
 * The DMA controller is memory-mapped at these addresses.
 * Each channel is 0x40 bytes apart.
 */
#define DN300_DMAC_BASE_ADDRESS         0x00FFA000
#define DN300_DMAC_CHANNEL_SIZE         0x40

#define DN300_DMAC_CHAN0_VIRTUAL_ADDRESS (DN300_DMAC_BASE_ADDRESS + (0 * DN300_DMAC_CHANNEL_SIZE))
#define DN300_DMAC_CHAN1_VIRTUAL_ADDRESS (DN300_DMAC_BASE_ADDRESS + (1 * DN300_DMAC_CHANNEL_SIZE))
#define DN300_DMAC_CHAN2_VIRTUAL_ADDRESS (DN300_DMAC_BASE_ADDRESS + (2 * DN300_DMAC_CHANNEL_SIZE))
#define DN300_DMAC_CHAN3_VIRTUAL_ADDRESS (DN300_DMAC_BASE_ADDRESS + (3 * DN300_DMAC_CHANNEL_SIZE))

#define DN300_DMAC_NUM_CHANNELS         4


/*
 * Public function prototypes
 */

/**
 * DMA_$INIT - Initialize all DMA channels
 *
 * Initializes all four channels of the M68450 DMA controller to a known
 * safe state with software abort asserted and interrupts disabled.
 */
void DMA_$INIT(void);

/**
 * DMA_$INIT_M68450_CHANNEL - Initialize a single DMA channel
 *
 * @param chan_virtual_address  Virtual address of the DMA channel registers
 * @param channel_number        Channel number (0-3) for priority assignment
 *
 * Initializes a single channel by:
 *   - Asserting software abort (CCR = 0x10)
 *   - Clearing all status bits (CSR = 0xFF)
 *   - Setting device control for explicit 68000 compatible mode (DCR = 0x28)
 *   - Setting sequence control for no address counting (SCR = 0x04)
 *   - Setting channel priority to the channel number (CPR = channel_number)
 */
void DMA_$INIT_M68450_CHANNEL(uint8_t *chan_virtual_address, int16_t channel_number);


#endif /* DMA_DMA_H */
