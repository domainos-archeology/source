/*
 * MAC_$SEND - Send a packet on a MAC channel
 *
 * Validates the channel, optionally resolves the link address with
 * MAC_OS_$ARP, copies the caller's descriptor into a local one that asks
 * MAC_OS_$SEND to build the header buffers, and hands that to MAC_OS_$SEND.
 *
 * Original address: 0x00E0BB12, size 316 bytes (0x00E0BB12-0x00E0BC4D).
 * A5 = 0x00E22990 (MAC_OS_$DATA), so every "(d,A5)" below is a field of the
 * MAC_OS module block that mac_os/mac_os_data.c models.
 *
 * The routine touches no hardware: the channel table, the ARP record and the
 * buffer chain are all ordinary memory, so the body builds unchanged on the
 * host (bead source-d6vh; it used to sit under "#if defined(ARCH_M68K)" with
 * a not-implemented stub).
 */

#include "mac/mac_internal.h"

/*
 * 0x00E0BB30 "cmpi.w #0xa,(A2)" / "bcc" - the channel bound, checked before
 * the table is indexed.  MAC_MAX_CHANNELS (mac/mac.h) is the same 10.
 */

void MAC_$SEND(uint16_t *channel, mac_$send_pkt_t *pkt_desc,
               uint16_t *bytes_sent, status_$t *status_ret)
{
    uint16_t            chan_num;
    mac_os_$channel_t  *chan;               /* A6-0x80 */
    uint16_t            flags;              /* D1 */
    uint16_t            owner_asid;         /* D2 */
    uint16_t            port_num;
    status_$t           cleanup_status;     /* A6-0x74 */
    status_$t           os_status;          /* A6-0x70 */
    uint8_t             cleanup_buf[24];    /* A6-0x68 */

    mac_$send_pkt_t     local_pkt;          /* A6-0x50 */
    uint16_t            local_bytes_sent;   /* A6-0x76 */
    mac_os_$buf_desc_t *chain;
    int8_t              arp_broadcast;      /* A6-0x78: MAC_OS_$ARP's
                                             *          fourth argument */

    /* 0x00E0BB20-0x00E0BB2A: both out-cells are cleared before any check. */
    *bytes_sent = 0;
    *status_ret = status_$ok;

    /* 0x00E0BB2C-0x00E0BB34 */
    chan_num = *channel;
    if (chan_num >= MAC_MAX_CHANNELS) {
        *status_ret = status_$mac_channel_not_open;
        return;
    }

    /*
     * 0x00E0BB36-0x00E0BB44: the image forms A0 = A5 + chan * 20 and keeps it
     * in the frame, then reads (0x7b2,A0) and (0x7aa,A0) - i.e. the channel
     * entry at A5+0x7A0 with field offsets 0x12 and 0x0A.
     */
    chan = &MAC_OS_$CHANNEL_TABLE[chan_num];

    /* 0x00E0BB48: move.w (0x7b2,A0),D1w / btst.l #0x9,D1 - word bit 9 */
    flags = chan->flags;
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
    if (owner_asid != PROC1_$AS_ID) {           /* 0x00E0BB5C */
        *status_ret = status_$mac_channel_not_open;
        return;
    }

    /* 0x00E0BBA2: move.w (0x7aa,A0),-(SP) */
    port_num = chan->port_index;

    /*
     * 0x00E0BB6E-0x00E0BB84: establish the cleanup handler.  FIM_$CLEANUP
     * returns status_$cleanup_handler_set on the way in; any other value is
     * the fault status arriving on the unwind, and 0x00E0BC3E stores D0 - the
     * value just returned - into the caller's cell.  The image neither
     * releases the handler nor touches the descriptor on that path.
     */
    cleanup_status = FIM_$CLEANUP(cleanup_buf);
    if (cleanup_status != status_$cleanup_handler_set) {
        *status_ret = cleanup_status;
        return;
    }

    /*
     * 0x00E0BB8C: "tst.b (0x18,A0) / bpl" - when the caller's is_broadcast
     * byte is true, resolve the link address in place.  The pushes at
     * 0x00E0BB92-0x00E0BBAE are, in argument order, the module's constant
     * broadcast next-hop record (0x00E0BBA6 "pea (0x8e0,A5)"), the port
     * number word, the packet descriptor itself as the link-address output,
     * a local byte for the broadcast answer, and the caller's status cell.
     * The "subq.l #0x2,SP" ahead of them is the discarded word result of a
     * Pascal function.
     */
    if (pkt_desc->is_broadcast < 0) {
        MAC_OS_$ARP((void *)&MAC_OS_$BROADCAST_NEXTHOP, (int16_t)port_num,
                    (uint16_t *)pkt_desc, (uint8_t *)&arp_broadcast,
                    status_ret);

        /* 0x00E0BBB2-0x00E0BBB8: on failure skip straight to the release. */
        if (*status_ret != status_$ok) {
            FIM_$RLS_CLEANUP(cleanup_buf);      /* 0x00E0BC32 */
            return;
        }
    }

    /*
     * Build the local descriptor MAC_OS_$SEND is given
     * (0x00E0BBBA-0x00E0BBF2).
     */

    /*
     * 0x00E0BBC2 "moveq #0x5,D2 / move.l (A0)+,(A1)+ / dbf" - six longwords,
     * 0x00..0x17, which is exactly the link address record.
     */
    local_pkt.link_addr = pkt_desc->link_addr;

    /* 0x00E0BBCE: move.b (0x18,A0),(-0x38,A6) */
    local_pkt.is_broadcast = pkt_desc->is_broadcast;

    /* 0x00E0BBD4: move.l (0x30,A0),(-0x20,A6) */
    local_pkt.frame_type = pkt_desc->frame_type;

    /* 0x00E0BBDA / 0x00E0BBDE: clr.l (-0x18,A6) / clr.l (-0x14,A6) */
    local_pkt.data_length   = 0;
    local_pkt.data_pages[0] = 0;

    /*
     * 0x00E0BBE2-0x00E0BBEE: "lea (0x1c,A0),A1 / lea (-0x34,A6),A2" then
     * three "move.l (A1)+,(A2)+" - the twelve-byte header descriptor.
     */
    local_pkt.hdr_desc = pkt_desc->hdr_desc;

    /*
     * 0x00E0BBF0 "clr.b (-0x28,A6)": clear "header already built" so
     * MAC_OS_$SEND allocates and fills the header buffers itself.
     */
    local_pkt.hdr_prebuilt = 0;

    /*
     * 0x00E0BBF4-0x00E0BC06: walk the CALLER's chain from
     * pkt_desc->hdr_desc.next ("movea.l (0x24,A0),A0", and hdr_desc is at
     * +0x1C so +0x24 is its next field) clearing the byte at +0x0C of every
     * entry.  mac_os_$buf_desc_t is {length, address, next}, so +0x0C is the
     * first byte past the record - the flag byte of whatever the chain entry
     * is embedded in.  Reproduced as found.
     */
    for (chain = (mac_os_$buf_desc_t *)ARCH_VA_TO_PTR(pkt_desc->hdr_desc.next);
         chain != NULL;
         chain = (mac_os_$buf_desc_t *)ARCH_VA_TO_PTR(chain->next)) {
        *(uint8_t *)((uint8_t *)chain + sizeof(mac_os_$buf_desc_t)) = 0;
    }

    /*
     * 0x00E0BC08-0x00E0BC18: MAC_OS_$SEND(channel, &local_pkt,
     * &local_bytes_sent, &os_status) - the caller's channel cell is passed
     * through unchanged.
     */
    MAC_OS_$SEND((int16_t *)channel, &local_pkt,
                 (int16_t *)&local_bytes_sent, &os_status);

    /* 0x00E0BC20-0x00E0BC2E: copy the word and the longword back out. */
    *bytes_sent = local_bytes_sent;
    *status_ret = os_status;

    /* 0x00E0BC32: pea (-0x68,A6) / jsr FIM_$RLS_CLEANUP */
    FIM_$RLS_CLEANUP(cleanup_buf);
}
