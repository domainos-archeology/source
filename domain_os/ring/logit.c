/*
 * RINGLOG_$LOGIT - append one packet event to the ring log
 *
 * Original address: 0x00E1A20C, size 504 bytes (0x00E1A20C-0x00E1A403).
 * A5 = 0x00E2C32C (the RINGLOG_ module data base); the buffer is
 * RINGLOG_$DATA at 0x00EA3E38.
 *
 * Returns the entry index it used, or -1 if the event was filtered out.
 */

#include "ring/ring_internal.h"
#include "ring/ringlog_internal.h"

/* 0x00E1A36E / 0x00E1A386: the 24-bit mask applied on the receive path */
#define RINGLOG_NODE_SRC_MASK   0x00FFFFFFu

/*
 * RINGLOG_$LOGIT
 *
 * Parameters (0x08, 0x0C off A6):
 *   header_info - a byte cell; only bit 7 of byte 0 is read
 *   pkt_info    - the packet record being logged
 */
int16_t RINGLOG_$LOGIT(uint8_t *header_info, void *pkt_info)
{
    const uint8_t      *pkt = (const uint8_t *)pkt_info;
    int16_t             result = -1;
    uint16_t            kind;
    int16_t             socket_type;
    int16_t             entry_idx;
    ringlog_$entry_t   *entry;
    ml_$spin_token_t    token;
    uint16_t            word_off;
    int16_t             i;
    uint32_t            node;

    /*
     * 0x00E1A21E-0x00E1A230: when RINGLOG_$ID is set, the packet must name it
     * either at +0x00 or at +0x08.
     */
    if (RINGLOG_$ID != 0 &&
        RINGLOG_$ID != ringlog_$pkt_long(pkt, 0x00) &&
        RINGLOG_$ID != ringlog_$pkt_long(pkt, 0x08)) {
        return -1;
    }

    /* 0x00E1A234-0x00E1A24A: the record kind, zero-extended from pkt[0x0C] */
    kind = pkt[0x0C];
    if (kind == 1) {
        socket_type = ringlog_$pkt_word(pkt, 0x1A);
    } else {
        socket_type = ringlog_$pkt_word(pkt, 0x44);
    }

    /* 0x00E1A24C-0x00E1A26C: above 0x0B the socket is looked up elsewhere */
    if (socket_type > 0x0B) {
        if (kind == 1) {
            socket_type = ringlog_$pkt_word(pkt, 0x1E + 2 * (uint16_t)pkt[0x19]);
        } else {
            socket_type = ringlog_$pkt_word(pkt, 0x38);
        }
    }

    /*
     * 0x00E1A26E-0x00E1A294.  Each filter byte is a Domain boolean: negative
     * means "do not filter this socket type".
     */
    if (RINGLOG_$NIL_SOCK >= 0 && socket_type == RINGLOG_SOCK_NIL) {
        return -1;
    }
    if (RINGLOG_$WHO_SOCK >= 0 && socket_type == RINGLOG_SOCK_WHO) {
        return -1;
    }
    if (RINGLOG_$MBX_SOCK >= 0 && socket_type == RINGLOG_SOCK_MBX) {
        return -1;
    }

    /* 0x00E1A296-0x00E1A2E0: claim the next slot under the spin lock */
    token = ML_$SPIN_LOCK(&RINGLOG_$CTL.spinlock);

    if (RINGLOG_$CTL.first_entry_flag < 0) {
        ringlog_$set_index(0);
    }
    RINGLOG_$CTL.first_entry_flag = 0;

    entry_idx = ringlog_$get_index();
    ringlog_$set_index((int16_t)(ringlog_$get_index() + 1));
    if (ringlog_$get_index() > 99) {
        ringlog_$set_index(0);
    }

    ML_$SPIN_UNLOCK(&RINGLOG_$CTL.spinlock, token);

    /* 0x00E1A2E2-0x00E1A2F2: entry = RINGLOG_$DATA + 0x2E * index */
    result = entry_idx;
    entry  = ringlog_$entry(entry_idx);

    /*
     * 0x00E1A2F6-0x00E1A31E: the flag nibble in the byte at entry + 0x0B.
     * Each bit is set with an explicit and/or pair that preserves the other
     * bits - including the four node-id bits in the byte's high nibble - so
     * the byte is never simply overwritten (bead source-c121).
     */
    {
        uint8_t *flags_byte = &entry->packed[RINGLOG_PACKED_OFF_08 + 3];

        /* andi.b #-0x9 / lsl.b #0x3 / or.b : bit 3 from header_info[0] bit 7 */
        *flags_byte = (uint8_t)((*flags_byte & (uint8_t)~RINGLOG_FLAG_INBOUND) |
                                (uint8_t)(((header_info[0] >> 7) & 1) << 3));

        /* bset.b #0x2 */
        *flags_byte |= RINGLOG_FLAG_VALID;

        /* seq / lsr.b #0x7 / andi.b #-0x3 / add.b / or.b : bit 1 from kind==1 */
        *flags_byte = (uint8_t)((*flags_byte & (uint8_t)~RINGLOG_FLAG_SEND) |
                                (uint8_t)((kind == 1 ? 1u : 0u) << 1));
    }

    /*
     * 0x00E1A320-0x00E1A330:
     *   andi.l #-0xfffff1,(0x8,A2)   long@0x08 &= 0xFF00000F
     *   move.l (A3),D1 / lsl.l #0x4 / or.l D1,(0x8,A2)
     */
    ringlog_$put_packed(entry, RINGLOG_PACKED_OFF_08,
                        (ringlog_$get_packed(entry, RINGLOG_PACKED_OFF_08) & 0xFF00000Fu) |
                        (ringlog_$pkt_long(pkt, 0x00) << 4));

    /* 0x00E1A332: move.w (0x16,A3),(0x14,A2) */
    entry->pkt_type = ringlog_$pkt_uword(pkt, 0x16);

    /*
     * 0x00E1A338-0x00E1A364: thirteen words ("moveq #0xc,D1" + dbf) starting
     * at pkt + 2 * ((pkt[0x18] + 0x1E) >> 1).  The last of them lands at
     * entry + 0x2E, in the next entry's shared word; that is what the image
     * does.
     */
    word_off = (uint16_t)(((uint32_t)pkt[0x18] + 0x1E) >> 1);
    for (i = 0; i < 13; i++) {
        entry->pkt_words[i] = ringlog_$pkt_uword(pkt, (uint16_t)(2 * word_off + 2 * i));
    }

    /* 0x00E1A366: btst.b #0x1,(0xb,A2) - the SEND flag just written */
    if ((entry->packed[RINGLOG_PACKED_OFF_08 + 3] & RINGLOG_FLAG_SEND) == 0) {
        /*
         * Receive path, 0x00E1A36E-0x00E1A3B8.
         *   move.l #0xffffff,D5 / and.l (0x34,A3),D5
         *   andi.l #-0xfffff01,(0x6,A2)   long@0x06 &= 0xF00000FF
         *   lsl.l #0x8,D5 / or.l D5,(0x6,A2)
         */
        node = ringlog_$pkt_long(pkt, 0x34) & RINGLOG_NODE_SRC_MASK;
        ringlog_$put_packed(entry, RINGLOG_PACKED_OFF_06,
                            (ringlog_$get_packed(entry, RINGLOG_PACKED_OFF_06) & 0xF00000FFu) |
                            (node << 8));

        /*
         *   move.l #0xffffff,D5 / and.l (0x40,A3),D5
         *   andi.l #0xfff,(0x4,A2)        long@0x04 &= 0x00000FFF
         *   lsl.l #0x8,D5 / lsl.l #0x4,D5 / or.l D5,(0x4,A2)
         */
        node = ringlog_$pkt_long(pkt, 0x40) & RINGLOG_NODE_SRC_MASK;
        ringlog_$put_packed(entry, RINGLOG_PACKED_OFF_04,
                            (ringlog_$get_packed(entry, RINGLOG_PACKED_OFF_04) & 0x00000FFFu) |
                            (node << 12));

        /* 0x00E1A3A0 / 0x00E1A3A6 */
        entry->sock_byte_03 = pkt[0x39];
        entry->sock_byte_02 = pkt[0x45];

        /* 0x00E1A3AC / 0x00E1A3B2 */
        entry->field_10 = ringlog_$pkt_long(pkt, 0x2E);
        entry->field_0c = ringlog_$pkt_long(pkt, 0x3A);
    } else {
        /*
         * Send path, 0x00E1A3BA-0x00E1A3F8.  Note the two node values are NOT
         * masked to 24 bits here, unlike the receive path above.
         */
        entry->field_0c = 0;
        entry->field_10 = 0;

        ringlog_$put_packed(entry, RINGLOG_PACKED_OFF_06,
                            (ringlog_$get_packed(entry, RINGLOG_PACKED_OFF_06) & 0xF00000FFu) |
                            (ringlog_$pkt_long(pkt, 0x00) << 8));

        ringlog_$put_packed(entry, RINGLOG_PACKED_OFF_04,
                            (ringlog_$get_packed(entry, RINGLOG_PACKED_OFF_04) & 0x00000FFFu) |
                            (ringlog_$pkt_long(pkt, 0x08) << 12));

        /* 0x00E1A3E6 */
        entry->sock_byte_02 = pkt[0x1B];

        /* 0x00E1A3EC-0x00E1A3F4: pkt[0x1F + 2 * pkt[0x19]] */
        entry->sock_byte_03 = pkt[0x1F + 2 * (uint16_t)pkt[0x19]];
    }

    return result;
}
