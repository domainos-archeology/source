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
    uint8_t owner_asid;
    uint16_t port_num;
    status_$t cleanup_status;
    status_$t os_status;
    uint8_t cleanup_buf[24];  /* FIM cleanup handler context */

    /* Local copy of packet descriptor for MAC_OS_$SEND */
    mac_$send_pkt_t local_pkt;
    uint16_t local_bytes_sent;
    void *chain_ptr;

    *bytes_sent = 0;
    *status_ret = status_$ok;

#if defined(M68K)
    chan = *channel;

    /*
     * Validate channel number and ownership.
     * Channel must be < 10, flag bit 9 (0x200) must be set (channel open),
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

    /* Check if channel is open (bit 9 / 0x200) */
    if ((flags & 0x200) == 0) {
        *status_ret = status_$mac_channel_not_open;
        return;
    }

    /* Check ownership: bits 2-7 >> 2 gives owner ASID */
    owner_asid = (flags & 0xFC) >> 2;
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
     * If arp_flag is negative, we need to do ARP lookup.
     * Otherwise, skip ARP and send directly.
     */
    if (pkt_desc->arp_flag < 0) {
        /* Do ARP lookup */
        MAC_OS_$ARP(MAC_$ARP_TABLE, port_num, pkt_desc, NULL, status_ret);
        if (*status_ret != status_$ok) {
            FIM_$RLS_CLEANUP();
            return;
        }
    }

    /*
     * Copy first 6 longwords (24 bytes) of packet descriptor to local copy.
     * This includes dest/src addresses, type, etc.
     */
    {
        uint32_t *src = (uint32_t *)pkt_desc;
        uint32_t *dst = (uint32_t *)&local_pkt;
        int i;
        for (i = 0; i < 6; i++) {
            dst[i] = src[i];
        }
    }

    /* Copy arp_flag (offset 0x18) */
    local_pkt.arp_flag = pkt_desc->arp_flag;

    /* Copy total_length (offset 0x30) */
    local_pkt.total_length = ((uint32_t *)pkt_desc)[0xC];

    /* Clear some local fields */
    ((uint32_t *)&local_pkt)[6] = 0;  /* offset 0x18 area */
    ((uint32_t *)&local_pkt)[7] = 0;  /* offset 0x1C area */

    /* Copy header info (offsets 0x1C-0x27) */
    ((uint32_t *)&local_pkt)[7] = ((uint32_t *)pkt_desc)[7];  /* 0x1C */
    ((uint32_t *)&local_pkt)[8] = ((uint32_t *)pkt_desc)[8];  /* 0x20 */
    ((uint32_t *)&local_pkt)[9] = ((uint32_t *)pkt_desc)[9];  /* 0x24 */

    /* Clear a byte (offset 0x28) */
    ((uint8_t *)&local_pkt)[0x28] = 0;

    /*
     * Walk the buffer chain and clear flags in each buffer entry.
     * Original clears byte at offset 0xC in each chain entry.
     */
    for (chain_ptr = (void *)((uint32_t *)pkt_desc)[9]; /* body_chain at 0x24 */
         chain_ptr != NULL;
         chain_ptr = (void *)(*(uint32_t *)((uint8_t *)chain_ptr + 8))) {
        *(uint8_t *)((uint8_t *)chain_ptr + 0xC) = 0;
    }

    /* Call MAC_OS_$SEND to actually transmit the packet */
    MAC_OS_$SEND(channel, &local_pkt, &local_bytes_sent, &os_status);

    *bytes_sent = local_bytes_sent;
    *status_ret = os_status;

    /* Release cleanup handler */
    FIM_$RLS_CLEANUP();

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
