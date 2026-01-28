/*
 * NETLOG_$SEND_PAGE - Send a filled log page
 *
 * Sends the completed log page to the logging server.
 * This function is called internally when a buffer fills up
 * or during shutdown to send any remaining entries.
 *
 * The function:
 *   1. Captures page metadata (done count, entry count)
 *   2. Gets a network header buffer
 *   3. Builds an internet packet header
 *   4. Sends the packet via NET_IO_$SEND
 *   5. Returns the header buffer
 *
 * Original address: 0x00E71C78
 */

#include "netlog/netlog_internal.h"
#include "network/network.h"
#include "pkt/pkt.h"
#include "net_io/net_io.h"

/*
 * AUDIT data end address for packet info
 * Used in PKT_$BLD_INTERNET_HDR call as the packet template
 */
#if defined(ARCH_M68K)
    #define AUDIT_PKT_INFO      ((void*)0xE248FC)
#else
    extern char AUDIT_PKT_INFO_SYM;
    #define AUDIT_PKT_INFO      (&AUDIT_PKT_INFO_SYM)
#endif

void NETLOG_$SEND_PAGE(void)
{
    netlog_data_t *nl = NETLOG_DATA;

    /*
     * Local variables for network header building
     * These match the stack layout in the original function
     */
    uint32_t data_len;              /* -0x20: Data length for send */
    int16_t port;                   /* -0x1A: Port number */
    uint16_t pkt_len;               /* -0x18: Packet length */
    uint16_t extra1;                /* -0x16: Extra param from BLD_INTERNET_HDR */
    uint16_t extra2[2];             /* -0x14: More extra params */
    uint32_t hdr_va;                /* -0x10: Header virtual address */
    status_$t status;               /* -0x0C: Status return */
    uint32_t hdr_pa;                /* -0x08: Header physical address */
    uint8_t send_extra[4];          /* -0x04: Extra data for NET_IO_$SEND */

    /*
     * Capture metadata for the page being sent
     * Copy done_cnt and entry count to packet header template
     */
    nl->pkt_done_cnt = nl->done_cnt;
    nl->pkt_entry_cnt = nl->page_counts[nl->send_page_index];

    /*
     * Get a network header buffer
     * This may allocate from wired memory (loopback) or use shared header
     */
    NETWORK_$GETHDR(&NETLOG_$NODE, &hdr_va, &hdr_pa);

    /*
     * Build the internet packet header if sending is enabled
     */
    if (nl->ok_to_send < 0) {
        PKT_$BLD_INTERNET_HDR(
            0,                          /* param1: flags */
            NETLOG_$NODE,               /* dest_node */
            NETLOG_$SOCK,               /* dest_sock */
            (int32_t)-1,                /* src_node_or: -1 = use default */
            NODE_$ME,                   /* src_node */
            NETLOG_$SOCK,               /* src_sock */
            AUDIT_PKT_INFO,             /* pkt_info template */
            0,                          /* param8 */
            &nl->pkt_type1,             /* template data (type1, type2, done_cnt, entry_cnt) */
            10,                         /* hdr_len: template size */
            NETLOG_PROTOCOL,            /* protocol: 0x3F6 */
            &port,                      /* port_out */
            (uint32_t *)hdr_va,         /* hdr_buf */
            &pkt_len,                   /* len_out */
            &extra1,                    /* param15 */
            extra2,                     /* param16 */
            &status                     /* status_ret */
        );
    }

    /*
     * Initialize extra field for send (unused)
     */
    extra1 = 0;

    /*
     * Send the packet if enabled and header build succeeded
     */
    if (nl->ok_to_send < 0 && status == status_$ok) {
        /*
         * Calculate data length: buffer_ppn << 10 gives size in bytes
         * The original shifts left by 10 (multiply by 1024)
         */
        data_len = nl->buffer_ppn[nl->send_page_index - 1] << 10;

        NET_IO_$SEND(
            port,                                               /* port */
            &hdr_va,                                            /* hdr_ptr */
            hdr_pa,                                             /* hdr_pa */
            pkt_len,                                            /* hdr_len */
            nl->buffer_va[nl->send_page_index],                 /* data_va */
            &data_len,                                          /* data_len */
            NETLOG_PROTOCOL,                                    /* protocol: 0x3F6 */
            0,                                                  /* flags */
            send_extra,                                         /* extra */
            &status                                             /* status_ret */
        );
    }

    /*
     * Return the network header buffer
     */
    NETWORK_$RTNHDR(&hdr_va);
}
