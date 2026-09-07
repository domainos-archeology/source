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

    /* error_table: 16 longwords at 0x08, indexed by SR[7:4].
     * SIO2681_$INT reads it with `move.l (0x8,A5,D1w*0x1)` where D1 = 4*index
     * (0x00E1CF52, 0x00E1CF92).  Values transcribed from the image at
     * 0x00E2DEC0. */
    .error_table = {
        0x00000000,  /*  0 */
        0x00000004,  /*  1 */
        0x00000001,  /*  2 */
        0x00000005,  /*  3 */
        0x00000002,  /*  4 */
        0x00000006,  /*  5 */
        0x00000003,  /*  6 */
        0x00000007,  /*  7 */
        0x00000020,  /*  8 */
        0x00000024,  /*  9 */
        0x00000021,  /* 10 */
        0x00000025,  /* 11 */
        0x00000022,  /* 12 */
        0x00000026,  /* 13 */
        0x00000023,  /* 14 */
        0x00000027,  /* 15 */
    },

    /* Command register values */
    .cmd_break_stop  = 0x70,   /* 0x48: misc 7 (stop break) */
    .pad_49          = 0x00,
    .cmd_break_start = 0x60,   /* 0x4A: misc 6 (start break) */
    .pad_4b          = 0x00,

    /* Default baud rate: index 14 tx and rx */
    .default_baud = 0x000E000E,

    /* Baud rate support masks selected by the chip's channel-B flag */
    .baud_mask_a = 0x0001,  /* 0x50 */
    .baud_mask_b = 0x0002,  /* 0x52 */

    /* More command register templates (image values at 0x00E2DF0C..0x00E2DF14) */
    .cmd_reset_mr_ptr     = 0x10,  /* 0x54 */
    .pad_55               = 0x00,
    .cmd_reset_err_enable = 0x45,  /* 0x56 */
    .pad_57               = 0x00,
    .cmd_reset_rx         = 0x2A,  /* 0x58 */
    .pad_59               = 0x00,
    .cmd_reset_tx         = 0x3A,  /* 0x5A */
    .pad_5b               = 0x00,
    .cmd_disable_rx_tx    = 0x0A,  /* 0x5C */
    .pad_5d               = 0x00,

    /* Mode register templates (image words at 0x00E2DF16 / 0x00E2DF18) */
    .mr2_template = 0x0700,
    .mr1_template = 0x0B00,

    /*
     * Baud rate support bit table, 17 words at 0x62.
     * SIO2681_$SET_LINE tests `(0x62,A3)` with A3 = A5 + 2*index (0x00E1D2C8)
     * and ANDs it against the chip's supported-rate mask.
     * Transcribed from the image at 0x00E2DF1A.
     */
    .baud_bits = {
        0x0001,  /*  0 */
        0x0002,  /*  1 */
        0x0003,  /*  2 */
        0x0003,  /*  3 */
        0x0002,  /*  4 */
        0x0003,  /*  5 */
        0x0003,  /*  6 */
        0x0003,  /*  7 */
        0x0002,  /*  8 */
        0x0000,  /*  9 */
        0x0003,  /* 10 */
        0x0000,  /* 11 */
        0x0003,  /* 12 */
        0x0001,  /* 13 */
        0x0003,  /* 14 */
        0x0002,  /* 15 */
        0x0001,  /* 16 */
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
