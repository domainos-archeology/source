/*
 * SIO2681 - Internal Definitions
 *
 * Internal data structures and helper functions for the SIO2681 driver.
 */

#ifndef SIO2681_INTERNAL_H
#define SIO2681_INTERNAL_H

#include "sio2681/sio2681.h"
#include "ml/ml.h"
#include "pchist/pchist.h"

/*
 * ============================================================================
 * Hardware Configuration
 * ============================================================================
 */

/* Base address for SIO2681 chips */
#define SIO2681_BASE_ADDR       0xFFB000

/* Maximum number of SIO2681 chips */
#define SIO2681_MAX_CHIPS       4

/*
 * ============================================================================
 * Global Data Tables
 * ============================================================================
 *
 * These tables are located at 0xe2deb8 in the original binary.
 */

/*
 * SIO2681_$DATA - Global data block
 *
 * Contains spin lock, error flag tables, command values, and baud rate tables.
 * All offsets are relative to the base at 0xe2deb8.
 *
 * Original address: 0xe2deb8
 */
typedef struct sio2681_global_data {
    /* Spin lock for SIO2681 operations */
    uint32_t    spin_lock;              /* 0x00: Spin lock variable */

    /* Error flag to status code mapping table */
    uint32_t    error_table[8];         /* 0x08: Error flags -> status codes */
    /* Index 0: no error
     * Index 1-7: various error conditions
     */

    /* Command register values */
    uint8_t     cmd_break_stop;         /* 0x48: Stop break command (0x70) */
    uint8_t     pad_49;
    uint8_t     cmd_break_start;        /* 0x4A: Start break command (0x60) */
    uint8_t     pad_4b;
    uint32_t    default_baud;           /* 0x4C: Default baud rate setting */

    /* Baud rate support masks per channel type */
    uint16_t    baud_mask_a;            /* 0x50: Channel A baud support mask */
    uint16_t    baud_mask_b;            /* 0x52: Channel B baud support mask */

    /* Command register templates */
    uint8_t     cmd_reset_error;        /* 0x54: Reset error status (0x40) */
    uint8_t     pad_55;
    uint8_t     cmd_enable_rx_tx;       /* 0x56: Enable RX+TX (0x05) */
    uint8_t     pad_57;
    uint8_t     cmd_reset_rx;           /* 0x58: Reset receiver (0x20) */
    uint8_t     pad_59;
    uint8_t     cmd_reset_tx;           /* 0x5A: Reset transmitter (0x30) */
    uint8_t     pad_5b;
    uint8_t     cmd_reset_mr;           /* 0x5C: Reset MR pointer (0x10) */
    uint8_t     pad_5d;

    /* Mode register templates */
    uint16_t    mr2_template;           /* 0x5E: Mode register 2 template */
    uint16_t    mr1_template;           /* 0x60: Mode register 1 template */

    /* Baud rate index table - maps baud rate index to support bit */
    uint16_t    baud_bits[17];          /* 0x62: Baud rate support bits */

    /* Baud rate code table - maps index to CSR value */
    uint8_t     baud_codes[17];         /* 0x84: Baud rate CSR codes */
    uint8_t     pad_95;

} sio2681_global_data_t;

/*
 * Global data instance
 * Original address: 0xe2deb8
 */
extern sio2681_global_data_t SIO2681_$DATA;

/*
 * Channel pointer table - indexed by chip_id << 1 | channel
 * Contains pointers to channel structures
 * Original address: 0xe2df70
 */
extern sio2681_channel_t *SIO2681_$CHANNELS[SIO2681_MAX_CHIPS * 2];

/*
 * Chip pointer table - indexed by chip_id
 * Contains pointers to chip structures
 * Original address: 0xe2df78
 */
extern sio2681_chip_t *SIO2681_$CHIPS[SIO2681_MAX_CHIPS];

/*
 * Interrupt vector table - interrupt service routines
 * Original address: 0xe351e8
 */
extern void (*SIO2681_$INT_VECTORS[4])(void);

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * sio2681_set_baud_rate - Set baud rate for a channel
 *
 * Internal helper to set the baud rate by writing to the CSR register.
 * Validates baud rate against supported rates for the channel.
 *
 * Parameters:
 *   channel  - Channel structure
 *   tx_rate  - Transmit baud rate index
 *   rx_rate  - Receive baud rate index
 *   extended - Use extended baud rates (ACR bit 7)
 *
 * Original address: 0x00e1d1da
 */
void sio2681_set_baud_rate(sio2681_channel_t *channel,
                           int16_t tx_rate, int16_t rx_rate, int8_t extended);

#endif /* SIO2681_INTERNAL_H */
