/*
 * XNS IDP Initialization
 *
 * Implementation of XNS_IDP_$INIT which initializes the IDP subsystem
 * at system startup.
 *
 * Original address: 0x00E30268
 */

#include "xns/xns_internal.h"

/*
 * XNS_IDP_$INIT - Initialize the XNS IDP subsystem
 *
 * This function is called during system startup to initialize the IDP
 * subsystem. It:
 *   1. Sets the initial dynamic socket number to 0xBB9 (3001)
 *   2. Initializes the exclusion lock
 *   3. Clears all channel state
 *   4. Sets up the local address from NODE_$ME
 *   5. Initializes port routing pointers
 *
 * Assembly analysis (0x00E30268):
 *   link.w A6,-0x1c
 *   movem.l {A2 D3 D2},-(SP)
 *   movea.l #0xe2b314,A0           ; A0 = XNS IDP base
 *   move.w #0xbb9,(0x536,A0)       ; next_socket = 0xBB9
 *   pea (0x520,A0)                 ; push lock address
 *   jsr ML_$EXCLUSION_INIT         ; initialize lock
 *   ...
 *   ; Clear all 16 channel entries
 *   moveq #0xf,D0                  ; D0 = 15 (loop counter)
 * chan_loop:
 *   bclr.b #0x7,(0xe4,A1)          ; clear active flag
 *   clr.l (0xa0,A1)                ; clear demux callback
 *   move.w #0xe1,(0xd6,A1)         ; user_socket = 0xE1 (none)
 *   clr.w (0xd8,A1)                ; xns_socket = 0
 *   andi.b #0x7,(0xda,A1)          ; clear flags except low 3 bits
 *   move.w #-0x1,(0xd4,A1)         ; connected_port = -1
 *   ; Clear per-port active flags
 *   moveq #0x7,D1                  ; D1 = 7
 * port_loop:
 *   clr.b (0xdc,A1,D2*1)           ; port_active[D2] = 0
 *   addq.w #1,D2
 *   dbf D1,port_loop
 *   lea (0x48,A0),A0               ; next channel
 *   dbf D0,chan_loop
 *   ...
 *   ; Set up local address from NODE_$ME
 *   move.w #0x800,(0x20,A0)        ; local_socket = 0x800
 *   move.l NODE_$ME,D3             ; D3 = node address
 *   clr.w D3w                      ; clear low word
 *   swap D3                        ; get high word
 *   andi.l #0xf,D3                 ; mask to 4 bits
 *   ori.w #0x1e00,D3w              ; set high bits
 *   move.w D3w,(0x22,A0)           ; local_host_hi
 *   move.w NODE_$ME+2,(0x24,A0)    ; local_host_lo
 *   ...
 *   ; Initialize port routing pointers
 *   moveq #0x7,D0                  ; 8 ports
 *   lea (A0),A2                    ; channel base
 *   movea.l #0xe26ee8,A1           ; ROUTE_$PORTP array
 * port_init:
 *   move.l (A1)+,(0x44,A0)         ; port_info = ROUTE_$PORTP[i]
 *   move.l #-0x10000,(0x48,A0)     ; mac_socket = 0xFFFF0000
 *   clr.l (0x40,A0)                ; port_ref = 0
 *   lea (0xc,A2),A2                ; next port entry
 *   dbf D0,port_init
 */
void XNS_IDP_$INIT(void)
{
    uint8_t *base = XNS_IDP_BASE;
    int16_t chan, port;

    /* Set initial dynamic socket number */
    *(uint16_t *)(base + XNS_OFF_NEXT_SOCKET) = XNS_FIRST_DYNAMIC_PORT;

    /* Initialize exclusion lock */
    ML_$EXCLUSION_INIT((ml_$exclusion_t *)(base + XNS_OFF_LOCK));

    /* Clear open channel count */
    *(uint16_t *)(base + XNS_OFF_OPEN_COUNT) = 0;

    /* Initialize all 16 channels */
    for (chan = 0; chan < XNS_MAX_CHANNELS; chan++) {
        uint8_t *chan_base = base + chan * XNS_CHANNEL_SIZE;

        /* Clear active flag (bit 7 of state) */
        *(uint8_t *)(chan_base + XNS_CHAN_OFF_STATE) &= 0x7F;

        /* Clear demux callback */
        *(uint32_t *)(chan_base + XNS_CHAN_OFF_DEMUX) = 0;

        /* Set user socket to "none" */
        *(uint16_t *)(chan_base + XNS_CHAN_OFF_USER_SOCKET) = XNS_NO_SOCKET;

        /* Clear XNS socket */
        *(uint16_t *)(chan_base + XNS_CHAN_OFF_XNS_SOCKET) = 0;

        /* Clear flags except low 3 bits */
        *(uint8_t *)(chan_base + XNS_CHAN_OFF_FLAGS) &= 0x07;

        /* Set connected port to "any" (-1) */
        *(int16_t *)(chan_base + XNS_CHAN_OFF_CONN_PORT) = -1;

        /* Clear per-port active flags */
        for (port = 0; port < XNS_MAX_PORTS; port++) {
            *(uint8_t *)(chan_base + XNS_CHAN_OFF_PORT_ACTIVE + port) = 0;
        }
    }

    /* Clear statistics */
    *(uint32_t *)(base + XNS_OFF_PACKETS_SENT) = 0;
    *(uint32_t *)(base + XNS_OFF_PACKETS_RECV) = 0;
    *(uint32_t *)(base + XNS_OFF_PACKETS_DROP) = 0;

    /* Clear registered address count */
    *(uint16_t *)(base + XNS_OFF_REG_COUNT) = 0;

    /* Set up local address from NODE_$ME */
    *(uint16_t *)(base + XNS_OFF_LOCAL_SOCKET) = 0x800;

    /* Extract network portion from node address:
     * NODE_$ME contains 4 bytes. We want bits [31:16] masked to low 4 bits,
     * OR'd with 0x1E00 */
    {
        uint32_t node = NODE_$ME;
        uint16_t host_hi = ((node >> 16) & 0x0F) | 0x1E00;
        *(uint16_t *)(base + XNS_OFF_LOCAL_HOST_HI) = host_hi;
        *(uint16_t *)(base + XNS_OFF_LOCAL_HOST_LO) = NODE_$ME_LO;
    }

    /* Initialize first channel's broadcast address */
    *(uint16_t *)(base + 0x10) = 0xFFFF;  /* broadcast network */

    /* Initialize port routing pointers */
    for (port = 0; port < XNS_MAX_PORTS; port++) {
        uint8_t *port_base = base + port * XNS_PORT_STATE_SIZE;

        /* Set port_info pointer from ROUTE_$PORTP array */
        *(uint32_t *)(port_base + XNS_PORT_OFF_INFO) = (uint32_t)ROUTE_$PORTP[port];

        /* Set MAC socket to invalid (0xFFFF in high word) */
        *(uint32_t *)(port_base + XNS_PORT_OFF_MAC_SOCKET) = 0xFFFF0000;

        /* Clear port reference */
        *(uint32_t *)(port_base + XNS_PORT_OFF_REF) = 0;
    }
}
