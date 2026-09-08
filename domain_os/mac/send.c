/*
 * MAC_$SEND - Send a packet on a MAC channel
 *
 * Sends a packet on the specified channel. May perform ARP lookup
 * if the destination address is not cached.
 *
 * Original address: 0x00E0BB12
 * Original size: 316 bytes
 */

#include "mac/mac_internal.h"

void MAC_$SEND(uint16_t *channel, mac_$send_pkt_t *pkt_desc,
               uint16_t *bytes_sent, status_$t *status_ret)
{
    uint16_t chan;
    uint32_t chan_offset;
    uint16_t flags;
    uint16_t owner_asid;
    uint16_t port_num;
    status_$t cleanup_status;
    status_$t os_status;
    uint8_t cleanup_buf[24];  /* FIM cleanup handler context */

    /* Local copy of packet descriptor for MAC_OS_$SEND */
    mac_$send_pkt_t local_pkt;
    uint16_t local_bytes_sent;
    void *chain_ptr;
    int8_t arp_broadcast;       /* A6-0x78: MAC_OS_$ARP's fourth argument */

    *bytes_sent = 0;
    *status_ret = status_$ok;

#if defined(ARCH_M68K)
    chan = *channel;

    /*
     * Validate channel number and ownership.
     * Channel must be < 10, flag bit 9 (MAC_OS_CHANNEL_IN_USE) must be set,
     * and owner ASID (bits 2-7 of flags >> 2) must match current ASID.
     */
    if (chan >= MAC_MAX_CHANNELS) {
        *status_ret = status_$mac_channel_not_open;
        return;
    }

    /* Calculate channel table offset: chan * 20 */
    chan_offset = (uint32_t)chan * 20;

    /* Read flags from channel entry at offset 0x7B2 */
    flags = *(uint16_t *)(MAC_$DATA_BASE + 0x7B2 + chan_offset);

    /* 0x00E0BB48: move.w (0x7b2,A0),D1w / btst.l #0x9,D1 - word bit 9 */
    if ((flags & MAC_OS_CHANNEL_IN_USE) == 0) {
        *status_ret = status_$mac_channel_not_open;
        return;
    }

    /*
     * 0x00E0BB52: move.w #0xfc,D2w / and.b (0x7b2,A0),D2b / lsr.w #0x2,D2w.
     * The and.b works on the flags word's HIGH byte, so the owner is word
     * bits 10..15, not bits 2..7.
     */
    owner_asid = (uint16_t)((flags & MAC_OS_CHANNEL_OWNER_MASK)
                            >> MAC_OS_CHANNEL_OWNER_SHIFT);
    if (owner_asid != PROC1_$AS_ID) {
        *status_ret = status_$mac_channel_not_open;
        return;
    }

    /* Get port number from channel entry at offset 0x7AA */
    port_num = *(uint16_t *)(MAC_$DATA_BASE + 0x7AA + chan_offset);

    /*
     * Set up cleanup handler to handle faults during send.
     * FIM_$CLEANUP returns status_$cleanup_handler_set on initial call,
     * or an error status if cleanup is triggered.
     */
    cleanup_status = FIM_$CLEANUP(cleanup_buf);
    if (cleanup_status != status_$cleanup_handler_set) {
        /* Cleanup was triggered - return the error */
        *status_ret = cleanup_status;
        return;
    }

    /*
     * If the caller asked for ARP (the boolean at +0x18 is true), resolve the
     * link address in place: 0x00E0BB8C "tst.b (0x18,A0) / bpl", and the ARP
     * call at 0x00E0BB92-0x00E0BBAE passes the packet descriptor itself as
     * the link-address output and a local byte as the broadcast answer.
     */
    if (pkt_desc->is_broadcast < 0) {
        MAC_OS_$ARP(MAC_$ARP_TABLE, port_num, (uint16_t *)pkt_desc,
                    (uint8_t *)&arp_broadcast, status_ret);
        if (*status_ret != status_$ok) {
            FIM_$RLS_CLEANUP(cleanup_buf);
            return;
        }
    }

    /*
     * Build the local descriptor MAC_OS_$SEND is given
     * (0x00E0BBBA-0x00E0BBF2).
     */

    /* Six longwords, 0x00..0x17 (0x00E0BBC2 "moveq #0x5" + dbf) */
    {
        uint32_t *src = (uint32_t *)pkt_desc;
        uint32_t *dst = (uint32_t *)&local_pkt;
        int i;
        for (i = 0; i < 6; i++) {
            dst[i] = src[i];
        }
    }

    /* +0x18 as a byte (0x00E0BBCE) */
    local_pkt.is_broadcast = pkt_desc->is_broadcast;

    /* +0x30 (0x00E0BBD4) */
    local_pkt.frame_type = pkt_desc->frame_type;

    /* +0x38 and +0x3C cleared (0x00E0BBDA, 0x00E0BBDE) */
    local_pkt.data_length = 0;
    local_pkt.data_pages[0] = 0;

    /* The header descriptor, three longwords (0x00E0BBE2-0x00E0BBEE) */
    local_pkt.hdr_desc = pkt_desc->hdr_desc;

    /*
     * Clear "header already built" so MAC_OS_$SEND allocates and fills the
     * header buffers itself (0x00E0BBF0 clr.b (-0x28,A6)).
     */
    local_pkt.hdr_prebuilt = 0;

    /*
     * Walk the buffer chain and clear flags in each buffer entry.
     * Original clears byte at offset 0xC in each chain entry.
     */
    for (chain_ptr = (void *)(uintptr_t)pkt_desc->hdr_desc.next;  /* 0x00E0BBF4 */
         chain_ptr != NULL;
         chain_ptr = (void *)(*(uint32_t *)((uint8_t *)chain_ptr + 8))) {
        *(uint8_t *)((uint8_t *)chain_ptr + 0xC) = 0;
    }

    /* Call MAC_OS_$SEND to actually transmit the packet */
    /* MAC_OS_$SEND is declared with int16_t * channel/bytes_sent and a
     * mac_os_$send_pkt_t * descriptor; the same addresses are pushed here. */
    MAC_OS_$SEND((int16_t *)channel, &local_pkt,
                 (int16_t *)&local_bytes_sent, &os_status);

    *bytes_sent = local_bytes_sent;
    *status_ret = os_status;

    /* Release cleanup handler */
    FIM_$RLS_CLEANUP(cleanup_buf);

#else
    /* Non-M68K implementation stub */
    (void)chan;
    (void)chan_offset;
    (void)flags;
    (void)owner_asid;
    (void)port_num;
    (void)cleanup_status;
    (void)os_status;
    (void)cleanup_buf;
    (void)local_pkt;
    (void)local_bytes_sent;
    (void)chain_ptr;
    *status_ret = status_$mac_channel_not_open;
#endif
}
