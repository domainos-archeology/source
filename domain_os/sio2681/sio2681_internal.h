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

/*
 * Maximum number of SIO2681 chips.
 *
 * SIO2681_$PTRS at 0x00E2DF80 is a 1-based array of 16-byte records and
 * SIO2681_$INT1_RTE begins at 0x00E2DFA0, so only two entries fit; the
 * matching interrupt-stub table at 0x00E351EC also holds exactly two
 * longwords before its trailing zeroes.
 */
#define SIO2681_MAX_CHIPS       2

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
    uint32_t    reserved_04;            /* 0x04: Unused (error_table is at 0x08) */

    /* Error flag to status code mapping table */
    /* Indexed by SR[7:4] (0..15): SIO2681_$INT does
     * `move.l (0x8,A5,D1w*0x1),-(SP)` with D1 = 4*index (00e1cf52), and the
     * 16 longwords run 0x08..0x47 -- cmd_break_stop follows at 0x48. */
    uint32_t    error_table[16];        /* 0x08: Error flags -> status codes */

    /* Command register values */
    uint8_t     cmd_break_stop;         /* 0x48: Stop break command (0x70) */
    uint8_t     pad_49;
    uint8_t     cmd_break_start;        /* 0x4A: Start break command (0x60) */
    uint8_t     pad_4b;
    uint32_t    default_baud;           /* 0x4C: Default baud rate setting */

    /* Baud rate support masks per channel type */
    uint16_t    baud_mask_a;            /* 0x50: Channel A baud support mask */
    uint16_t    baud_mask_b;            /* 0x52: Channel B baud support mask */

    /* Command register templates.  Every one is read with `move.b`, so the
     * low byte of each word is padding.  Values are from the image at
     * 0x00E2DEB8 (2681 CR encoding: bits 6..4 = miscellaneous command,
     * bit 3 = TX disable, bit 2 = TX enable, bit 1 = RX disable,
     * bit 0 = RX enable). */
    uint8_t     cmd_reset_mr_ptr;       /* 0x54: 0x10 - misc 1, reset MR pointer.
                                         * SIO2681_$SET_LINE 0x00E1D43E, issued
                                         * immediately before the MR1/MR2 pair. */
    uint8_t     pad_55;
    uint8_t     cmd_reset_err_enable;   /* 0x56: 0x45 - misc 4 (reset error
                                         * status) + RX enable + TX enable.
                                         * SIO2681_$SET_LINE 0x00E1D4D6, the
                                         * last register write of the call. */
    uint8_t     pad_57;
    uint8_t     cmd_reset_rx;           /* 0x58: 0x2A - misc 2 (reset receiver)
                                         * + RX disable + TX disable.
                                         * SIO2681_$SET_LINE 0x00E1D286. */
    uint8_t     pad_59;
    uint8_t     cmd_reset_tx;           /* 0x5A: 0x3A - misc 3 (reset
                                         * transmitter) + RX/TX disable.
                                         * SIO2681_$SET_LINE 0x00E1D28C. */
    uint8_t     pad_5b;
    uint8_t     cmd_disable_rx_tx;      /* 0x5C: 0x0A - no misc command,
                                         * RX disable + TX disable.
                                         * SIO2681_$SET_LINE 0x00E1D280, the
                                         * first register write of the call. */
    uint8_t     pad_5d;

    /* Mode register templates.  Both are loaded with `move.w` into a word
     * frame local whose HIGH byte is then edited and stored to MR
     * (SIO2681_$SET_LINE 0x00E1D35E / 0x00E1D3E4, stored at 0x00E1D444 /
     * 0x00E1D44A), so the low byte of each word is padding.  MR1 is written
     * first, hence MR1 is the cell at 0x60. */
    uint16_t    mr2_template;           /* 0x5E: 0x0700 - MR2 template */
    uint16_t    mr1_template;           /* 0x60: 0x0B00 - MR1 template */

    /* Baud rate index table - maps baud rate index to support bit */
    uint16_t    baud_bits[17];          /* 0x62: Baud rate support bits */

    /* Baud rate code table - maps index to CSR value.  The stride is a WORD:
     * sio2681_set_baud_rate reads `move.b (0x85,A2),D1b` with A2 = A5 + 2*index
     * (00e1d204 / 00e1d20e), i.e. the CSR nibble is the low byte of the word at
     * 0x84 + 2*index.  The table ends at 0xA5. */
    uint16_t    baud_codes[17];         /* 0x84: Baud rate CSR codes */

    uint16_t    pad_a6;                 /* 0xA6 */

    /*
     * 0xA8 (0x00E2DF60): the eight longwords SIO6509_$RCV indexes with
     * `lea (0x0,A5,D0w*0x1),A1 / move.l (0xa8,A1),-(SP)` at 0x00E1D566 and
     * 0x00E1D56A, where D0 = ((status >> 4) & 7) * 4.  The values are
     * 0,2,4,6,1,5,3,0 - a receive status-bit permutation, not a status code.
     */
    uint32_t    sio6509_rcv_flags[8];   /* 0xA8 */

} sio2681_global_data_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(sio2681_global_data_t, spin_lock) == 0x00, "sio2681_global_data_t.spin_lock");
_Static_assert(__builtin_offsetof(sio2681_global_data_t, reserved_04) == 0x04, "sio2681_global_data_t.reserved_04");
_Static_assert(__builtin_offsetof(sio2681_global_data_t, error_table) == 0x08, "sio2681_global_data_t.error_table");
_Static_assert(__builtin_offsetof(sio2681_global_data_t, cmd_break_stop) == 0x48, "sio2681_global_data_t.cmd_break_stop");
_Static_assert(__builtin_offsetof(sio2681_global_data_t, cmd_break_start) == 0x4A, "sio2681_global_data_t.cmd_break_start");
_Static_assert(__builtin_offsetof(sio2681_global_data_t, default_baud) == 0x4C, "sio2681_global_data_t.default_baud");
_Static_assert(__builtin_offsetof(sio2681_global_data_t, baud_mask_a) == 0x50, "sio2681_global_data_t.baud_mask_a");
_Static_assert(__builtin_offsetof(sio2681_global_data_t, baud_mask_b) == 0x52, "sio2681_global_data_t.baud_mask_b");
_Static_assert(__builtin_offsetof(sio2681_global_data_t, cmd_reset_mr_ptr) == 0x54, "sio2681_global_data_t.cmd_reset_mr_ptr");
_Static_assert(__builtin_offsetof(sio2681_global_data_t, cmd_reset_err_enable) == 0x56, "sio2681_global_data_t.cmd_reset_err_enable");
_Static_assert(__builtin_offsetof(sio2681_global_data_t, cmd_reset_rx) == 0x58, "sio2681_global_data_t.cmd_reset_rx");
_Static_assert(__builtin_offsetof(sio2681_global_data_t, cmd_reset_tx) == 0x5A, "sio2681_global_data_t.cmd_reset_tx");
_Static_assert(__builtin_offsetof(sio2681_global_data_t, cmd_disable_rx_tx) == 0x5C, "sio2681_global_data_t.cmd_disable_rx_tx");
_Static_assert(__builtin_offsetof(sio2681_global_data_t, mr2_template) == 0x5E, "sio2681_global_data_t.mr2_template");
_Static_assert(__builtin_offsetof(sio2681_global_data_t, mr1_template) == 0x60, "sio2681_global_data_t.mr1_template");
_Static_assert(__builtin_offsetof(sio2681_global_data_t, baud_bits) == 0x62, "sio2681_global_data_t.baud_bits");
_Static_assert(__builtin_offsetof(sio2681_global_data_t, baud_codes) == 0x84, "sio2681_global_data_t.baud_codes");
_Static_assert(__builtin_offsetof(sio2681_global_data_t, sio6509_rcv_flags) == 0xA8, "sio2681_global_data_t.sio6509_rcv_flags");
/* The record's fields end at 0xC7, which is where SIO2681_$PTRS begins
 * (0x00E2DEB8 + 0xC8 == 0x00E2DF80). */

/*
 * Global data instance
 * Original address: 0xe2deb8
 */
extern sio2681_global_data_t SIO2681_$DATA;

/*
 * SIO2681_$PTRS - the per-chip pointer table, 0x00E2DF80
 *
 * SIO2681_$INIT writes all three pointers of one entry with a single
 * chip_num*0x10 index off 0x00E2DF80:
 *
 *   00e33426  lsl.w #0x4,D0w                        ; D0 = chip_num * 0x10
 *   00e33428  move.l A3,(-0x8,A4,D0w*0x1)           ; +0x08 = the chip record
 *   00e3342c  move.l (0x10,A6),(-0x10,A4,D0w*0x1)   ; +0x00 = channel A
 *   00e3346e  move.l (0x1c,A6),(-0xc,A3,D0w*0x1)    ; +0x04 = channel B
 *
 * with A3 = A4 = 0x00E2DF80, i.e. entry n is at 0x00E2DF80 + 0x10*(n-1): the
 * table is 1-BASED, and TERM_$INIT passes the constant word 1 as chip_num
 * (0x00E33220, from the cell 0x00E331C4 "pea (0x5a,PC)" points at).
 *
 * The two interrupt stubs fill in the fourth field: after their movem,
 * "move.l (0x12,SP),(0xc,A0)" at 0x00E2DFBC stores the interrupted PC there.
 */
typedef struct sio2681_ptrs_entry {
    sio2681_channel_t  *chan_a;         /* 0x00 */
    sio2681_channel_t  *chan_b;         /* 0x04 */
    sio2681_chip_t     *chip;           /* 0x08 */
    m68k_ptr_t          saved_pc;       /* 0x0C: written by the rte stubs */
} sio2681_ptrs_entry_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(sio2681_ptrs_entry_t, chan_a) == 0x00, "sio2681_ptrs_entry_t.chan_a");
_Static_assert(__builtin_offsetof(sio2681_ptrs_entry_t, chan_b) == 0x04, "sio2681_ptrs_entry_t.chan_b");
_Static_assert(__builtin_offsetof(sio2681_ptrs_entry_t, chip) == 0x08, "sio2681_ptrs_entry_t.chip");
_Static_assert(__builtin_offsetof(sio2681_ptrs_entry_t, saved_pc) == 0x0C, "sio2681_ptrs_entry_t.saved_pc");
_Static_assert(sizeof(sio2681_ptrs_entry_t) == 0x10, "sio2681_ptrs_entry_t must be 16 bytes");
#endif

/*
 * SIO2681_PTRS_SECTION - keep SIO2681_$PTRS inside the SIO_INT code segment.
 *
 * The SAU2 map's SIO_INT segment (0xE2DF80, size 0x8C) opens with
 * SIO2681_$PTRS at 0xE2DF80 and is followed immediately by the two stubs at
 * 0xE2DFA0/0xE2DFB0, which reach the table with a 16-bit PC-relative operand:
 * `41 fa ff d6  lea (-0x2a,PC),A0' at 0xE2DFA8 (0xE2DFAA - 0x2A = 0xE2DF80)
 * and the same at 0xE2DFB8 for entry 2.  Emitted as ordinary `.bss' the table
 * lands tens of kilobytes from sio2681/sau2/int_rte.o and both R_68K_PC16
 * relocations overflow (source-uwxz), so give it a section of its own, which
 * the generated build/sau2/layout.ld (tools/gen_layout_ld.py) links at its
 * map position, directly ahead of the int_rte.s stubs - the same technique
 * the SVC dispatch tables use (source-a5t8).  This is a code-segment cell, not an A5
 * module block, so it is outside the `.moddata.<name>` scheme of
 * docs/design-per-process-data.md (source-0i3).
 */
#if defined(ARCH_M68K)
#define SIO2681_PTRS_SECTION  __attribute__((section(".text.sio2681_ptrs")))
#else
#define SIO2681_PTRS_SECTION
#endif

extern sio2681_ptrs_entry_t SIO2681_$PTRS[SIO2681_MAX_CHIPS];

/*
 * SIO2681_$INT_VECTORS - the interrupt stubs, 0x00E351EC
 *
 * 0x00E334F0 "move.l (-0x4,A5,D0w*0x1),(-0x4,A1,D1w*0x1)" with A5 =
 * 0x00E351EC and D0 = chip_num*4 reads this table, so it is 1-based on
 * chip_num just like SIO2681_$PTRS.  The image holds
 *
 *   00e351ec  00 e2 df a0  00 e2 df b0
 *
 * i.e. SIO2681_$INT1_RTE and SIO2681_$INT2_RTE, the hand-written stubs
 * transcribed in sio2681/sau2/int_rte.s.
 */
void SIO2681_$INT1_RTE(void);
void SIO2681_$INT2_RTE(void);

extern void (*const SIO2681_$INT_VECTORS[SIO2681_MAX_CHIPS])(void);

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
