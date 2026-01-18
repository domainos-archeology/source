/*
 * LOG_$ADD - Add an entry to the system log
 *
 * Adds a timestamped entry to the circular log buffer. The entry
 * includes a type code and variable-length data. Uses spin locks
 * for thread safety.
 *
 * Original address: 00e1763e
 * Original size: 312 bytes
 *
 * The log buffer is organized as:
 *   [0]: head index (first valid entry)
 *   [1]: tail index (next free slot)
 *   [2...]: entries
 *
 * Each entry is:
 *   [0]: size in words (including this header)
 *   [1]: type code
 *   [2-3]: timestamp (32-bit)
 *   [4...]: data words
 *
 * Assembly (key parts):
 *   00e17654    tst.l (0x14,A5)         ; check LOG_$LOGFILE_PTR
 *   00e17682    jsr ML_$SPIN_LOCK
 *   00e17764    jsr ML_$SPIN_UNLOCK
 */

#include "log/log_internal.h"

/* Reference to current time */
extern uint32_t TIME_$CURRENT_CLOCKH;

/* Early log extended for copying entry data */
extern early_log_extended_t EARLY_LOG_EXTENDED;

void LOG_$ADD(int16_t type, void *data, int16_t data_len)
{
    int16_t *buf;
    int16_t *entry;
    int16_t *src;
    int16_t words_needed;
    int16_t data_words;
    int16_t new_tail;
    int16_t head, tail;
    int16_t i;
    uint16_t lock_token;

    /* Check if log is initialized */
    if (LOG_$LOGFILE_PTR == NULL) {
        return;
    }

    /* Calculate words needed: 4 header words + data words */
    /* data_words = (data_len + 1) / 2 */
    data_words = (data_len + 1) >> 1;
    words_needed = data_words + 4;

    /* Validate entry size (must be 1-100 words) */
    if (words_needed <= 0 || words_needed > LOG_MAX_ENTRY_WORDS) {
        return;
    }

    /* Clear dirty flag during update */
    LOG_$STATE.dirty_flag = 0;

    /* Acquire spin lock */
    lock_token = ML_$SPIN_LOCK(&LOG_$STATE.spin_lock);

    buf = LOG_$LOGFILE_PTR;
    head = buf[0];
    tail = buf[1];

    /* Calculate new tail position */
    new_tail = tail + words_needed - 1;

    /* Check if we need to wrap around */
    if (new_tail > LOG_MAX_INDEX) {
        /* Mark end of buffer and wrap */
        buf[tail + 1] = 0;  /* End marker */
        buf[0] = 1;         /* Reset head */
        buf[1] = 1;         /* Reset tail */
        new_tail = words_needed;
    }

    /* Skip over old entries if we're overwriting them */
    while (buf[1] <= buf[0] && buf[0] <= new_tail) {
        int16_t entry_size = buf[buf[0] + 1];
        if (entry_size == 0 || (buf[0] += entry_size) > (LOG_MAX_INDEX - 1)) {
            buf[0] = 1;  /* Wrap head */
        }
    }

    /* Get pointer to new entry location */
    entry = &buf[buf[1] + 1];
    LOG_$STATE.current_entry_ptr = entry;

    /* Write entry header */
    entry[0] = words_needed;                    /* Size */
    entry[1] = type;                            /* Type */
    *(uint32_t *)&entry[2] = TIME_$CURRENT_CLOCKH; /* Timestamp */

    /* Also save to early log extended structure for crash recovery */
    EARLY_LOG_EXTENDED.data_len = words_needed;
    EARLY_LOG_EXTENDED.type = type;
    EARLY_LOG_EXTENDED.timestamp = TIME_$CURRENT_CLOCKH;

    /* Copy data words */
    src = (int16_t *)data;
    data_words--;  /* Adjust for loop */
    if (data_words >= 0) {
        for (i = 0; i <= data_words; i++) {
            entry[4 + i] = src[i];
            /* Also copy to early log buffer */
            ((int16_t *)EARLY_LOG_EXTENDED.data)[i] = src[i];
        }
    }

    /* Update tail pointer */
    buf[1] += words_needed;
    if (buf[1] > LOG_MAX_INDEX) {
        buf[1] = 1;  /* Wrap */
    }

    /* Release spin lock */
    ML_$SPIN_UNLOCK(&LOG_$STATE.spin_lock, lock_token);

    /* Mark log as dirty */
    LOG_$STATE.dirty_flag = (int8_t)-1;
}
