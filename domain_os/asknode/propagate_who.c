/*
 * ASKNODE_$PROPAGATE_WHO - Propagate WHO response to network
 *
 * Sends a WHO response packet to propagate node information across
 * the network. Used for network topology discovery and WHO query
 * forwarding.
 *
 * If the response has already been marked as done (ASKNODE_DONE_MARKER),
 * this function returns immediately without sending.
 *
 * Original address: 0x00E65E8E
 * Size: 234 bytes
 *
 * Assembly analysis:
 *   The function builds a WHO response packet and sends it via
 *   PKT_$SEND_INTERNET. If the response type is 0x46, it sends
 *   a simplified response. Otherwise, it forwards the full response.
 */

#include "asknode/asknode_internal.h"

void ASKNODE_$PROPAGATE_WHO(int16_t *response, uint32_t *routing_info)
{
    uint32_t pkt_info[8];
    uint8_t temp1[2], temp2[4];
    status_$t status[3];

    /*
     * Check if already propagated (marked with ASKNODE_DONE_MARKER).
     * The marker value 0xDEAF (-0x2151) indicates propagation is complete.
     */
    if (*response == (int16_t)ASKNODE_DONE_MARKER) {
        return;
    }

    /* Copy packet info block from global template */
    {
        uint32_t *src = PKT_$DEFAULT_INFO;
        uint32_t *dst = pkt_info;
        int i;
        for (i = 0; i < 7; i++) *dst++ = *src++;
        *(uint16_t *)dst = *(uint16_t *)src;
    }

    /* Set version and flags */
    pkt_info[1] = 0x100BE;  /* Magic constant */
    *(uint16_t *)((char *)pkt_info + 8) = 0;  /* Clear flags */

    /* Check response type */
    if (response[1] == 0x46) {
        /*
         * Time sync response (0x46)
         * Send a simplified 8-byte response packet
         */
        int16_t simple_response[4];
        simple_response[0] = 3;     /* Version */
        simple_response[1] = 0x46;  /* Response type */
        simple_response[2] = 0;
        simple_response[3] = 0;

        *(uint16_t *)pkt_info = 0x20;  /* Packet length */

        PKT_$SEND_INTERNET(
            *(uint32_t *)(response + 6),   /* dest routing from response */
            *(uint32_t *)(response + 4),   /* dest node from response */
            response[0xD],                  /* dest sock */
            *routing_info,                  /* src routing */
            NODE_$ME,                       /* src node */
            4,                              /* src sock */
            pkt_info,
            response[0xC],                  /* request ID */
            simple_response,
            8,                              /* response length */
            &DAT_00e658cc,                  /* no data */
            0,
            temp1,
            temp2,
            status
        );
    } else {
        /*
         * Standard WHO response
         * Forward the full response packet (0x22 bytes)
         */
        *(uint16_t *)pkt_info = 0xB0;  /* Packet length */

        PKT_$SEND_INTERNET(
            *routing_info,                  /* dest routing */
            2,                              /* broadcast dest node */
            4,                              /* dest sock */
            *routing_info,                  /* src routing */
            NODE_$ME,                       /* src node */
            response[0xD],                  /* src sock */
            pkt_info,
            response[0xC],                  /* request ID */
            response,                       /* forward full response */
            0x22,                           /* response length */
            &DAT_00e658cc,                  /* no data */
            0,
            temp1,
            temp2,
            status
        );
    }

    /* Mark response as propagated */
    *response = (int16_t)ASKNODE_DONE_MARKER;
}
