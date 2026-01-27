/*
 * XNS Error Protocol Send Function
 *
 * Implementation of XNS_ERROR_$SEND for sending XNS Error Protocol packets.
 * The XNS Error Protocol is used to report undeliverable packets back to
 * the sender.
 *
 * Original address: 0x00E17A2E
 */

#include "xns/xns_internal.h"

/* Internal state for error socket */
int32_t XNS_ERROR_$STD_IDP_CHANNEL = 0;

/* Static data for error sending (at 0xE2B29C) */
static struct {
    uint8_t _reserved[0x18];
    int32_t header_len;         /* +0x18 */
    void *header_ptr;           /* +0x1C */
    void *iov;                  /* +0x20 */
    uint8_t flags;              /* +0x24 */
} xns_error_send_params;

/*
 * Static helper: xns_$maybe_open_error_socket
 *
 * Opens the error protocol socket if not already open.
 *
 * Original address: 0x00E178AA
 */
static void xns_$maybe_open_error_socket(status_$t *status_ret)
{
    if (XNS_ERROR_$STD_IDP_CHANNEL == 0) {
        struct {
            int16_t socket;
            int16_t channel_ret;
            code_ptr_t demux_callback;
            void *user_data;
        } open_opt;

        open_opt.socket = XNS_SOCKET_ERROR;
        open_opt.demux_callback = NULL;
        open_opt.user_data = NULL;

        XNS_IDP_$OS_OPEN(&open_opt, status_ret);
        if (*status_ret == status_$ok) {
            XNS_ERROR_$STD_IDP_CHANNEL = open_opt.channel_ret;
        }
    } else {
        *status_ret = status_$ok;
    }
}

/*
 * Static helper: xns_$maybe_close_error_socket
 *
 * Closes the error protocol socket if open.
 *
 * Original address: 0x00E17910
 */
static void xns_$maybe_close_error_socket(void)
{
    if (XNS_ERROR_$STD_IDP_CHANNEL != 0) {
        status_$t status;
        int16_t channel = XNS_ERROR_$STD_IDP_CHANNEL;
        XNS_IDP_$OS_CLOSE(&channel, &status);
        XNS_ERROR_$STD_IDP_CHANNEL = 0;
    }
}

/*
 * Static helper: xns_$copy_error_header
 *
 * Copies relevant header fields from the original packet to the error packet.
 *
 * Original address: 0x00E17876
 */
static uint8_t xns_$copy_error_header(void *packet_info)
{
    uint8_t *pkt = (uint8_t *)packet_info;
    /* Return packet type */
    return *(uint8_t *)(pkt + 0x2D);
}

/*
 * Static helper: xns_$setup_error_header
 *
 * Sets up the error packet header fields.
 *
 * Original address: 0x00E17960
 */
static void xns_$setup_error_header(void)
{
    /* Setup error header - minimal implementation */
}

/*
 * XNS_ERROR_$SEND - Send an XNS Error Protocol packet
 *
 * Sends an error response packet for a received packet that could
 * not be processed. Error packets contain:
 *   - The first 42 bytes of the original packet (IDP header + 12 data bytes)
 *   - Error code and parameter
 *
 * Error packet format (after IDP header):
 *   +0x1E: Error code (2 bytes)
 *   +0x20: Error parameter (2 bytes)
 *   +0x22: Original packet data (42 bytes minimum)
 *
 * @param packet_info   Original packet information structure:
 *                      +0x18: header length
 *                      +0x1C: header pointer
 * @param error_code    Pointer to error code
 * @param error_param   Pointer to error parameter
 * @param result_ret    Output: unused result
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E17A2E
 */
void XNS_ERROR_$SEND(void *packet_info, uint16_t *error_code, uint16_t *error_param,
                     uint16_t *result_ret, status_$t *status_ret)
{
    uint8_t *pkt = (uint8_t *)packet_info;
    int32_t header_len;
    int16_t *orig_header;
    int16_t *error_header;
    void *hdr_buf;
    int32_t netbuf_handle = 0;
    void *netbuf_ptr[2];
    uint8_t cleanup_buf[24];
    status_$t local_status;
    int16_t packet_offset;

    *result_ret = 0;
    *status_ret = status_$ok;

    /* Set up cleanup handler */
    local_status = FIM_$CLEANUP(cleanup_buf);
    if (local_status != status_$cleanup_handler_set) {
        /* Cleanup failed - return original packet if allocated */
        if (netbuf_handle != 0) {
            NETBUF_$RTN_HDR(netbuf_ptr);
        }
        *status_ret = local_status;
        return;
    }

    /* Validate original packet */
    header_len = *(int32_t *)(pkt + 0x18);
    orig_header = *(int16_t **)(pkt + 0x1C);

    if (header_len < XNS_IDP_HEADER_SIZE || orig_header == NULL) {
        *status_ret = status_$xns_invalid_param;
        return;
    }

    /* Check source address isn't broadcast */
    {
        int8_t src_bc = xns_$is_local_addr((uint8_t *)orig_header + 0x12);
        int8_t dest_bc = xns_$is_local_addr((uint8_t *)orig_header + 0x06);

        if ((src_bc | dest_bc) < 0) {
            *status_ret = status_$xns_invalid_param;
            return;
        }
    }

    /* Don't send error for error packets */
    if (*(uint8_t *)((uint8_t *)orig_header + 5) == XNS_IDP_TYPE_ERROR) {
        *status_ret = status_$xns_invalid_param;
        return;
    }

    /* Open error socket if needed */
    xns_$maybe_open_error_socket(&local_status);
    *status_ret = local_status;
    if (*status_ret != status_$ok) {
        return;
    }

    /* Get a network buffer for the error packet */
    NETBUF_$GET_HDR(&netbuf_handle, netbuf_ptr);
    error_header = (int16_t *)netbuf_ptr[0];

    /* Copy original header info */
    xns_$copy_error_header(packet_info);
    xns_$setup_error_header();

    /* Build error packet */
    packet_offset = 0x4C - *(int16_t *)(pkt + 0x36);

    xns_error_send_params.header_len = packet_offset;
    xns_error_send_params.header_ptr = netbuf_ptr[0];
    xns_error_send_params.iov = NULL;
    xns_error_send_params.flags = 0xFF;

    /* Set checksum to "compute" */
    error_header[0] = -1;

    /* Set length */
    error_header[1] = (uint16_t)xns_error_send_params.header_len;

    /* Set transport control and packet type */
    *(uint8_t *)((uint8_t *)error_header + 4) = 0;
    *(uint8_t *)((uint8_t *)error_header + 5) = XNS_IDP_TYPE_ERROR;

    /* Swap source and destination - destination becomes original source */
    *(uint32_t *)(error_header + 3) = *(uint32_t *)(orig_header + 0x1A / 2);
    *(uint32_t *)(error_header + 5) = *(uint32_t *)(orig_header + 0x1C / 2);
    *(uint32_t *)(error_header + 7) = *(uint32_t *)(orig_header + 0x1E / 2);

    /* Set source address - use error socket (port 3) */
    error_header[0x0E / 2] = XNS_SOCKET_ERROR;

    /* Clear source network/host (will be filled by send) */
    *(uint32_t *)(error_header + 9) = 0;
    error_header[0x0B] = 0x800;

    /* Extract local node info for source */
    {
        uint32_t node = NODE_$ME;
        uint16_t host_hi = ((node >> 16) & 0x0F) | 0x1E00;
        uint16_t host_lo = node & 0xFFFF;
        error_header[0x0C] = host_hi;
        error_header[0x0D] = host_lo;
    }

    /* Set error code and parameter */
    error_header[0x10 / 2] = *error_code;
    error_header[0x0F] = *error_param;

    /* Send the error packet */
    XNS_IDP_$OS_SEND((int16_t *)&XNS_ERROR_$STD_IDP_CHANNEL,
                     &xns_error_send_params, result_ret, status_ret);

    /* Close error socket */
    xns_$maybe_close_error_socket();

    /* Release cleanup handler */
    FIM_$RLS_CLEANUP(cleanup_buf);
}
