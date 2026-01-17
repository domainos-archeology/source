/*
 * APP_$RECEIVE - Receive a packet on a socket
 *
 * Receives the next available packet from a socket and parses the
 * network headers. Handles both local and remote network packets.
 *
 * Original address: 0x00E00800
 */

#include "app/app_internal.h"

void APP_$RECEIVE(uint16_t sock_num, void *result, status_$t *status_ret)
{
    app_receive_result_t *res = (app_receive_result_t *)result;
    uint8_t *pkt_ptr;
    uint8_t *sock_entry;
    uint8_t sock_flags;
    int8_t get_result;
    int8_t holding_lock;
    uint16_t hdr_offset;
    uint16_t data_len;
    uint16_t total_size;
    uint16_t net_type;
    app_pkt_hdr_t *out_hdr;

    /* Local variables from stack */
    uint8_t *local_pkt;
    uint32_t local_routing_key;
    uint16_t local_sock;
    uint8_t local_flags;

    /* Stack space for UIDs */
    uint32_t uid_data[4];  /* For src/dest UIDs */

    *status_ret = status_$ok;
    holding_lock = 0;

    /* Try to get a packet from the socket */
    get_result = SOCK_$GET(sock_num, (void **)&local_pkt);

    if (get_result >= 0) {
        /* No packet available */
        *status_ret = status_$network_buffer_queue_is_empty;
        return;
    }

    /* Copy UID data from local stack to result */
    res->src_uid_high = uid_data[0];
    res->src_uid_low = uid_data[1];
    res->dest_uid_high = uid_data[2];
    res->dest_uid_low = uid_data[3];

    /* Get socket entry and extract flags */
    sock_entry = (uint8_t *)(*(uint32_t *)((uint8_t *)&SOCK_$TABLE_BASE + sock_num * 4));
    sock_flags = sock_entry[0x15];

    /* Set flags in result (bits 15-22) */
    res->flags = (res->flags & 0x807F) | ((uint16_t)sock_flags << 15);

    /* Copy routing key and socket info */
    res->routing_key = local_routing_key;
    res->sock_num = local_sock;

    /* Check if this is a "raw" packet (flag bit 1 set) */
    if ((local_flags & 0x02) != 0) {
        /* Raw packet handling */
        int8_t more_flag = (local_flags & 0x04) ? -1 : 0;

        /* Set high bit of flags based on more_flag */
        res->flags = (res->flags & 0x7F) | ((more_flag & 0x80));

        /* Set pointers directly into packet */
        res->hdr_ptr = local_pkt + 0x1E;
        res->data_ptr = local_pkt + 0x36;

        /* Extract source/dest node info */
        res->src_node = *(uint32_t *)(local_pkt + 0x06);
        res->dest_node = *(uint32_t *)(local_pkt + 0x12);

        /* Set protocol info */
        res->info = (res->info & 0x807F) | 0x0080;

        /* If dest_node is 0, look up from route */
        if (res->dest_node == 0) {
            uint32_t page_base = (uint32_t)local_pkt & 0xFFFFFC00;
            uint16_t port_type = *(uint16_t *)(page_base + 0x3E0);
            uint32_t port_id = *(uint16_t *)(page_base + 0x3E2);
            uint32_t *port_ptr = (uint32_t *)ROUTE_$FIND_PORTP(port_type, port_id);
            res->dest_node = *port_ptr;
        }

        return;
    }

    /* Non-raw packet handling */
    pkt_ptr = local_pkt;

    /* Set high bit of flags based on packet flag */
    {
        uint8_t pkt_flag = local_pkt[7];
        int8_t bit_set = (pkt_flag & 0x08) ? -1 : 0;
        res->flags = (res->flags & 0x7F) | ((bit_set & 0x80));
    }

    /* Calculate header offset */
    hdr_offset = (uint8_t)local_pkt[0x18] + 0x1E;

    /* Get data length from packet */
    data_len = *(int16_t *)(local_pkt + 0x12);

    /* Set data pointer */
    res->data_ptr = local_pkt + hdr_offset;

    /* Calculate total packet size */
    total_size = (data_len + hdr_offset + 3) & ~0x03;  /* Align to 4 bytes */

    /* Check if packet is too large for inline processing */
    if (total_size + 0x18 >= APP_MAX_INLINE_SIZE) {
        /* Need to use temp buffer - acquire lock */
        ML_$EXCLUSION_START(&APP_$EXCLUSION_LOCK);
        holding_lock = -1;

        /* Copy header portion to temp buffer */
        OS_$DATA_COPY(local_pkt, APP_$TEMP_BUFFER, hdr_offset);

        /* Update header pointer to use original packet location */
        res->hdr_ptr = local_pkt;
        pkt_ptr = APP_$TEMP_BUFFER;
    } else {
        /* Inline processing */
        res->hdr_ptr = local_pkt + total_size;
    }

    /* Build output header */
    out_hdr = (app_pkt_hdr_t *)res->hdr_ptr;
    out_hdr->type = 0x0118;
    out_hdr->flags = pkt_ptr[0x0E];

    /* Set protocol info in result */
    net_type = (uint8_t)pkt_ptr[0x0C];
    res->info = (res->info & 0x807F) | (net_type << 7);

    /* Copy lengths */
    out_hdr->protocol = *(uint16_t *)(pkt_ptr + 0x16);
    out_hdr->template_len = *(uint16_t *)(pkt_ptr + 0x12);

    /* Handle based on network type */
    if (net_type == APP_NET_TYPE_LOCAL) {
        /* Local network (same node) */
        res->src_node = 0;
        res->dest_node = 0;

        /* Source is local node */
        out_hdr->src_node = NODE_$ME;

        /* Get source socket from variable offset */
        {
            uint8_t offset_idx = (uint8_t)pkt_ptr[0x19];
            out_hdr->src_sock = *(uint16_t *)(pkt_ptr + 0x1E + offset_idx * 2);
        }

        /* Get dest socket/node based on address size */
        if ((uint8_t)pkt_ptr[0x18] == 4) {
            out_hdr->dest_sock = *(uint16_t *)(pkt_ptr + 0x1A);
            out_hdr->dest_node = *(uint32_t *)(pkt_ptr + 0x08);
        } else {
            out_hdr->dest_sock = *(uint16_t *)(pkt_ptr + 0x1E);
            out_hdr->dest_node = *(uint32_t *)(pkt_ptr + 0x20);
        }
    } else if (net_type == APP_NET_TYPE_REMOTE) {
        /* Remote network */
        res->src_node = *(uint32_t *)(pkt_ptr + 0x2E);
        res->dest_node = *(uint32_t *)(pkt_ptr + 0x3A);

        /* Extract 24-bit node IDs */
        out_hdr->src_node = *(uint32_t *)(pkt_ptr + 0x34) & 0x00FFFFFF;
        out_hdr->src_sock = *(uint16_t *)(pkt_ptr + 0x38);
        out_hdr->dest_node = *(uint32_t *)(pkt_ptr + 0x40) & 0x00FFFFFF;
        out_hdr->dest_sock = *(uint16_t *)(pkt_ptr + 0x44);

        /* Check address size indicator */
        if ((uint8_t)pkt_ptr[0x2D] == 4) {
            out_hdr->addr_size = pkt_ptr[0x4B];
            out_hdr->net_type = 2;

            /* Special handling for address size 0x29 (')') */
            if (out_hdr->addr_size == APP_ADDR_SIZE_REMOTE) {
                /* Adjust data pointer backwards */
                res->data_ptr = (uint8_t *)res->data_ptr - 0x10;
                out_hdr->data_len = out_hdr->data_len + 0x10;
            }
        } else {
            out_hdr->net_type = 1;
            out_hdr->addr_size = pkt_ptr[0x2D];
        }
    }

    /* Release lock if we were holding it */
    if (holding_lock < 0) {
        ML_$EXCLUSION_STOP(&APP_$EXCLUSION_LOCK);
    }
}
