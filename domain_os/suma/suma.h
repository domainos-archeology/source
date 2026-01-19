/*
 * suma/suma.h - Summagraphics Tablet Pad Public API
 *
 * The SUMA subsystem handles input from Summagraphics-compatible
 * tablet/digitizer devices. It processes 5-byte tablet data packets
 * containing stylus position and button state.
 *
 * Tablet data packet format (5 bytes):
 *   Byte 0: Sync byte with button state (bit 6 = sync flag, bits 5-2 = ID)
 *   Bytes 1-4: X/Y coordinate data (6 bits each, packed)
 *
 * Original addresses: 0x00e1ad18 (SUMA_$RCV), 0x00e33224 (SUMA_$INIT)
 */

#ifndef SUMA_H
#define SUMA_H

#include "base/base.h"
#include "time/time.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Tablet pad buffer size (number of entries in circular buffer) */
#define SUMA_TPAD_BUFFER_SIZE   6

/* Initial position threshold for filtering small movements */
#define SUMA_INITIAL_THRESHOLD  0x200

/* Maximum position threshold */
#define SUMA_MAX_THRESHOLD      0x0a00

/* Threshold increment on duplicate events */
#define SUMA_THRESHOLD_INCREMENT 0x10

/*
 * ============================================================================
 * Data Structures
 * ============================================================================
 */

/*
 * Tablet position sample
 *
 * Stores a single tablet position reading with timestamp.
 * Size: 16 bytes
 */
typedef struct suma_sample_t {
    uint32_t    delta_time;     /* 0x00: Time delta from previous sample */
    uint32_t    timestamp_high; /* 0x04: Timestamp high word */
    uint16_t    timestamp_low;  /* 0x08: Timestamp low word */
    uint8_t     id_flags;       /* 0x0A: ID and flags (bits 5-2 = ID << 2) */
    uint8_t     reserved_0b;    /* 0x0B: Reserved */
    uint8_t     x_high;         /* 0x0C: X coordinate high byte */
    uint8_t     x_low;          /* 0x0D: X coordinate low byte */
    uint8_t     y_high;         /* 0x0E: Y coordinate high byte */
    uint8_t     y_low;          /* 0x0F: Y coordinate low byte */
} suma_sample_t;

/*
 * SUMA state structure
 *
 * Maintains the current state of tablet input processing.
 * Located at 0x00e2dd88 in original binary.
 * Size: approximately 46 bytes (0x2e)
 */
typedef struct suma_state_t {
    uint32_t    last_time;          /* 0x00: Last sample timestamp (low 32 bits) */
    uint32_t    prev_delta;         /* 0x04: Previous delta time */
    uint32_t    prev_timestamp_high;/* 0x08: Previous timestamp high */
    uint16_t    prev_timestamp_low; /* 0x0C: Previous timestamp low */
    uint8_t     prev_id_flags;      /* 0x0E: Previous ID and flags */
    uint8_t     prev_reserved;      /* 0x0F: Previous reserved byte */
    uint8_t     prev_x_high;        /* 0x10: Previous X high */
    uint8_t     prev_x_low;         /* 0x11: Previous X low */
    uint8_t     prev_y_high;        /* 0x12: Previous Y high */
    uint8_t     prev_y_low;         /* 0x13: Previous Y low */
    uint32_t    cur_delta;          /* 0x14: Current delta time */
    uint32_t    cur_timestamp_high; /* 0x18: Current timestamp high */
    uint16_t    cur_timestamp_low;  /* 0x1C: Current timestamp low */
    uint8_t     cur_id_flags;       /* 0x1E: Current ID and flags */
    uint8_t     cur_reserved;       /* 0x1F: Current reserved byte */
    uint8_t     cur_x_high;         /* 0x20: Current X high */
    uint8_t     cur_x_low;          /* 0x21: Current X low */
    uint8_t     cur_y_high;         /* 0x22: Current Y high */
    uint8_t     cur_y_low;          /* 0x23: Current Y low */
    void        *tpad_buffer;       /* 0x24: Pointer to TPAD buffer */
    uint16_t    rcv_state;          /* 0x28: Receive state machine (0-4) */
    uint16_t    threshold;          /* 0x2A: Position threshold for filtering */
} suma_state_t;

/*
 * TPAD buffer structure
 *
 * Circular buffer for tablet pad samples.
 * Located at 0x00e2de3c in original binary.
 */
typedef struct tpad_buffer_t {
    uint16_t    head;               /* 0x00: Head index (write position) */
    uint16_t    tail;               /* 0x02: Tail index (read position) */
    suma_sample_t samples[SUMA_TPAD_BUFFER_SIZE]; /* 0x04: Sample buffer */
} tpad_buffer_t;

/*
 * ============================================================================
 * Global Data
 * ============================================================================
 */

/*
 * SUMA_$STATE - Tablet state structure
 *
 * Original address: 0x00e2dd88
 */
extern suma_state_t SUMA_$STATE;

/*
 * TERM_$TPAD_BUFFER - Tablet pad sample buffer
 *
 * Circular buffer storing tablet position samples.
 *
 * Original address: 0x00e2de3c
 */
extern tpad_buffer_t TERM_$TPAD_BUFFER;

/*
 * ============================================================================
 * Function Prototypes
 * ============================================================================
 */

/*
 * SUMA_$INIT - Initialize the SUMA (tablet pad) subsystem
 *
 * Initializes the tablet state structure:
 *   - Sets tpad_buffer pointer to TERM_$TPAD_BUFFER
 *   - Clears receive state to 0
 *   - Sets initial threshold to 0x200
 *
 * Original address: 0x00e33224
 */
void SUMA_$INIT(void);

/*
 * SUMA_$RCV - Receive and process a tablet data byte
 *
 * Called when a byte is received from the tablet device. Implements
 * a 5-state state machine to assemble 5-byte tablet packets:
 *
 *   State 0: Wait for sync byte (bit 6 set), extract ID
 *   States 1-4: Collect coordinate bytes (6 bits each)
 *   After state 4: Compare with previous position, filter duplicates
 *
 * Movement filtering:
 *   - If ID matches and X/Y within threshold, treat as duplicate
 *   - Duplicates increase threshold (up to 0xa00)
 *   - New positions reset threshold to 0x200
 *
 * Parameters:
 *   param_1 - Unused (may be device handle)
 *   param_2 - Received data byte
 *
 * Original address: 0x00e1ad18
 */
void SUMA_$RCV(uint32_t param_1, uint8_t param_2);

#endif /* SUMA_H */
