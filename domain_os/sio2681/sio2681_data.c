/*
 * SIO2681 Global Data
 *
 * Contains the global data tables used by the SIO2681 driver.
 * These tables are located at 0xe2deb8 in the original binary.
 *
 * Original address: 0xe2deb8
 */

#include "sio2681/sio2681_internal.h"

/*
 * Global data instance
 *
 * The error_table maps hardware error bits to status codes.
 * Index is derived from SR bits 4-7 (shifted down by 4).
 *
 * Baud rate tables map rate indices to CSR codes and support bits.
 */
sio2681_global_data_t SIO2681_$DATA = {
    /* spin_lock */
    .spin_lock = 0,

    /* error_table: Maps SR[7:4] to error status codes */
    .error_table = {
        0x00000004,  /* Index 0: Not used typically */
        0x00000001,  /* Index 1: status code 1 */
        0x00000005,  /* Index 2: status code 5 */
        0x00000002,  /* Index 3: status code 2 */
        0x00000006,  /* Index 4: status code 6 */
        0x00000003,  /* Index 5: status code 3 */
        0x00000007,  /* Index 6: status code 7 */
        0x00000020,  /* Index 7: status code 0x20 (special) */
    },

    /* Command register values */
    .cmd_break_stop  = 0x70,   /* Stop break */
    .pad_49          = 0x00,
    .cmd_break_start = 0x60,   /* Start break */
    .pad_4b          = 0x00,

    /* Default baud rate: 9600 tx/rx */
    .default_baud = 0x000E000E,  /* Index 14 = 9600 baud */

    /* Baud rate support masks for extended rate set selection */
    .baud_mask_a = 0x0001,  /* Channel A mask */
    .baud_mask_b = 0x0002,  /* Channel B mask */

    /* More command register templates */
    .cmd_reset_error  = 0x40,  /* Reset error status */
    .pad_55           = 0x00,
    .cmd_enable_rx_tx = 0x05,  /* Enable RX + TX */
    .pad_57           = 0x00,
    .cmd_reset_rx     = 0x20,  /* Reset receiver */
    .pad_59           = 0x00,
    .cmd_reset_tx     = 0x30,  /* Reset transmitter */
    .pad_5b           = 0x00,
    .cmd_reset_mr     = 0x10,  /* Reset MR pointer */
    .pad_5d           = 0x00,

    /* Mode register templates */
    .mr2_template = 0x0700,  /* MR2: 1 stop bit, normal mode */
    .mr1_template = 0x1300,  /* MR1: 8 bits, no parity, RxRDY interrupt */

    /*
     * Baud rate support bit table
     * Each entry indicates which baud rate set supports this rate.
     * Index corresponds to baud rate index 0-16.
     *
     * Bit 0 = standard set, Bit 1 = extended set
     */
    .baud_bits = {
        0x0001,  /* 0: 50 baud */
        0x0002,  /* 1: 75 baud */
        0x0003,  /* 2: 110 baud */
        0x0003,  /* 3: 134.5 baud */
        0x0002,  /* 4: 150 baud */
        0x0003,  /* 5: 200 baud */
        0x0003,  /* 6: 300 baud */
        0x0003,  /* 7: 600 baud */
        0x0002,  /* 8: 1050 baud */
        0x0000,  /* 9: 1200 baud */
        0x0003,  /* 10: 1800 baud */
        0x0001,  /* 11: 2000 baud */
        0x0002,  /* 12: 2400 baud */
        0x0001,  /* 13: 4800 baud */
        0x0000,  /* 14: 9600 baud */
        0x0001,  /* 15: 19200 baud */
        0x0002,  /* 16: 38400 baud */
    },

    /*
     * Baud rate CSR code table
     * Each entry is the CSR nibble value for that baud rate index.
     * Upper nibble = RX rate, lower nibble = TX rate.
     */
    .baud_codes = {
        0x00,  /* 0: 50 baud */
        0x00,  /* 1: 75 baud */
        0x01,  /* 2: 110 baud */
        0x02,  /* 3: 134.5 baud */
        0x03,  /* 4: 150 baud */
        0x04,  /* 5: 200 baud (extended) */
        0x05,  /* 6: 300 baud */
        0x06,  /* 7: 600 baud */
        0x07,  /* 8: 1050 baud */
        0x00,  /* 9: 1200 baud */
        0x08,  /* 10: 1800 baud */
        0x00,  /* 11: 2000 baud */
        0x09,  /* 12: 2400 baud */
        0x0A,  /* 13: 4800 baud */
        0x0B,  /* 14: 9600 baud */
        0x0C,  /* 15: 19200 baud */
        0x0D,  /* 16: 38400 baud */
    },
    .pad_95 = 0x00,
};

/*
 * Channel pointer table
 * Indexed by (chip_num << 1) | channel_num
 * Original address: 0xe2df70
 */
sio2681_channel_t *SIO2681_$CHANNELS[SIO2681_MAX_CHIPS * 2] = {
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL
};

/*
 * Chip pointer table
 * Indexed by chip_num
 * Original address: 0xe2df78
 */
sio2681_chip_t *SIO2681_$CHIPS[SIO2681_MAX_CHIPS] = {
    NULL, NULL, NULL, NULL
};

/*
 * Interrupt vector table
 * Contains function pointers for each chip's interrupt handler.
 * Original address: 0xe351e8
 *
 * Note: These would be filled in during system initialization.
 */
void (*SIO2681_$INT_VECTORS[4])(void) = {
    NULL, NULL, NULL, NULL
};
