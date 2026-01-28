/*
 * RTWIRED_PROC_START - Send RIP packet to wired/local port via NET_IO_$SEND
 *
 * This function sends routing information protocol packets to directly
 * connected (wired) networks, as opposed to RIP_$SEND_TO_PORT which sends
 * via XNS/IDP.
 *
 * In the original Pascal implementation, this was a nested procedure within
 * RIP_$SEND that accessed the parent's stack frame for packet data. In this
 * C implementation, all necessary data is passed explicitly.
 *
 * The function:
 * 1. Allocates a network header buffer via NETWORK_$GETHDR
 * 2. Builds an internet header via PKT_$BLD_INTERNET_HDR
 * 3. Sends the packet via NET_IO_$SEND
 * 4. Returns the header buffer via NETWORK_$RTNHDR
 * 5. Advances the port's event counter if the port is active
 *
 * Original address: 0x00E87000
 */

#include "route/route_internal.h"
#include "pkt/pkt.h"
#include "ec/ec.h"
#include "net_io/net_io.h"
#include "network/network.h"

/*
 * =============================================================================
 * Global Data References
 * =============================================================================
 */

#if defined(ARCH_M68K)
    /* This node's ID */
    #define NODE_$ME                (*(uint32_t *)0xE245A4)

    /* RIP broadcast control parameters (30 bytes) */
    #define RIP_$BCAST_CONTROL      ((void *)0xE26EC0)

    /* Port array base - each entry is 0x5C (92) bytes */
    #define ROUTE_$PORT_BASE        ((uint8_t *)0xE2E0A0)

    /* Send callback/data pointer (4 bytes of zeros) */
    #define RTWIRED_$CALLBACK       ((void *)0xE870D8)

    /* Global flags at 0xE87D74 (accessed as word at A5+0xC where A5=0xE87D68) */
    #define RTWIRED_$SEND_FLAGS     (*(uint16_t *)0xE87D74)
#else
    extern uint32_t NODE_$ME;
    extern uint8_t RIP_$BCAST_CONTROL[30];
    extern uint8_t *ROUTE_$PORT_BASE;
    extern uint32_t RTWIRED_$CALLBACK_DATA;
    #define RTWIRED_$CALLBACK       (&RTWIRED_$CALLBACK_DATA)
    extern uint16_t RTWIRED_$SEND_FLAGS;
#endif

/* Port structure offsets */
#define PORT_ENTRY_SIZE         0x5C    /* 92 bytes per port */
#define PORT_NETWORK_OFF        0x00    /* Network address at offset 0 */
#define PORT_STATE_OFF          0x2E    /* Port state at offset 0x2E */
#define PORT_EVENTCOUNT_OFF     0x38    /* Event counter at offset 0x38 */

/* Port state values */
#define PORT_STATE_ACTIVE       2

/*
 * =============================================================================
 * RTWIRED_PROC_START
 * =============================================================================
 *
 * Send a RIP packet to a wired/local port using NET_IO_$SEND.
 *
 * This function builds an internet header and sends the packet directly
 * to the specified port, bypassing the IDP routing layer used by
 * RIP_$SEND_TO_PORT.
 *
 * @param port_index    Port index (0-7)
 * @param packet_id     Packet identifier (from PKT_$NEXT_ID)
 * @param route_data    Route data buffer (cmd + entries)
 * @param route_len     Route data length in bytes
 *
 * Original address: 0x00E87000
 *
 * Assembly structure:
 *   link.w  A6,-0x8           ; Small local frame
 *   movem.l {A3 A2 D2},-(SP)  ; Save registers
 *   move.w  (0x8,A6),D2w      ; D2 = port_index (param)
 *   movea.l (A6),A2           ; A2 = caller's frame pointer (for nested access)
 *
 * The function accesses parent's stack frame for:
 *   - packet_id at (caller_frame - 0x70)
 *   - route_data at (caller_frame + 0x0E)
 *   - route_len at (caller_frame + 0x12)
 *
 * Local variables allocated in parent's frame:
 *   - hdr_phys at (caller_frame - 0x64)
 *   - hdr_va at (caller_frame - 0x60)
 *   - Various PKT_$BLD_INTERNET_HDR outputs
 */
void RTWIRED_PROC_START(int16_t port_index, uint16_t packet_id,
                        void *route_data, uint16_t route_len)
{
    uint32_t hdr_phys;          /* Physical address of header buffer */
    uint32_t hdr_va;            /* Virtual address of header buffer */
    uint8_t *port_entry;        /* Pointer to port structure */
    uint32_t port_network;      /* Port's network address */
    status_$t status;           /* Operation status */

    /* PKT_$BLD_INTERNET_HDR output parameters */
    int16_t port_out;           /* Output port number (unused) */
    uint16_t hdr_len;           /* Header length */
    uint16_t param15;           /* Additional output */
    uint16_t param16;           /* Additional output */

    /* NET_IO_$SEND output */
    uint32_t send_extra;        /* Extra send parameter */

    /* Get port entry pointer */
    port_entry = ROUTE_$PORT_BASE + (port_index * PORT_ENTRY_SIZE);
    port_network = *(uint32_t *)(port_entry + PORT_NETWORK_OFF);

    /*
     * Allocate a network header buffer.
     * NETWORK_$GETHDR checks if this is a loopback (local) destination
     * and allocates appropriately.
     */
    NETWORK_$GETHDR(RTWIRED_$CALLBACK, &hdr_phys, &hdr_va);

    /*
     * Build the internet packet header.
     *
     * PKT_$BLD_INTERNET_HDR parameters:
     *   routing_key     = port_network (destination network)
     *   dest_node       = 0 (use routing)
     *   dest_sock       = 8 (RIP socket)
     *   src_node_or     = port_network (explicit source)
     *   src_node        = NODE_$ME (this node)
     *   src_sock        = 8 (RIP socket)
     *   pkt_info        = RIP_$BCAST_CONTROL
     *   request_id      = packet_id
     *   template        = route_data
     *   hdr_len         = route_len
     *   protocol        = 0
     *   port_out        = &port_out (output)
     *   hdr_buf         = &hdr_phys (header buffer pointer)
     *   len_out         = &hdr_len (output)
     *   param15         = &param15 (output)
     *   param16         = &param16 (output)
     *   status_ret      = &status (output)
     */
    PKT_$BLD_INTERNET_HDR(
        port_network,           /* routing_key */
        0,                      /* dest_node - use routing */
        8,                      /* dest_sock - RIP socket */
        port_network,           /* src_node_or - explicit source network */
        NODE_$ME,               /* src_node - this node */
        8,                      /* src_sock - RIP socket */
        RIP_$BCAST_CONTROL,     /* pkt_info - broadcast control */
        packet_id,              /* request_id */
        route_data,             /* template - route data */
        route_len,              /* hdr_len - route data length */
        0,                      /* protocol */
        &port_out,              /* port_out */
        &hdr_phys,              /* hdr_buf - use phys as buffer ptr */
        &hdr_len,               /* len_out */
        &param15,               /* param15 */
        &param16,               /* param16 */
        &status                 /* status_ret */
    );

    /*
     * If header build succeeded, send the packet.
     */
    if (status == status_$ok) {
        /*
         * NET_IO_$SEND parameters:
         *   port      = port_index
         *   hdr_ptr   = &hdr_phys (pointer to header buffer address)
         *   hdr_pa    = hdr_va (virtual/physical address from GETHDR)
         *   hdr_len   = hdr_len (from PKT_$BLD_INTERNET_HDR)
         *   data_va   = 0 (no additional data)
         *   data_len  = RTWIRED_$CALLBACK (pointer to 4 bytes of zeros)
         *   protocol  = 0
         *   flags     = RTWIRED_$SEND_FLAGS (global, usually 0)
         *   extra     = &send_extra
         *   status    = &status
         */
        NET_IO_$SEND(
            port_index,             /* port */
            &hdr_phys,              /* hdr_ptr */
            hdr_va,                 /* hdr_pa */
            hdr_len,                /* hdr_len */
            0,                      /* data_va - no extra data */
            (uint32_t *)RTWIRED_$CALLBACK,  /* data_len ptr (zeros) */
            0,                      /* protocol */
            RTWIRED_$SEND_FLAGS,    /* flags */
            &send_extra,            /* extra */
            &status                 /* status_ret */
        );
    }

    /*
     * Return the header buffer regardless of send status.
     */
    NETWORK_$RTNHDR(&hdr_phys);

    /*
     * If the port is active (state == 2), advance its event counter
     * to signal that a packet was sent.
     */
    if (*(uint16_t *)(port_entry + PORT_STATE_OFF) == PORT_STATE_ACTIVE) {
        EC_$ADVANCE((ec_$eventcount_t *)(port_entry + PORT_EVENTCOUNT_OFF));
    }
}
