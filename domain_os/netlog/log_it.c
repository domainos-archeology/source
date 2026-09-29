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
 *
 * Module data through NETLOG_$DATA: Claude Opus 5.5 (source-iq58).
 */

#include "netlog/netlog_internal.h"

void NETLOG_$LOG_IT(uint16_t kind, uint32_t *uid,
                    uint16_t param3, uint16_t param4,
                    uint16_t param5, uint16_t param6,
                    uint16_t param7, uint16_t param8)
{
    netlog_$data_t *nl = &NETLOG_$DATA;
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
     * 0xE71B90..0xE71BA6: the counter array is Pascal [1..2]
     * (`(0x6e,A0)` with A0 = A5 + current_buf_index*2), and the index used
     * below is the value AFTER the increment.
     */
    nl->page_counts[nl->current_buf_index]++;
    entry_index = nl->page_counts[nl->current_buf_index];

    /*
     * 0xE71BB6..0xE71BCA computes A0 = current_buf_ptr + entry_index*26
     * (D1 = n*2; D0 = D1<<2 = n*8; D1 += D0 = n*10; D0 += D0 = n*16;
     * D1 += D0 = n*26) and then writes every field at a negative
     * displacement from -0x1A to -0x02, so the record actually begins one
     * entry lower.
     */
    entry = NETLOG_ENTRY_ADDR(ARCH_VA_TO_PTR(nl->current_buf_ptr),
                              entry_index);

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
    /* 0x00E71BD2: move.b (0x00e20609).l - the low-order byte of the word
     * PROC1_$CURRENT (0xE20608) */
    entry->process_id = (uint8_t)PROC1_$CURRENT;
    /*
     * 0xE71BDA: move.l (-0xe,A6),(-0x18,A0).  TIME_$CLOCK filled the 6-byte
     * clock at (-0x10,A6), so this longword is bytes 2..5 of it - the LOW
     * 16 bits of `high` joined to the 16-bit `low`, i.e. the middle 32 bits
     * of the 48-bit clock, not `high`.
     */
    entry->timestamp = ((timestamp.high & 0xFFFFu) << 16) | timestamp.low;
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
    /* 0xE71C14: cmpi.w #0x27,(0x6e,A1) */
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
        nl->page_counts[nl->current_buf_index] = 0;   /* 0xE71C38 */

        /*
         * Set flag to advance event count after releasing lock
         */
        need_advance = (int8_t)0xFF;

        /*
         * Update current buffer pointer
         * 0xE71C46: move.l (0x54,A5,D0*1),(0x74,A5), D0 = index*4
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
