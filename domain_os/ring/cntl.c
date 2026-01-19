/*
 * RINGLOG_$CNTL - Ring logging control
 *
 * Controls the ring logging subsystem.
 *
 * Original address: 0x00E72226
 */

#include "ring/ringlog_internal.h"

/*
 * Wire area descriptors passed to MST_$WIRE_AREA
 * These define the start address and parameters for wiring the ring buffer.
 */
static uint32_t ringlog_wire_start = RINGLOG_BUF_BASE;
static uint32_t ringlog_wire_end = RINGLOG_WIRE_END;

/*
 * RINGLOG_$CNTL - Ring logging control
 *
 * This function controls the ring logging subsystem through various commands.
 * Commands can start/stop logging, clear the buffer, set filters, and
 * retrieve logged data.
 *
 * Commands 0, 1, 2 copy the buffer to the output parameter.
 * Commands 0, 3, 5 initialize and start logging.
 * Commands 1, 4 stop logging.
 * Commands 6, 7, 8 set socket type filters.
 *
 * Parameters:
 *   cmd_ptr     - Pointer to command code
 *   param       - Command-specific parameter/output buffer
 *   status_ret  - Pointer to receive status code
 */
void RINGLOG_$CNTL(uint16_t *cmd_ptr, void *param, status_$t *status_ret)
{
    uint16_t cmd = *cmd_ptr;
    uint32_t *param_words = (uint32_t *)param;
    int16_t i;

    /* Clear status */
    *status_ret = status_$ok;

    switch (cmd) {
    case RINGLOG_CMD_START:         /* 0 */
    case RINGLOG_CMD_CLEAR:         /* 3 */
    case RINGLOG_CMD_START_FILTERED: /* 5 */
        /*
         * Start logging with optional clear.
         * - Stop any existing logging first
         * - Clear the buffer index
         * - Clear valid flags in all entries
         * - Wire the buffer memory
         * - Set ID filter if command 5
         * - Enable logging
         */
        RINGLOG_$STOP_LOGGING();

        /* Clear buffer index */
        RINGLOG_$BUF.current_index = 0;

        /* Clear valid flag in all entries */
        for (i = 0; i < RINGLOG_MAX_ENTRIES; i++) {
            uint8_t *entry_bytes = (uint8_t *)&RINGLOG_$BUF.entries[i];
            /* Clear valid bit (bit 2) at offset 0x0b (flags byte) */
            entry_bytes[0x0a] &= ~RINGLOG_FLAG_VALID;
        }

        /* Reset wire count */
        RINGLOG_$CTL.wire_count = 0;

        /* Wire the ring buffer memory */
        {
            uint32_t wire_end = RINGLOG_WIRE_END;
            MST_$WIRE_AREA(&ringlog_wire_start,
                           &wire_end,
                           &RINGLOG_$CTL,
                           &ringlog_wire_end,
                           &RINGLOG_$CTL.wire_count);
        }

        /* Set ID filter for command 5 */
        if (cmd == RINGLOG_CMD_START_FILTERED) {
            RINGLOG_$ID = param_words[0];
        } else {
            RINGLOG_$ID = 0;
        }

        /* Enable logging */
        RING_$LOGGING_NOW = (int8_t)0xFF;  /* true = -1 */
        break;

    case RINGLOG_CMD_STOP_COPY:     /* 1 */
    case RINGLOG_CMD_STOP:          /* 4 */
        /*
         * Stop logging.
         */
        RINGLOG_$STOP_LOGGING();
        break;

    case RINGLOG_CMD_SET_NIL_SOCK:  /* 6 */
        /*
         * Set NIL socket filter.
         * param[0] byte: 0 = filter out NIL sockets, -1 = allow
         */
        RINGLOG_$NIL_SOCK = *(int8_t *)param;
        break;

    case RINGLOG_CMD_SET_WHO_SOCK:  /* 7 */
        /*
         * Set WHO socket filter.
         * param[0] byte: 0 = filter out WHO sockets, -1 = allow
         */
        RINGLOG_$WHO_SOCK = *(int8_t *)param;
        break;

    case RINGLOG_CMD_SET_MBX_SOCK:  /* 8 */
        /*
         * Set MBX socket filter.
         * param[0] byte: 0 = filter out MBX sockets, -1 = allow
         */
        RINGLOG_$MBX_SOCK = *(int8_t *)param;
        break;

    default:
        /* Unknown command - do nothing */
        break;
    }

    /*
     * For commands 0, 1, 2: copy buffer contents to output parameter.
     * The bitmask (1 << cmd) & 0x7 checks if cmd is 0, 1, or 2.
     */
    if ((1 << (cmd & 0x1f)) & 0x7) {
        /*
         * Copy the entire ring buffer to the output.
         * Buffer size is 2 + (100 * 46) = 4602 bytes = 0x11FA bytes
         * That's 0x47F 32-bit words (1151 words, but we copy 0x47F = 1151)
         */
        uint32_t *src = (uint32_t *)&RINGLOG_$BUF;
        uint32_t *dst = param_words;
        int16_t count = 0x47F;  /* 1151 words = 4604 bytes (rounded up) */

        for (i = 0; i <= count; i++) {
            *dst++ = *src++;
        }
    }
}
