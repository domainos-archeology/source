/*
 * RINGLOG_$LOGIT - Log a packet event
 *
 * Records a packet send/receive event to the ring log circular buffer.
 *
 * Original address: 0x00E1A20C
 */

#include "ring/ringlog_internal.h"

/*
 * RINGLOG_$LOGIT - Log a packet event
 *
 * This function logs packet send/receive events to a circular buffer for
 * debugging and monitoring. The function implements several filtering
 * mechanisms to avoid logging unwanted packets.
 *
 * Algorithm:
 * 1. Check if the packet's network ID matches the filter (if filter active)
 * 2. Determine the socket type from the packet structure
 * 3. Check socket type filters (NIL, WHO, MBX)
 * 4. Acquire spinlock and allocate next entry in circular buffer
 * 5. Fill in the log entry with packet information
 * 6. Release spinlock
 *
 * The packet info structure layout depends on whether this is a send (type=1)
 * or receive operation:
 *
 * For sends (pkt_info[0x0c] == 1):
 *   - offset 0x00: local network ID
 *   - offset 0x08: remote network ID
 *   - offset 0x0c: packet type flag (1 = send)
 *   - offset 0x16: packet type
 *   - offset 0x18: header length
 *   - offset 0x19: socket index
 *   - offset 0x1a: socket type (short)
 *   - offset 0x1b: socket byte
 *   - offset 0x1e+: socket type table
 *
 * For receives:
 *   - offset 0x00: local network ID
 *   - offset 0x16: packet type
 *   - offset 0x2e: additional info
 *   - offset 0x34: remote network ID (24-bit at offset 0x34)
 *   - offset 0x38: socket type (short)
 *   - offset 0x39: socket byte 2
 *   - offset 0x3a: field_0c (4 bytes)
 *   - offset 0x40: network ID (24-bit)
 *   - offset 0x44: socket type alt
 *   - offset 0x45: socket byte 1
 *
 * Parameters:
 *   header_info - Pointer to header info; bit 7 of byte 0 indicates inbound
 *   pkt_info    - Pointer to packet information structure
 *
 * Returns:
 *   Entry index (0-99) if logged successfully
 *   -1 if packet was filtered out or logging not active
 */
int16_t RINGLOG_$LOGIT(uint8_t *header_info, void *pkt_info)
{
    uint32_t *pkt = (uint32_t *)pkt_info;
    uint8_t *pkt_bytes = (uint8_t *)pkt_info;
    int16_t result = -1;
    int16_t socket_type;
    int8_t is_send;
    int16_t entry_idx;
    int entry_offset;
    ringlog_entry_t *entry;
    ml_$spin_token_t token;
    int16_t i;
    uint8_t *src;

    /* Check network ID filter */
    if (RINGLOG_$ID != 0) {
        /* Filter is active - check if packet matches */
        if (RINGLOG_$ID != pkt[0] && RINGLOG_$ID != pkt[2]) {
            return -1;
        }
    }

    /* Determine packet type (send vs receive) */
    is_send = pkt_bytes[0x0c];

    /* Get socket type based on packet type */
    if (is_send == 1) {
        /* Send packet: socket type at offset 0x1a */
        socket_type = *(int16_t *)&pkt_bytes[0x1a];
    } else {
        /* Receive packet: socket type at offset 0x44 */
        socket_type = *(int16_t *)&pkt_bytes[0x44];
    }

    /* If socket type > 11, look it up in a table */
    if (socket_type > 0x0b) {
        if (is_send == 1) {
            /* For sends: socket table lookup using index at offset 0x19 */
            uint8_t sock_idx = pkt_bytes[0x19];
            socket_type = *(int16_t *)&pkt_bytes[0x1e + (sock_idx * 2)];
        } else {
            /* For receives: socket type at offset 0x38 */
            socket_type = *(int16_t *)&pkt_bytes[0x38];
        }
    }

    /* Apply socket type filters */
    if (RINGLOG_$NIL_SOCK >= 0 && socket_type == RINGLOG_SOCK_NIL) {
        return -1;
    }
    if (RINGLOG_$WHO_SOCK >= 0 && socket_type == RINGLOG_SOCK_WHO) {
        return -1;
    }
    if (RINGLOG_$MBX_SOCK >= 0 && socket_type == RINGLOG_SOCK_MBX) {
        return -1;
    }

    /* Acquire spinlock for buffer access */
    token = ML_$SPIN_LOCK(&RINGLOG_$CTL.spinlock);

    /* Check if this is the first entry after clear/wrap */
    if (RINGLOG_$CTL.first_entry_flag < 0) {
        RINGLOG_$BUF.current_index = 0;
    }
    RINGLOG_$CTL.first_entry_flag = 0;

    /* Get current entry index and advance */
    entry_idx = RINGLOG_$BUF.current_index;
    RINGLOG_$BUF.current_index++;

    /* Wrap around at end of buffer */
    if (RINGLOG_$BUF.current_index > 99) {
        RINGLOG_$BUF.current_index = 0;
    }

    /* Release spinlock */
    ML_$SPIN_UNLOCK(&RINGLOG_$CTL.spinlock, token);

    /* Calculate entry pointer */
    result = entry_idx;
    entry = &RINGLOG_$BUF.entries[entry_idx];

    /* Set flags byte */
    /* Clear inbound flag (bit 3), then set from header_info bit 7 */
    uint8_t flags = 0;
    flags |= (header_info[0] >> 7) << 3;  /* Inbound flag */
    flags |= RINGLOG_FLAG_VALID;           /* Mark as valid */

    /* Set send flag based on packet type */
    if (is_send == 1) {
        flags |= RINGLOG_FLAG_SEND;
    }

    /* Store flags (at offset 0x0B within entry, which is byte 3 of local_network_id_flags) */
    uint8_t *entry_bytes = (uint8_t *)entry;
    entry_bytes[0x0b] = flags;

    /* Store local network ID (shifted left 4 bits) */
    /* Preserve low 4 bits, store network ID in upper 28 bits */
    entry->local_network_id_flags = (entry->local_network_id_flags & 0x0000000F) |
                                     (pkt[0] << 4);

    /* Store packet type from offset 0x16 */
    entry->packet_type = *(uint16_t *)&pkt_bytes[0x16];

    /* Copy packet data (13 words = 26 bytes) starting from calculated offset */
    /* The offset is based on header length field at pkt[6] (byte offset 0x18) */
    uint8_t hdr_len = pkt_bytes[0x18];
    int data_offset = (((hdr_len + 0x1e) >> 1) + 1) * 2;
    src = &pkt_bytes[data_offset];

    /* Copy 13 words (26 bytes) to entry->packet_data (but structure has 24 bytes) */
    /* Actually copy to offset 0x16 which extends past packet_data */
    for (i = 0; i < 13; i++) {
        *(uint16_t *)&entry_bytes[0x16 + i*2] = *(uint16_t *)&src[i*2 - 2];
    }

    /* Fill in remaining fields based on send vs receive */
    if ((flags & RINGLOG_FLAG_SEND) == 0) {
        /* Receive packet */
        /* Remote network ID from offset 0x34 (24-bit, shifted << 8) */
        uint32_t remote_id = pkt[0x0d] & 0x00FFFFFF;  /* pkt[0x0d] = offset 0x34 */
        entry->remote_network_id = (entry->remote_network_id & 0x00000FFF) |
                                    (remote_id << 12);

        /* Local network ID field from offset 0x40 (24-bit, shifted << 12) */
        uint32_t local_id = pkt[0x10] & 0x00FFFFFF;  /* pkt[0x10] = offset 0x40 */
        /* Store in different field - using packed format */
        /* This is stored in bits overlapping remote_network_id low bits */
        entry->remote_network_id = (entry->remote_network_id & 0xFFFFF000) |
                                    ((local_id >> 12) & 0xFFF);

        /* Socket bytes */
        entry->sock_byte2 = pkt_bytes[0x39];
        entry->sock_byte1 = pkt_bytes[0x45];

        /* Additional fields */
        entry->field_10 = *(uint32_t *)&pkt_bytes[0x2e];
        entry->field_0c = *(uint32_t *)&pkt_bytes[0x3a];
    } else {
        /* Send packet */
        /* Clear extra fields for sends */
        entry->field_0c = 0;
        entry->field_10 = 0;

        /* Remote network ID from pkt[0] (local node sending to) */
        entry->remote_network_id = (entry->remote_network_id & 0x00000FFF) |
                                    ((pkt[0] & 0x00FFFFFF) << 12);

        /* Local network ID from pkt[2] */
        entry->remote_network_id = (entry->remote_network_id & 0xFFFFF000) |
                                    ((pkt[2] >> 12) & 0xFFF);

        /* Socket bytes for send */
        entry->sock_byte1 = pkt_bytes[0x1b];
        uint8_t sock_idx = pkt_bytes[0x19];
        entry->sock_byte2 = pkt_bytes[0x1f + sock_idx * 2];
    }

    return result;
}
