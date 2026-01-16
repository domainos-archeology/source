/*
 * NETLOG_$LOG_IT - Log an event
 *
 * Records a log entry if logging is enabled for the specified kind.
 * The entry is buffered and sent when the buffer fills (39 entries).
 *
 * This function uses a spin lock for thread safety and double-buffering
 * to allow one buffer to be sent while the other accumulates entries.
 *
 * Original address: 0x00E71B38
 */

#include "netlog/netlog_internal.h"

void NETLOG_$LOG_IT(uint16_t kind, uint32_t *uid,
                    uint16_t param3, uint16_t param4,
                    uint16_t param5, uint16_t param6,
                    uint16_t param7, uint16_t param8)
{
    netlog_data_t *nl = NETLOG_DATA;
    ml_$spin_token_t token;
    clock_t timestamp;
    netlog_entry_t *entry;
    int16_t entry_index;
    int8_t need_advance = 0;

    /*
     * Copy UID to local storage (8 bytes)
     * This matches the original's behavior of copying via A0+
     */
    uint32_t uid_high = uid[0];
    uint32_t uid_low = uid[1];

    /*
     * Check if logging is enabled for this kind
     * NETLOG_$KINDS is a bitmask; check if bit 'kind' is set
     */
    if ((NETLOG_$KINDS & (1 << (kind & 0x1F))) == 0) {
        return;
    }

    /*
     * Acquire spin lock for thread safety
     */
    token = ML_$SPIN_LOCK(&nl->spin_lock);

    /*
     * Get current timestamp (high 32 bits of 48-bit clock)
     */
    TIME_$CLOCK(&timestamp);

    /*
     * Increment entry count for current buffer and get index
     * Entry indices are 1-based (1 to 39)
     */
    nl->page_counts[nl->current_buf_index]++;
    entry_index = nl->page_counts[nl->current_buf_index];

    /*
     * Calculate entry address:
     *   entry = buffer_base + (entry_index * 26)
     *
     * The original code calculates: entry_index * 26
     *   = entry_index * (2 + 8 + 16)
     *   = entry_index * 2 * (1 + 4) + entry_index * 2 * 8
     *   = entry_index * 0x1A
     */
    entry = (netlog_entry_t *)((char *)nl->current_buf_ptr +
                               (entry_index * NETLOG_ENTRY_SIZE));

    /*
     * Fill in the log entry
     * Offsets relative to entry base (entries are written backwards from end):
     *   -0x1A (entry_base + 0): kind
     *   -0x19 (entry_base + 1): process_id
     *   -0x18 (entry_base + 2): timestamp (high 32 bits)
     *   -0x14 (entry_base + 6): uid_high
     *   -0x10 (entry_base + 10): uid_low
     *   -0x0C (entry_base + 14): param3
     *   -0x0A (entry_base + 16): param4 (low byte)
     *   -0x08 (entry_base + 18): param5
     *   -0x06 (entry_base + 20): param6
     *   -0x04 (entry_base + 22): param7
     *   -0x02 (entry_base + 24): param8
     */
    entry->kind = (uint8_t)kind;
    entry->process_id = NETLOG_GET_CURRENT_PID();
    entry->timestamp = timestamp.high;  /* Use high 32 bits */
    entry->uid_high = uid_high;
    entry->uid_low = uid_low;
    entry->param3 = param3;
    entry->param4 = (uint8_t)param4;
    entry->param5 = param5;
    entry->param6 = param6;
    entry->param7 = param7;
    entry->param8 = param8;

    /*
     * Check if buffer is full (39 entries)
     */
    if (nl->page_counts[nl->current_buf_index] == NETLOG_ENTRIES_PER_PAGE) {
        /*
         * Buffer is full - prepare to send it
         * Save the index of the full buffer and increment done count
         */
        nl->send_page_index = nl->current_buf_index;
        nl->done_cnt++;

        /*
         * Switch to the other buffer (1 <-> 2)
         */
        nl->current_buf_index = NETLOG_SWITCH_BUFFER(nl->current_buf_index);

        /*
         * Clear entry count for new buffer
         */
        nl->page_counts[nl->current_buf_index] = 0;

        /*
         * Set flag to advance event count after releasing lock
         */
        need_advance = (int8_t)0xFF;

        /*
         * Update current buffer pointer
         * buffer_va array is indexed [0,1,2] but we use indices 1 and 2
         */
        nl->current_buf_ptr = nl->buffer_va[nl->current_buf_index];
    }

    /*
     * Release spin lock
     */
    ML_$SPIN_UNLOCK(&nl->spin_lock, token);

    /*
     * If a buffer filled, advance the event count to trigger sending
     * This is done after releasing the spin lock to minimize lock hold time
     */
    if (need_advance < 0) {
        EC_$ADVANCE(&NETLOG_$EC);
    }
}
