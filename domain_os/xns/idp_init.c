/*
 * XNS IDP Initialization
 *
 * Implementation of XNS_IDP_$INIT which initializes the IDP subsystem
 * at system startup.
 *
 * Original address: 0x00E30268
 *
 * Module data through XNS_IDP_$DATA: Claude Opus 5.5 (source-iq58).
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
    xns_$idp_data_t *d = &XNS_IDP_$DATA;    /* movea.l #0xe2b314,A0 */
    int16_t chan, port;

    /* Set initial dynamic socket number: move.w #0xbb9,(0x536,A0) */
    d->next_socket = XNS_FIRST_DYNAMIC_PORT;

    /* Initialize exclusion lock: pea (0x520,A0) */
    ML_$EXCLUSION_INIT(&d->lock);

    /* Clear open channel count: clr.w (0x534,A0) */
    d->open_channels = 0;

    /* Initialize all 16 channels */
    for (chan = 0; chan < XNS_MAX_CHANNELS; chan++) {
        xns_$channel_t *c = &d->channels[chan];

        /* Clear active flag: bclr.b #0x7,(0xe4,A1) - bit 15 of the word */
        c->state &= (int16_t)~0x8000;

        /* Clear demux callback: clr.l (0xa0,A1) */
        c->demux = NULL;

        /* Set user socket to "none": move.w #0xe1,(0xd6,A1) */
        c->user_socket = XNS_NO_SOCKET;

        /* Clear XNS socket: clr.w (0xd8,A1) */
        c->xns_socket = 0;

        /* andi.b #0x7,(0xda,A1): the HIGH byte of the flags word */
        c->flags &= 0x07FF;

        /* Set connected port to "any": move.w #-0x1,(0xd4,A1) */
        c->connected_port = -1;

        /* Clear per-port active flags: clr.b (0xdc,A2) */
        for (port = 0; port < XNS_MAX_PORTS; port++) {
            c->port_active[port] = 0;
        }
    }

    /* Clear statistics: clr.l (A0) / (0x4,A0) / (0x8,A0) */
    d->packets_sent = 0;
    d->packets_received = 0;
    d->packets_dropped = 0;

    /* Clear registered address count: clr.w (0x538,A0) */
    d->registered_count = 0;

    /* Address entry 0, the node's own: move.w #0x800,(0x20,A0) */
    d->addrs[0][0] = 0x800;

    /* Extract network portion from node address:
     * NODE_$ME contains 4 bytes. We want bits [31:16] masked to low 4 bits,
     * OR'd with 0x1E00 */
    {
        uint32_t node = NODE_$ME;
        uint16_t host_hi = ((node >> 16) & 0x0F) | 0x1E00;
        uint16_t host_lo = node & 0xFFFF;
        d->addrs[0][1] = host_hi;               /* (0x22,A0) */
        d->addrs[0][2] = host_lo;               /* (0x24,A0) */
    }

    /* Entry 0 belongs to every port: move.w #-0x1,(0x10,A0) */
    d->addr_port[0] = -1;

    /* Initialize the port table (0x00E30312-0x00E30332) */
    for (port = 0; port < XNS_MAX_PORTS; port++) {
        xns_$port_state_t *p = &d->ports[port];

        /* move.l (A1)+,(0x44,A0): the ROUTE_$PORTP entry, a VA */
        p->net_addr_ptr = ARCH_PTR_TO_VA(ROUTE_$PORTP[port]);

        /* move.l #-0x10000,(0x48,A0): mac_socket = 0xFFFF, refcount = 0 */
        p->mac_socket = 0xFFFF;
        p->refcount = 0;

        /* Clear port reference: clr.l (0x40,A0) */
        p->mac_handle = 0;
    }
}
