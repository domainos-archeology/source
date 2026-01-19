/*
 * SIO2681 - Signetics 2681 DUART Driver
 *
 * This module provides the driver for the Signetics 2681 Dual UART chip
 * used in Apollo workstations for serial I/O. The 2681 provides:
 *   - Two independent full-duplex async channels
 *   - Programmable baud rate generator
 *   - Programmable data format (5-8 bits, 1-2 stop bits, parity)
 *   - Hardware flow control (RTS/CTS, DCD)
 *   - Counter/timer for baud rate generation
 *
 * Register Layout (per chip, 32 bytes):
 *   Channel A: offsets 0x00-0x0F
 *   Channel B: offsets 0x10-0x1F (mirrors A with offset)
 *   Shared:    ACR, IMR, ISR, IPCR at various offsets
 *
 * Memory-mapped base address: 0xFFB000 - (chip_id << 5)
 */

#ifndef SIO2681_H
#define SIO2681_H

#include "base/base.h"
#include "sio/sio.h"

/*
 * ============================================================================
 * Hardware Register Definitions (Signetics 2681 DUART)
 * ============================================================================
 *
 * The 2681 uses different registers for read vs write at same address.
 */

/* Register offsets - Channel A (or shared) */
#define SIO2681_REG_MRA         0x01    /* Mode Register A (R/W) */
#define SIO2681_REG_SRA         0x03    /* Status Register A (R) */
#define SIO2681_REG_CSRA        0x03    /* Clock Select Register A (W) */
#define SIO2681_REG_CRA         0x05    /* Command Register A (W) */
#define SIO2681_REG_RHRA        0x07    /* Receive Holding Register A (R) */
#define SIO2681_REG_THRA        0x07    /* Transmit Holding Register A (W) */
#define SIO2681_REG_IPCR        0x09    /* Input Port Change Register (R) */
#define SIO2681_REG_ACR         0x09    /* Auxiliary Control Register (W) */
#define SIO2681_REG_ISR         0x0B    /* Interrupt Status Register (R) */
#define SIO2681_REG_IMR         0x0B    /* Interrupt Mask Register (W) */
#define SIO2681_REG_CTU         0x0D    /* Counter/Timer Upper (R) */
#define SIO2681_REG_CTUR        0x0D    /* Counter/Timer Upper Register (W) */
#define SIO2681_REG_CTL         0x0F    /* Counter/Timer Lower (R) */
#define SIO2681_REG_CTLR        0x0F    /* Counter/Timer Lower Register (W) */

/* Register offsets - Channel B */
#define SIO2681_REG_MRB         0x11    /* Mode Register B (R/W) */
#define SIO2681_REG_SRB         0x13    /* Status Register B (R) */
#define SIO2681_REG_CSRB        0x13    /* Clock Select Register B (W) */
#define SIO2681_REG_CRB         0x15    /* Command Register B (W) */
#define SIO2681_REG_RHRB        0x17    /* Receive Holding Register B (R) */
#define SIO2681_REG_THRB        0x17    /* Transmit Holding Register B (W) */

/* Shared registers */
#define SIO2681_REG_IVR         0x19    /* Interrupt Vector Register (R/W) - not used by Apollo */
#define SIO2681_REG_IPR         0x1B    /* Input Port Register (R) */
#define SIO2681_REG_OPCR        0x1B    /* Output Port Configuration Register (W) */
#define SIO2681_REG_SOPBC       0x1D    /* Set Output Port Bits Command (W) */
#define SIO2681_REG_ROPBC       0x1F    /* Reset Output Port Bits Command (W) */

/*
 * Command Register (CR) Commands
 */
#define SIO2681_CR_NOP              0x00    /* No operation */
#define SIO2681_CR_RESET_MR_PTR     0x10    /* Reset MR pointer to MR1 */
#define SIO2681_CR_RESET_RX         0x20    /* Reset receiver */
#define SIO2681_CR_RESET_TX         0x30    /* Reset transmitter */
#define SIO2681_CR_RESET_ERROR      0x40    /* Reset error status */
#define SIO2681_CR_RESET_BRK_INT    0x50    /* Reset break change interrupt */
#define SIO2681_CR_START_BREAK      0x60    /* Start break */
#define SIO2681_CR_STOP_BREAK       0x70    /* Stop break */

/* CR transmitter commands (bits 3-2) */
#define SIO2681_CR_TX_NOP           0x00    /* No action on transmitter */
#define SIO2681_CR_TX_ENABLE        0x04    /* Enable transmitter */
#define SIO2681_CR_TX_DISABLE       0x08    /* Disable transmitter */

/* CR receiver commands (bits 1-0) */
#define SIO2681_CR_RX_NOP           0x00    /* No action on receiver */
#define SIO2681_CR_RX_ENABLE        0x01    /* Enable receiver */
#define SIO2681_CR_RX_DISABLE       0x02    /* Disable receiver */

/*
 * Status Register (SR) Bits
 */
#define SIO2681_SR_RXRDY            0x01    /* Receiver ready */
#define SIO2681_SR_FFULL            0x02    /* FIFO full */
#define SIO2681_SR_TXRDY            0x04    /* Transmitter ready */
#define SIO2681_SR_TXEMT            0x08    /* Transmitter empty */
#define SIO2681_SR_OVERRUN          0x10    /* Overrun error */
#define SIO2681_SR_PARITY           0x20    /* Parity error */
#define SIO2681_SR_FRAMING          0x40    /* Framing error */
#define SIO2681_SR_BREAK            0x80    /* Break received */

/*
 * Interrupt Status/Mask Register (ISR/IMR) Bits
 */
#define SIO2681_INT_TXRDY_A         0x01    /* Channel A TxRDY */
#define SIO2681_INT_RXRDY_A         0x02    /* Channel A RxRDY/FFULL */
#define SIO2681_INT_DELTA_BREAK_A   0x04    /* Channel A break change */
#define SIO2681_INT_CTR_READY       0x08    /* Counter ready */
#define SIO2681_INT_TXRDY_B         0x10    /* Channel B TxRDY */
#define SIO2681_INT_RXRDY_B         0x20    /* Channel B RxRDY/FFULL */
#define SIO2681_INT_DELTA_BREAK_B   0x40    /* Channel B break change */
#define SIO2681_INT_INPUT_CHANGE    0x80    /* Input port change */

/*
 * Input Port Change Register (IPCR) Bits
 */
#define SIO2681_IPCR_CTS_A          0x01    /* CTS A current state */
#define SIO2681_IPCR_CTS_B          0x02    /* CTS B current state */
#define SIO2681_IPCR_DCD_A          0x04    /* DCD A current state (active low) */
#define SIO2681_IPCR_DCD_B          0x08    /* DCD B current state (active low) */
#define SIO2681_IPCR_DELTA_CTS_A    0x10    /* CTS A changed */
#define SIO2681_IPCR_DELTA_CTS_B    0x20    /* CTS B changed */
#define SIO2681_IPCR_DELTA_DCD_A    0x40    /* DCD A changed */
#define SIO2681_IPCR_DELTA_DCD_B    0x80    /* DCD B changed */

/*
 * Mode Register 1 (MR1) Bits
 */
#define SIO2681_MR1_BITS_5          0x00    /* 5 data bits */
#define SIO2681_MR1_BITS_6          0x01    /* 6 data bits */
#define SIO2681_MR1_BITS_7          0x02    /* 7 data bits */
#define SIO2681_MR1_BITS_8          0x03    /* 8 data bits */
#define SIO2681_MR1_PARITY_EVEN     0x00    /* Even parity */
#define SIO2681_MR1_PARITY_ODD      0x04    /* Odd parity */
#define SIO2681_MR1_PARITY_SPACE    0x08    /* Space (force 0) */
#define SIO2681_MR1_PARITY_MARK     0x0C    /* Mark (force 1) */
#define SIO2681_MR1_PARITY_NONE     0x10    /* No parity */
#define SIO2681_MR1_ERROR_CHAR      0x00    /* Error mode: character */
#define SIO2681_MR1_ERROR_BLOCK     0x20    /* Error mode: block */
#define SIO2681_MR1_RX_INT_RXRDY    0x00    /* RX interrupt on RxRDY */
#define SIO2681_MR1_RX_INT_FFULL    0x40    /* RX interrupt on FIFO full */
#define SIO2681_MR1_RX_RTS_CONTROL  0x80    /* RTS controlled by RX FIFO */

/*
 * Mode Register 2 (MR2) Bits
 */
#define SIO2681_MR2_STOP_1          0x07    /* 1 stop bit */
#define SIO2681_MR2_STOP_1_5        0x08    /* 1.5 stop bits (5 bits only) */
#define SIO2681_MR2_STOP_2          0x0F    /* 2 stop bits */
#define SIO2681_MR2_CTS_TX_CONTROL  0x10    /* CTS controls transmitter */
#define SIO2681_MR2_MODE_NORMAL     0x00    /* Normal operation */
#define SIO2681_MR2_MODE_ECHO       0x40    /* Auto-echo */
#define SIO2681_MR2_MODE_LOCAL_LOOP 0x80    /* Local loopback */
#define SIO2681_MR2_MODE_REMOTE_LOOP 0xC0   /* Remote loopback */

/*
 * Output Port Configuration Register (OPCR) Bits
 */
#define SIO2681_OPCR_OP3_OUT        0x00    /* OP3 = output port bit 3 */
#define SIO2681_OPCR_OP3_CTR        0x04    /* OP3 = counter/timer output */
#define SIO2681_OPCR_OP4_OUT        0x00    /* OP4 = output port bit 4 */
#define SIO2681_OPCR_OP4_RTS_A      0x01    /* OP4 = RTS A (active low) */
#define SIO2681_OPCR_OP5_OUT        0x00    /* OP5 = output port bit 5 */
#define SIO2681_OPCR_OP5_RTS_B      0x02    /* OP5 = RTS B (active low) */

/*
 * ============================================================================
 * Driver Data Structures
 * ============================================================================
 */

/*
 * SIO2681 Chip Structure
 *
 * One per physical 2681 chip. Contains pointers to hardware registers
 * and shared state for both channels.
 *
 * Size: 0x0C bytes (12 bytes)
 */
typedef struct sio2681_chip {
    volatile uint8_t *regs;     /* 0x00: Base address of chip registers */
    uint16_t    config1;        /* 0x04: Configuration word 1 */
    uint16_t    config2;        /* 0x06: Configuration word 2 */
    uint8_t     imr_shadow;     /* 0x08: Shadow copy of IMR (write-only reg) */
    uint8_t     reserved_09[3]; /* 0x09: Padding */
} sio2681_chip_t;

/*
 * SIO2681 Channel Structure
 *
 * One per DUART channel (two per chip). Links to the chip structure,
 * the SIO descriptor, and the peer channel.
 *
 * Size: 0x1C bytes (28 bytes)
 */
typedef struct sio2681_channel {
    volatile uint8_t *regs;     /* 0x00: Base address of channel registers */
    struct sio2681_chip *chip;  /* 0x04: Pointer to parent chip structure */
    struct sio2681_channel *peer; /* 0x08: Pointer to other channel */
    sio_desc_t  *sio_desc;      /* 0x0C: SIO descriptor for callbacks */
    uint16_t    flags;          /* 0x10: Channel flags */
    uint16_t    int_bit;        /* 0x12: Interrupt bit position (0 or 4) */
    uint32_t    reserved_14;    /* 0x14: Reserved */
    uint16_t    tx_int_mask;    /* 0x18: Transmit interrupt mask bit */
    uint16_t    baud_support;   /* 0x1A: Supported baud rates mask */
} sio2681_channel_t;

/* Channel flags */
#define SIO2681_FLAG_CHANNEL_B      0x02    /* This is channel B (vs A) */

/*
 * ============================================================================
 * Status Codes
 * ============================================================================
 */
#define status_$sio2681_invalid_baud    0x00360008  /* Invalid baud rate */

/*
 * ============================================================================
 * Public Function Declarations
 * ============================================================================
 */

/*
 * SIO2681_$INIT - Initialize a SIO2681 DUART chip
 *
 * Initializes both channels of a 2681 DUART chip and links them to
 * SIO descriptors. Sets up interrupt handling and default line parameters.
 *
 * Parameters:
 *   int_vec_ptr    - Pointer to interrupt vector number
 *   chip_num_ptr   - Pointer to chip number (0-based)
 *   chan_a_struct  - Pointer to channel A structure storage
 *   chan_a_callback - Pointer to channel A callback/SIO descriptor pointer
 *   chan_a_params  - Channel A initial parameters
 *   chan_b_struct  - Pointer to channel B structure storage
 *   chan_b_callback - Pointer to channel B callback/SIO descriptor pointer
 *   chan_b_params  - Channel B initial parameters
 *   chip_struct    - Pointer to chip structure storage
 *   config         - Configuration data (baud rate limits, etc.)
 *
 * Original address: 0x00e333dc
 */
void SIO2681_$INIT(int16_t *int_vec_ptr, int16_t *chip_num_ptr,
                   sio2681_channel_t *chan_a_struct, sio_desc_t **chan_a_callback,
                   sio_params_t *chan_a_params,
                   sio2681_channel_t *chan_b_struct, sio_desc_t **chan_b_callback,
                   sio_params_t *chan_b_params,
                   sio2681_chip_t *chip_struct, uint16_t *config);

/*
 * SIO2681_$INT - Interrupt handler for SIO2681
 *
 * Called when the 2681 generates an interrupt. Dispatches to appropriate
 * handlers for receive, transmit, and modem status changes.
 *
 * Parameters:
 *   chip - Pointer to chip structure
 *
 * Original address: 0x00e1ceec
 */
void SIO2681_$INT(sio2681_chip_t *chip);

/*
 * SIO2681_$SET_LINE - Set line parameters
 *
 * Configures serial line parameters (baud rate, data format, flow control).
 *
 * Parameters:
 *   channel     - Channel structure
 *   params      - New parameters
 *   change_mask - Mask of parameters to change
 *   status_ret  - Status return
 *
 * Original address: 0x00e1d250
 */
void SIO2681_$SET_LINE(sio2681_channel_t *channel, sio_params_t *params,
                       uint32_t change_mask, status_$t *status_ret);

/*
 * SIO2681_$INQ_LINE - Inquire line status
 *
 * Returns current modem signal states (CTS, DCD).
 *
 * Parameters:
 *   channel     - Channel structure
 *   params_ret  - Parameter block to update with signal states
 *   mask        - Mask of signals to query
 *   status_ret  - Status return
 *
 * Original address: 0x00e725b0
 */
void SIO2681_$INQ_LINE(sio2681_channel_t *channel, sio_params_t *params_ret,
                       uint32_t mask, status_$t *status_ret);

/*
 * SIO2681_$XMIT - Transmit a character
 *
 * Writes a character to the transmit holding register and enables
 * transmit interrupts if not already enabled.
 *
 * Parameters:
 *   channel - Channel structure
 *   ch      - Character to transmit
 *
 * Original address: 0x00e1d4fc
 */
void SIO2681_$XMIT(sio2681_channel_t *channel, uint8_t ch);

/*
 * SIO2681_$SET_BREAK - Set or clear break condition
 *
 * Starts or stops a break condition on the serial line.
 *
 * Parameters:
 *   channel - Channel structure
 *   enable  - Enable break (negative) or disable (non-negative)
 *
 * Original address: 0x00e1d114
 */
void SIO2681_$SET_BREAK(sio2681_channel_t *channel, int8_t enable);

/*
 * SIO2681_$TONE - Control tone output
 *
 * Controls the tone output on the output port (used for speaker/bell).
 *
 * Parameters:
 *   channel    - Channel structure
 *   enable_ptr - Pointer to enable flag (bit 7 = enable)
 *   param3     - Reserved
 *   param4     - Reserved
 *
 * Original address: 0x00e1d172
 */
void SIO2681_$TONE(sio2681_channel_t *channel, uint8_t *enable_ptr,
                   uint32_t param3, uint32_t param4);

#endif /* SIO2681_H */
