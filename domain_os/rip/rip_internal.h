/*
 * RIP - Routing Information Protocol Internal Definitions
 *
 * Internal data structures and functions used only within the RIP subsystem.
 * External users should use rip.h instead.
 */

#ifndef RIP_INTERNAL_H
#define RIP_INTERNAL_H

#include "app/app.h"   /* app_$reply_hdr_t, app_$receive_rec_t */
#include "rip/rip.h"
#include "ml/ml.h"
#include "proc1/proc1.h"
#include "route/route.h"
#include "network/network.h"
#include "time/time.h"

/*
 * =============================================================================
 * Constants
 * =============================================================================
 */

/* RIP_TABLE_SIZE, RIP_TABLE_MASK, RIP_ROUTE_TIMEOUT, the RIP_STATE_* codes,
 * RIP_INFINITY, RIP_ROUTES_PER_ENTRY and RIP_LOCK_PRIORITY: moved to
 * rip/rip.h (ASKNODE_$INTERNET_INFO needs the table size and the state
 * field layout). */

/*
 * =============================================================================
 * Data Structures
 * =============================================================================
 */

/*
 * XNS network address (10 bytes): rip_$xns_addr_t now lives in rip/rip.h
 * because ROUTE_$SERVICE / ROUTE_$CLOSE_PORT / NETWORK_$FETCH_DISKLESS_INFO
 * build one to pass to RIP_$UPDATE_D / RIP_$UPDATE_INT.
 */

/* rip_$route_t / rip_$entry_t: moved to rip/rip.h. */

/*
 * =============================================================================
 * Global Data (m68k addresses)
 * =============================================================================
 */

/*
 * RIP_$WIRED_DATA (the old RIP_$DATA, RIP_$STATS, RIP_$INFO and the recent-
 * change flags), RIP_$INIT_DATA and RIP_$RTWIRED_DATA: rip/rip.h.
 */

/*
 * rip_$no_data - the all-zero longword at 0x00E68E28, in the code region
 * right after RIP_$SERVER's last instruction and before RIP_$TABLE_D
 * (0x00E68E2C); `gsk read 0xE68E28 4` gives 00 00 00 00.  Both routines
 * that send a RIP packet with no data pass it as PKT_$SEND_INTERNET's data
 * pointer with a length of 0: RIP_$SERVER ("pea (0x1de,PC)" at 0x00E68C48)
 * and RIP_$ANNOUNCE_NS ("pea (-0x35a,PC)" at 0x00E69180).  One cell shared by two files, so it
 * is defined once, in rip/server.c, and never read (the length is 0).
 */
extern const uint32_t rip_$no_data;

/*
 * status_$internet_unknown_network_port: route/route.h
 * status_$network_too_many_networks_in_internet: network/network.h
 */

/*
 * =============================================================================
 * Internal Function Prototypes
 * =============================================================================
 */

/*
 * RIP_$LOCK - Acquire RIP subsystem lock
 *
 * Raises process priority and acquires the RIP exclusion lock.
 * Must be paired with RIP_$UNLOCK.
 *
 * Original address: 0x00E154A4
 */
void RIP_$LOCK(void);

/*
 * RIP_$UNLOCK - Release RIP subsystem lock
 *
 * Releases the RIP exclusion lock and restores process priority.
 *
 * Original address: 0x00E154C4
 */
void RIP_$UNLOCK(void);

/*
 * RIP_$AGE - Age routing table entries
 *
 * Scans all routing table entries and ages them:
 * - VALID routes past expiration become AGING
 * - AGING routes past expiration become EXPIRED (metric set to infinity)
 * - EXPIRED routes are cleared (UNUSED)
 *
 * After aging, calls RIP_$SEND_UPDATES to propagate changes.
 *
 * Original address: 0x00E155C0
 */
void RIP_$AGE(void);

/* RIP_$SEND_UPDATES (0x00E6887A) is declared in rip/rip.h (ROUTE_$SERVICE calls it). */

/* RIP_$UPDATE_INT (0x00E15922) is declared in rip/rip.h
 * (NETWORK_$FETCH_DISKLESS_INFO calls it). */

/*
 * Helper functions (nested Pascal procedures in original):
 *
 * RIP_$COMPARE_SOURCE (0x00E15830):
 *   Compares route source addresses. For non-standard routes, compares
 *   full 6-byte host address. For standard routes, compares lower 20 bits.
 *   Returns 0xFF (-1) if same source, 0 otherwise.
 *
 * RIP_$APPLY_UPDATE (0x00E15888):
 *   Applies update to a route entry. Handles route withdrawal (transition
 *   to AGING with short timeout) vs normal update (copy source, set VALID).
 *   Also sets recent_changes flag if metric changed.
 */

/* Route aging timeout (short) - used when route is being invalidated */
#define RIP_AGING_TIMEOUT       0x28

/*
 * RIP_$PACKET_LENGTH - Calculate RIP packet data length
 *
 * Returns the packet data length for a given number of route entries.
 * Each entry is 6 bytes (4 byte network + 2 byte metric) plus 2 bytes
 * for the command field.
 *
 * @param entry_count   Number of route entries
 * @return              Packet data length (entry_count * 6 + 2)
 *
 * Original address: 0x00E68864
 */
int16_t RIP_$PACKET_LENGTH(int16_t entry_count);

/*
 * RIP_$PROCESS_REQUEST (0x00E688C8) is a nested Pascal procedure of
 * RIP_$SERVER - "move.l (A6),D6" at 0x00E688D4 takes the parent's frame
 * pointer out of the static link and every buffer it touches (entry_count,
 * response_count, payload_va and the response packet) is a field of that
 * frame.  It is therefore a static inside rip/server.c taking a
 * rip_$server_frame_t *, not a global with an invented ABI.
 */

/*
 * RIP_$SERVER - Main RIP protocol server
 *
 * Processes incoming RIP packets from socket 8. Handles:
 * - Request (cmd=1): Send routing information for requested networks
 * - Response (cmd=2): Update routing table with received routes
 * - Name Register (cmd=3): Apollo extension for name service registration
 *
 * Called from socket receive processing when RIP packets arrive.
 *
 * The original is a procedure: it leaves nothing in D0 and its only caller
 * (0x00E11BA8) neither reserves a result slot nor reads one.
 *
 * Original address: 0x00E68A08
 */
void RIP_$SERVER(void);

/*
 * =============================================================================
 * Server Structures
 * =============================================================================
 */

/* RIP command types */
#define RIP_CMD_REQUEST         1
#define RIP_CMD_RESPONSE        2
#define RIP_CMD_NAME_REGISTER   3

/* Maximum entries per RIP packet */
#define RIP_MAX_ENTRIES         0x5A    /* 90 entries */

/* RIP packet entry size */
#define RIP_ENTRY_SIZE          6

/* RIP socket number */
#define RIP_SOCKET              8

/*
 * Table access types and functions are in rip.h (public API)
 * - rip_$table_d_buf_t
 * - rip_$table_buf_t
 * - RIP_$TABLE_D()
 * - RIP_$TABLE()
 */

/*
 * =============================================================================
 * Send/Broadcast Functions
 * =============================================================================
 */

/*
 * RIP_$SEND's two nested Pascal procedures - RIP_$SEND_TO_PORT (0x00E870DC,
 * XNS/IDP) and RIP_$SEND_TO_PORT_INTERNET (0x00E87000, Domain internet) -
 * are statics inside rip/send.c.  Each takes a single word parameter (the
 * port index) and reaches the rest of its inputs through the static link
 * "movea.l (A6),A2"; RIP_$SEND calls them with
 * "subq.l #2,SP / move.w Dn,-(SP) / bsr" at 0x00E8724A, 0x00E87262,
 * 0x00E8727C and 0x00E8728A.
 *
 * 0x00E87000 also carries the label RTWIRED_PROC_START; that is the start of
 * the wired routing region, not the procedure's name.
 */

/*
 * RIP_$SEND - Main RIP send function
 *
 * Sends a RIP packet to one or all ports. Dispatches to either
 * RIP_$SEND_TO_PORT (for XNS/IDP networks) or RTWIRED_PROC_START
 * (for wired/local networks) based on port flags.
 *
 * @param addr_info     Source address info (12 bytes: network + host + socket)
 * @param port_index    Port index (0-7), or -1 for all ports (broadcast)
 * @param route_data    Route data buffer (cmd + entries)
 * @param route_len     Route data length
 * @param flags         Pascal boolean read as a byte at (0x14,A6)
 *                      ("move.b (0x14,A6),D3b" at 0x00E871C8):
 *                      If < 0: non-standard routes, send via IDP only
 *                      If >= 0: standard routes, get new packet ID first
 *
 * Original address: 0x00E871B6
 */
void RIP_$SEND(void *addr_info, int16_t port_index, void *route_data,
               uint16_t route_len, boolean flags);

/*
 * RIP_$BROADCAST (0x00E87298) is declared in rip/rip.h - it iterates every
 * routing table entry, builds a RIP response packet and sends it to all
 * ports; ROUTE_$PROCESS drives it from its periodic timer.
 */

/*
 * =============================================================================
 * Port Management Functions
 * =============================================================================
 */

/*
 * Structure for IDP packet header (used by RIP_$STD_DEMUX)
 *
 * This represents the layout of an incoming IDP packet as seen by
 * the demultiplexer. Offsets are relative to the packet structure base.
 */
typedef struct idp_$packet_t {
    uint8_t     _reserved0[0x1A];   /* 0x00: Unknown header fields */
    uint16_t    checksum;           /* 0x1A: Packet checksum or length field */
    uint32_t    src_network;        /* 0x1C: Source network address */
    uint8_t     _reserved1[0x06];   /* 0x20: Unknown fields */
    uint32_t    dest_network;       /* 0x26: Destination network address */
    uint16_t    dest_socket;        /* 0x2A: Destination socket */
    uint16_t    pkt_length;         /* 0x2C: Packet data length */
    uint8_t     _reserved2[0x08];   /* 0x2E: Unknown fields */
    uint16_t    rip_length;         /* 0x36: RIP data length */
    uint8_t     rip_data[16];       /* 0x38: RIP packet data (variable) */
} idp_$packet_t;

/*
 * RIP_$STD_OPEN - Open standard RIP IDP channel
 *
 * Opens an XNS/IDP channel for receiving RIP packets on the standard
 * routing port. The channel uses RIP_$STD_DEMUX as its packet demultiplexer.
 *
 * On success, the channel number is stored in RIP_$STD_IDP_CHANNEL.
 *
 * Original address: 0x00E15AAE
 */
void RIP_$STD_OPEN(void);

/*
 * RIP_$STD_DEMUX - Demultiplex incoming RIP packets
 *
 * Demultiplexer callback invoked by XNS_IDP when a packet arrives on
 * the RIP channel. Extracts relevant information from the IDP packet
 * and queues it for the RIP server via SOCK_$PUT on socket 8.
 *
 * Parameters:
 * @param pkt           Pointer to IDP packet structure
 * @param param_2       Pointer to event count parameter 1
 * @param param_3       Pointer to event count parameter 2
 * @param param_4       Unused
 * @param status_ret    Output: status code (0x3B0016 on success)
 *
 * Original address: 0x00E15A2C
 */
void RIP_$STD_DEMUX(idp_$packet_t *pkt, uint16_t *param_2, uint16_t *param_3,
                    void *param_4, status_$t *status_ret);

/* RIP_$STD_IDP_CHANNEL: see rip/rip.h */

/*
 * The reply header APP_$RECEIVE hands back at app_$receive_rec_t.reply
 * (`movea.l (-0x30,A6),A0` at 0x00E2FD1A) is the shared eight-byte
 * app_$reply_hdr_t (app/app.h).  RIP_$INIT reads two of its four words:
 *   prefix.data_len    `move.w (0x4,A0),(-0x76,A6)`  0x00E2FD1E
 *   prefix.request_id  `move.w (0x6,A0),D3w`         0x00E2FD28
 * (source-ca0z)
 */

#endif /* RIP_INTERNAL_H */
