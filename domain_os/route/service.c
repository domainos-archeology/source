/*
 * ROUTE_$SERVICE - Main routing service entry point
 *
 * Applies one routing-service request to one port.  The operation argument is
 * a 16-bit Pascal SET; every test in the image is "btst.b #n,(0x1,A3)", i.e.
 * bit n of the set word's low byte:
 *
 *   bit 0 (0x01)  set the port's network address
 *   bit 1 (0x02)  set the port's status
 *   bit 2 (0x04)  create a new port instead of finding an existing one
 *   bit 3 (0x08)  close the port (handled entirely by the nested procedure)
 *   bit 5 (0x20)  user port; validates port type, create bit and queue length
 *
 * Address ranges (SR10.2 SAU2 image), walked end to end:
 *   0x00E6A030-0x00E6A054  prologue, *status_ret = 0, ML_$EXCLUSION_START
 *   0x00E6A056-0x00E6A064  bit 3: close port, then unlock and return
 *   0x00E6A066-0x00E6A0B4  bit 5: user-port argument validation
 *   0x00E6A0B6-0x00E6A128  port 0 RIP re-announce (includes the port+0x20
 *                          store at 0x00E6A0D4)
 *   0x00E6A12A-0x00E6A1AE  bit 2 set: NET_IO_$CREATE_PORT
 *   0x00E6A1B0-0x00E6A1EA  bit 2 clear: ROUTE_$FIND_PORT
 *   0x00E6A1EC-0x00E6A21A  bit 1: status range check
 *   0x00E6A21C-0x00E6A282  port pointer, zero-network check
 *   0x00E6A284-0x00E6A392  bit 0: network change, three RIP pairs
 *   0x00E6A394-0x00E6A3FA  bit 1: routing-counter decrements
 *   0x00E6A3FC-0x00E6A446  driver callbacks when the port leaves status 1
 *   0x00E6A448-0x00E6A4AC  status store and IDP channel registration
 *   0x00E6A4AE-0x00E6A516  routing initialisation for the new status
 *   0x00E6A518-0x00E6A594  status-0-only cleanup, else restore the old status
 *   0x00E6A596-0x00E6A5D6  unlock, RIP updates, reply record, epilogue
 *
 * A5 is loaded with 0x00E825DC at 0x00E6A038 but ROUTE_$SERVICE never uses it
 * itself; the module-local procedures it reaches with bsr (ROUTE_$ANNOUNCE_NET
 * at 0x00E69FF2) do.
 *
 * Original address: 0x00E6A030 (1448 bytes)
 */

#include "route/route_internal.h"
#include "rip/rip.h"
#include "net_io/net_io.h"
#include "ml/ml.h"
#include "hint/hint.h"
#include "xns_idp/xns_idp.h"
#include "app/app.h"
#include "sock/sock.h"
#include "arch/arch.h"

/*
 * =============================================================================
 * Operation Flag Bits
 * =============================================================================
 *
 * The image tests bit n of the low byte of the operation word with
 * "btst.b #n,(0x1,A3)"; testing the whole word against 1<<n is the same
 * thing for n < 8 and does not depend on the host's byte order.
 */
#define SERVICE_OP_SET_NETWORK      0x0001  /* Bit 0: set network address */
#define SERVICE_OP_SET_STATUS       0x0002  /* Bit 1: set port status */
#define SERVICE_OP_CREATE_PORT      0x0004  /* Bit 2: create new port */
#define SERVICE_OP_CLOSE_PORT       0x0008  /* Bit 3: close port */
#define SERVICE_OP_USER_PORT        0x0020  /* Bit 5: user port validation */

/*
 * =============================================================================
 * Port status bit masks
 * =============================================================================
 *
 * Each is the D-register literal of a "btst.l Dn,Dm" over the status word,
 * i.e. a set of status VALUES 0..31.  Cited by the address of the moveq that
 * loads it.
 */
#define PORT_STATUS_VALID_MASK      0x3E  /* 0x00E6A21C: statuses 1..5 */
#define PORT_STATUS_NEED_NETWORK    0x38  /* 0x00E6A258: statuses 3,4,5 */
#define PORT0_ANNOUNCE_MASK         0x3C  /* 0x00E6A0BC: statuses 2..5 */
#define PORT_STATUS_ROUTING_MASK    0x30  /* 0x00E6A3B4 / 0x00E6A4C8: 4,5 */
#define PORT_STATUS_DISABLE_STD     0x0E  /* 0x00E6A3C2 / 0x00E6A4BA: 1,2,3 */
#define PORT_STATUS_N_ROUTING_MASK  0x28  /* 0x00E6A3DA / 0x00E6A50A: 3,5 */
#define PORT_STATUS_DISABLE_N       0x16  /* 0x00E6A3E8 / 0x00E6A4FA: 1,2,4 */

/*
 * Port types route_$close_port accepts: `moveq #0x6,D1 / btst.l D0,D1` at
 * 0x00E69F08, i.e. types 1 and 2.
 */
#define PORT_TYPE_VALID_MASK        0x06

/*
 * Old-status values that make route_$close_port drop the port from the
 * routing counters: `moveq #0x28,D1 / btst.l D0,D1` at 0x00E69F2E, i.e.
 * statuses 3 and 5.
 */
#define PORT_STATE_DECREMENT_MASK   0x28

/* Largest queue length a user port may ask for (0x00E6A09A cmpi.w #0x20) */
#define MAX_USER_PORT_QUEUE_LENGTH  0x20

/* The status value that means "port closed"; the image compares against the
 * literal 1 at 0x00E6A3FC, 0x00E6A452 and 0x00E6A520. */
#define PORT_STATUS_CLOSED          1

/*
 * =============================================================================
 * PC-relative constant cells
 * =============================================================================
 *
 * Every RIP_$UPDATE_D argument the image passes as "pea (d,PC)" is a named
 * cell in the ROUTE_UNWIRED code segment.  Bytes read out of the image:
 *
 *   0x00E69FAE  00 00      op byte 0x00 - add a standard route
 *   0x00E69FB0  00 10      hop-count word 0x0010
 *   0x00E6A5D8  00 00      hop-count word 0x0000
 *   0x00E6A5DA  ff 00      op byte 0xFF - add a non-standard route
 *   0x00E6A02C  00 00 00 00
 *
 * The two op cells are read as single bytes by RIP_$UPDATE_D (its flags
 * argument is a boolean pointer), which is why each is spelled as a byte.
 *
 * Which cell each of the six RIP_$UPDATE_D calls passes, taken from the pea
 * displacements (the m68k PC for "pea (d,PC)" is the instruction address + 2):
 *
 *   port 0 re-announce   0x00E6A0F6 -> hop 0x00E6A5D8, 0x00E6A0EE -> op 0x00E69FAE
 *                        0x00E6A116 -> hop 0x00E6A5D8, 0x00E6A10E -> op 0x00E6A5DA
 *   old-network removal  0x00E6A2CA -> hop 0x00E69FB0, 0x00E6A2C2 -> op 0x00E69FAE
 *                        0x00E6A2EA -> hop 0x00E69FB0, 0x00E6A2E2 -> op 0x00E6A5DA
 *   new-network add      0x00E6A35C -> hop 0x00E6A5D8, 0x00E6A354 -> op 0x00E69FAE
 *                        0x00E6A37E -> hop 0x00E6A5D8, 0x00E6A376 -> op 0x00E6A5DA
 *
 * so the middle pair is the only one that carries a hop count of 0x10.
 */
static const boolean RIP_OP_STD = 0x00;                 /* 0x00E69FAE */
static const uint16_t RIP_HOP_COUNT_16 = 0x0010;        /* 0x00E69FB0 */
static const uint16_t RIP_HOP_COUNT_ZERO = 0x0000;      /* 0x00E6A5D8 */
static const boolean RIP_OP_NON_STD = (boolean)0xFF;    /* 0x00E6A5DA */

/*
 * 0x00E6A02C: four zero bytes, passed as the attach_service callback's
 * "service_rec" argument (0x00E6A436 "pea (-0x40c,PC)").  Two words, matching
 * route_$set_service_fn_t's {opcode, service} record.
 */
static const uint16_t ROUTE_$NULL_SERVICE_REC[2] = { 0x0000, 0x0000 };

/*
 * =============================================================================
 * route_$close_port - the nested procedure at 0x00E69EC2
 * =============================================================================
 *
 * ROUTE_$SERVICE reaches it with `bsr.w 0x00e69ec2` (0x00E6A05E) and pushes
 * NOTHING.  The callee walks back into its parent's frame with
 * `movea.l (A6),A2` (0x00E69ECA) and reads four things from it:
 *
 *   (0x0c,A2)   ROUTE_$SERVICE's port_info argument  (0x00E69ECE, 0x00E69EDA,
 *               0x00E69F00)
 *   (0x10,A2)   ROUTE_$SERVICE's status_ret argument (0x00E69EF2, 0x00E69F0E,
 *               0x00E69F5A)
 *   (-0x48,A2)  ROUTE_$SERVICE's short_port local     (0x00E69F42, 0x00E69F62)
 *   (-0x62,A2)  ROUTE_$SERVICE's old_status local     (0x00E69F2A)
 *
 * The last one is read on a path where ROUTE_$SERVICE has not written it:
 * the close-port arm at 0x00E6A056 runs before anything stores to A6-0x62
 * (the first such store is at 0x00E6A448, in the status arm).  The value
 * tested is therefore whatever the frame happened to contain, and this
 * translation reproduces that by taking it as a parameter and letting the
 * caller pass its own uninitialised local.
 *
 * Its own frame is `link.w A6,-0x18`, whose only named cell is the 12-byte
 * `source` at A6-0x10 that it hands to RIP_$UPDATE_D.
 */
static void route_$close_port(route_$short_port_t *port_info,
                              status_$t *status_ret,
                              uint16_t old_status,
                              route_$short_port_t *short_port)
{
    int16_t port_index;
    route_$port_t *port;
    rip_$xns_addr_t source;             /* its own A6-0x10 */

    /*
     * 0x00E69ECC-0x00E69EEA.  The first argument is the port TYPE word at
     * port_info+0x06, not a network: ROUTE_$FIND_PORT compares it against
     * port+0x2E (0x00E15B1E) and its second argument against the
     * sign-extended port+0x30 (0x00E15B24).
     */
    port_index = ROUTE_$FIND_PORT(port_info->port_type,
                                  (int32_t)(int16_t)port_info->socket);

    /* 0x00E69EEC-0x00E69EFC */
    if (port_index == -1) {
        *status_ret = status_$internet_unknown_network_port;
        return;
    }

    /*
     * 0x00E69F00-0x00E69F18: `moveq #0x6,D1 / btst.l D0,D1` over the same
     * port-type word, i.e. types 1 and 2 only.
     */
    if (((1u << (port_info->port_type & 0x1F)) & PORT_TYPE_VALID_MASK) == 0) {
        *status_ret = status_$internet_illegal_port_type;
        return;
    }

    /* 0x00E69F1C-0x00E69F26 */
    port = &ROUTE_$PORT_ARRAY[port_index];

    /*
     * 0x00E69F2A-0x00E69F40: `move.w (-0x62,A2),D0w / moveq #0x28,D1 /
     * btst.l D0,D1` - the PARENT's old_status local, not this port's status
     * word, and unwritten on this path (see the note above).  When the bit
     * is set the port is dropped from the routing counters with
     * ROUTE_$DECREMENT_PORT(true, port_index, 0) - `st -(SP)` at
     * 0x00E69F3A pushes the byte true.
     */
    if (((1u << (old_status & 0x1F)) & PORT_STATE_DECREMENT_MASK) != 0) {
        ROUTE_$DECREMENT_PORT(true, port_index, 0);
    }

    /* 0x00E69F42-0x00E69F4C: fills the PARENT's short_port local. */
    ROUTE_$SHORT_PORT(port, short_port);

    /*
     * 0x00E69F4E-0x00E69F58: the source address is the port's network
     * followed by six bytes of whatever this frame held, with the low 20
     * bits of the longword at A6-0xA masked off.
     *
     *   00e69f4e  move.l (A3),(-0x10,A6)
     *   00e69f52  andi.l #-0x100000,(-0xa,A6)
     */
    source.network = port->network;
    {
        uint32_t host_tail = ((uint32_t)source.host[2] << 24) |
                             ((uint32_t)source.host[3] << 16) |
                             ((uint32_t)source.host[4] << 8) |
                             (uint32_t)source.host[5];
        host_tail &= 0xFFF00000;
        source.host[2] = (uint8_t)(host_tail >> 24);
        source.host[3] = (uint8_t)(host_tail >> 16);
        source.host[4] = (uint8_t)(host_tail >> 8);
        source.host[5] = (uint8_t)host_tail;
    }

    /*
     * 0x00E69F5A-0x00E69F76.  The two `pea (d,PC)` cells resolve into the
     * same constants ROUTE_$SERVICE uses (the m68k PC for `pea (d,PC)` is
     * the instruction address + 2):
     *   0x00E69F66 -> 0x00E69F68 + 0x48 = 0x00E69FB0 = RIP_HOP_COUNT_16
     *   0x00E69F5E -> 0x00E69F60 + 0x4E = 0x00E69FAE = RIP_OP_STD
     */
    RIP_$UPDATE_D(&port->network, &source, &RIP_HOP_COUNT_16,
                  (const uint8_t *)short_port, &RIP_OP_STD, status_ret);

    /* 0x00E69F7A-0x00E69F9E */
    if (port->port_type == ROUTE_PORT_TYPE_ROUTING) {
        SOCK_$CLOSE(port->socket);          /* 0x00E69F84 */
        ROUTE_$N_USER_PORTS--;              /* 0x00E69F90 */
        ROUTE_$CLEANUP_WIRED();             /* 0x00E69F96 */

        /*
         * 0x00E69F9A: `movea.l (0x44,A3),A0 / clr.b (A0)`.  +0x44 is
         * route_$port_t.driver_stats, the address of this port's
         * route_$port_stats_t, and the byte cleared is the HIGH byte of
         * that block's flags word - the "in use" boolean
         * NET_IO_$CREATE_PORT sets with `st (A1)` at 0x00E5A682.  Done as
         * a word mask so it does not depend on the host's byte order.
         */
        {
            route_$port_stats_t *stats =
                (route_$port_stats_t *)ARCH_VA_TO_PTR(port->driver_stats);

            stats->flags &= 0x00FF;
        }
    }

    /* 0x00E69FA0 */
    port->active = 0;
}

/*
 * =============================================================================
 * Implementation
 * =============================================================================
 */
void ROUTE_$SERVICE(const uint16_t *operation, route_$short_port_t *port_info,
                    status_$t *status_ret)
{
    int16_t port_index;                 /* D2w */
    route_$port_t *port;                /* A1/A2, saved in D4 at 0x00E6A22A */
    route_$short_port_t short_port;     /* A6-0x48 */
    rip_$xns_addr_t source;             /* A6-0x10 */
    status_$t rip_status;               /* A6-0x54 */
    status_$t idp_status;               /* A6-0x50 */
    uint16_t idp_port;                  /* A6-0x66 */
    uint16_t old_status;                /* A6-0x62 */
    uint16_t queue_length;              /* A6-0x68 */
    void *driver;                       /* A6-0x5C */
    uint16_t attach_out;                /* A6-0x60, never read by the caller */
    route_$driver_info_t *driver_info;  /* A3, after it stops holding the ops */
    uint32_t effective_network;
    uint16_t check_status;
    int i;

    /* 0x00E6A042-0x00E6A046 */
    *status_ret = status_$ok;

    /* 0x00E6A048-0x00E6A054 */
    ML_$EXCLUSION_START(&ROUTE_$SERVICE_MUTEX);

    /*
     * 0x00E6A056-0x00E6A064.  route_$close_port (0x00E69EC2) is a nested
     * Pascal procedure of this routine: `bsr.w` with nothing pushed, and the
     * callee reads this frame through the saved frame pointer.  The four
     * values it takes that way are passed explicitly here - including
     * old_status, which this routine has NOT written on this path (its first
     * store is at 0x00E6A448), so the value the image tests is whatever the
     * frame held.
     */
    if (*operation & SERVICE_OP_CLOSE_PORT) {
        /*
         * old_status is deliberately read before it is written: that is what
         * 0x00E69F2A does with (-0x62,A2).  The warning is suppressed rather
         * than "fixed" with an initialiser, because giving it a value would
         * be an invention, not a transcription.
         */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#endif
        route_$close_port(port_info, status_ret, old_status, &short_port);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        /* 0x00E6A062 branches into the unlock at 0x00E6A274 */
        ML_$EXCLUSION_STOP(&ROUTE_$SERVICE_MUTEX);
        return;
    }

    /* 0x00E6A066-0x00E6A0B4 */
    if (*operation & SERVICE_OP_USER_PORT) {
        if (port_info->port_type != ROUTE_PORT_TYPE_ROUTING) {
            /* 0x00E6A07E */
            *status_ret = status_$route_illegal_op_for_port_type;
        } else if (!(*operation & SERVICE_OP_CREATE_PORT)) {
            /* 0x00E6A092 */
            *status_ret = status_$route_create_flag_required;
        } else if (port_info->queue_length > MAX_USER_PORT_QUEUE_LENGTH) {
            /* 0x00E6A09A "cmpi.w #0x20,(0xa,A0) / bls": unsigned */
            *status_ret = status_$route_queue_length_too_large;
        }

        /* 0x00E6A0AC-0x00E6A0B4 */
        if (*status_ret != status_$ok) {
            ML_$EXCLUSION_STOP(&ROUTE_$SERVICE_MUTEX);
            return;
        }
    }

    /*
     * 0x00E6A0B6-0x00E6A128: when port 0's status is one of 2..5, re-announce
     * its network to RIP.
     */
    if (((uint32_t)PORT0_ANNOUNCE_MASK >> (ROUTE_$PORT_ARRAY[0].active & 0x1F)) & 1) {
        /* 0x00E6A0C6-0x00E6A0D0 */
        ROUTE_$SHORT_PORT(&ROUTE_$PORT_ARRAY[0], &short_port);

        /*
         * 0x00E6A0D2-0x00E6A0DA: the port's own XNS endpoint network word at
         * port+0x20 is refreshed from port->network before the announcement.
         * ("move.l (A2),D1 / move.l D1,(0x20,A2) / move.l D1,(-0x10,A6)")
         */
        ROUTE_$PORT_ARRAY[0].xns_addr.network = ROUTE_$PORT_ARRAY[0].network;
        source.network = ROUTE_$PORT_ARRAY[0].network;

        /* 0x00E6A0DC-0x00E6A0E8: three clr.w over A6-0xC..A6-0x7 */
        for (i = 0; i < 6; i++) {
            source.host[i] = 0;
        }

        /* 0x00E6A0EA-0x00E6A106 */
        RIP_$UPDATE_D(&ROUTE_$PORT_ARRAY[0].network, &source,
                      &RIP_HOP_COUNT_ZERO, (const uint8_t *)&short_port,
                      &RIP_OP_STD, status_ret);
        /* 0x00E6A10A-0x00E6A126 */
        RIP_$UPDATE_D(&ROUTE_$PORT_ARRAY[0].network, &source,
                      &RIP_HOP_COUNT_ZERO, (const uint8_t *)&short_port,
                      &RIP_OP_NON_STD, status_ret);
    }

    if (*operation & SERVICE_OP_CREATE_PORT) {
        /* 0x00E6A132-0x00E6A14A */
        if (*operation & SERVICE_OP_USER_PORT) {
            queue_length = port_info->queue_length;
        } else {
            queue_length = 10;
        }

        /* 0x00E6A14C-0x00E6A168 */
        if (port_info->port_type == ROUTE_PORT_TYPE_LOCAL) {
            driver = NET_IO_$NIL_DRIVER;
        } else {
            driver = NET_IO_$USER_DRIVER;
        }

        /*
         * 0x00E6A16A-0x00E6A18C.  "subq.l #0x2,SP" reserves the word function
         * result slot; the value is taken from D0 afterwards.
         */
        port_index = NET_IO_$CREATE_PORT(port_info->port_type, 0, driver,
                                         queue_length, status_ret);

        /* 0x00E6A18E-0x00E6A196 */
        if (*status_ret != status_$ok) {
            ML_$EXCLUSION_STOP(&ROUTE_$SERVICE_MUTEX);
            return;
        }

        /* 0x00E6A198-0x00E6A1AE */
        if (port_info->port_type == ROUTE_PORT_TYPE_ROUTING) {
            ROUTE_$N_USER_PORTS++;
            route_$wire_routing_area();
        }
    } else {
        /*
         * 0x00E6A1B0-0x00E6A1CA: the socket word is sign-extended to a
         * longword before the push ("move.w (0x8,A0),D1w / ext.l D1").
         */
        port_index = ROUTE_$FIND_PORT(port_info->port_type,
                                      (int32_t)(int16_t)port_info->socket);

        /* 0x00E6A1CC-0x00E6A1EA: the unlock happens before the status store */
        if (port_index == -1) {
            ML_$EXCLUSION_STOP(&ROUTE_$SERVICE_MUTEX);
            *status_ret = status_$internet_unknown_network_port;
            return;
        }
    }

    /* 0x00E6A1EC-0x00E6A21A */
    if (*operation & SERVICE_OP_SET_STATUS) {
        if ((((uint32_t)PORT_STATUS_VALID_MASK >> (port_info->status & 0x1F)) & 1) == 0) {
            ML_$EXCLUSION_STOP(&ROUTE_$SERVICE_MUTEX);
            *status_ret = status_$route_service_type_bad;
            return;
        }
    }

    /* 0x00E6A21C-0x00E6A22C: D3 = 0x5C * port_index, D4 keeps the pointer */
    port = &ROUTE_$PORT_ARRAY[port_index];

    /* 0x00E6A22E-0x00E6A240 */
    if (*operation & SERVICE_OP_SET_NETWORK) {
        effective_network = port_info->network;
    } else {
        effective_network = port->network;
    }

    if (effective_network == 0) {
        /* 0x00E6A242-0x00E6A256 */
        if (*operation & SERVICE_OP_SET_STATUS) {
            check_status = port_info->status;
        } else {
            check_status = port->active;
        }

        /* 0x00E6A258-0x00E6A272 */
        if (((uint32_t)PORT_STATUS_NEED_NETWORK >> (check_status & 0x1F)) & 1) {
            *status_ret = status_$route_no_routing_zero_network;
            ROUTE_$SHORT_PORT(port, port_info);
            /* falls into the unlock at 0x00E6A274 */
            ML_$EXCLUSION_STOP(&ROUTE_$SERVICE_MUTEX);
            return;
        }
    }

    /* 0x00E6A284-0x00E6A298 */
    if ((*operation & SERVICE_OP_SET_NETWORK) &&
        port->network != port_info->network) {

        /* 0x00E6A29C-0x00E6A2FC */
        if (port->network != 0) {
            ROUTE_$SHORT_PORT(port, &short_port);
            source.network = port->network;
            for (i = 0; i < 6; i++) {
                source.host[i] = 0;
            }

            /*
             * 0x00E6A2BE-0x00E6A2DA and 0x00E6A2DE-0x00E6A2FA.  This pair is
             * the one that passes the hop-count cell at 0x00E69FB0 (0x0010),
             * not the zero cell the other two pairs use.
             */
            RIP_$UPDATE_D(&port->network, &source, &RIP_HOP_COUNT_16,
                          (const uint8_t *)&short_port, &RIP_OP_STD,
                          &rip_status);
            RIP_$UPDATE_D(&port->network, &source, &RIP_HOP_COUNT_16,
                          (const uint8_t *)&short_port, &RIP_OP_NON_STD,
                          &rip_status);
        }

        /* 0x00E6A2FE-0x00E6A31A */
        if (port_index == 0) {
            ROUTE_$ANNOUNCE_NET(port_info->network);
            HINT_$ADD_NET(port_info->network);
        }

        /* 0x00E6A31C-0x00E6A324 */
        port->network = port_info->network;
        port->xns_addr.network = port->network;

        /* 0x00E6A326-0x00E6A390 */
        if (port_info->network != 0) {
            ROUTE_$SHORT_PORT(port, &short_port);
            source.network = port_info->network;
            for (i = 0; i < 6; i++) {
                source.host[i] = 0;
            }

            /*
             * 0x00E6A350-0x00E6A36E and 0x00E6A372-0x00E6A390.  Argument 1 is
             * the request record's own address ("move.l (0xc,A6),-(SP)"),
             * whose first longword is the network just stored.
             */
            RIP_$UPDATE_D(&port_info->network, &source, &RIP_HOP_COUNT_ZERO,
                          (const uint8_t *)&short_port, &RIP_OP_STD,
                          &rip_status);
            RIP_$UPDATE_D(&port_info->network, &source, &RIP_HOP_COUNT_ZERO,
                          (const uint8_t *)&short_port, &RIP_OP_NON_STD,
                          &rip_status);
        }
    }

    /* 0x00E6A394-0x00E6A3AC */
    if ((*operation & SERVICE_OP_SET_STATUS) &&
        port->active != port_info->status) {

        /* 0x00E6A3B0 */
        old_status = port->active;

        /* 0x00E6A3B4-0x00E6A3D4: leaving a routing status for a non-routing
         * one gives back the standard routing port count. */
        if ((((uint32_t)PORT_STATUS_ROUTING_MASK >> (old_status & 0x1F)) & 1) &&
            (((uint32_t)PORT_STATUS_DISABLE_STD >> (port_info->status & 0x1F)) & 1)) {
            /* "subq.l #0x2,SP" reserves a discarded word result slot */
            ROUTE_$DECREMENT_PORT(0, port_index, (int8_t)0xFF);
        }

        /* 0x00E6A3D6-0x00E6A3FA: the same for the non-standard count */
        if ((((uint32_t)PORT_STATUS_N_ROUTING_MASK >> (old_status & 0x1F)) & 1) &&
            (((uint32_t)PORT_STATUS_DISABLE_N >> (port_info->status & 0x1F)) & 1)) {
            ROUTE_$DECREMENT_PORT(0, port_index, 0);
        }

        /*
         * 0x00E6A3FC-0x00E6A446: a port leaving the closed status runs the
         * driver's two open-side entries.  A3 stops being the operation
         * pointer here; the image does not test another operation bit after
         * 0x00E6A396.
         */
        if (old_status == PORT_STATUS_CLOSED) {
            driver_info = (route_$driver_info_t *)ARCH_VA_TO_PTR(port->driver_info);

            /* 0x00E6A408-0x00E6A41C */
            if (driver_info->leave_status_1 != 0) {
                route_$port_status_fn_t leave_fn =
                    (route_$port_status_fn_t)ARCH_VA_TO_PTR(driver_info->leave_status_1);

                leave_fn(&port->socket, status_ret);
            }

            /* 0x00E6A41E-0x00E6A444 */
            if (*status_ret == status_$ok && driver_info->attach_service != 0) {
                route_$set_service_fn_t attach_fn =
                    (route_$set_service_fn_t)ARCH_VA_TO_PTR(driver_info->attach_service);

                /*
                 * Five arguments plus a discarded word result slot:
                 *   pea (0x30,A2)      &port->socket
                 *   pea (-0x40c,PC)    the zero record at 0x00E6A02C
                 *   clr.w -(SP)        request word 0
                 *   pea (-0x60,A6)     an uninitialised local
                 *   pea (A0)           status_ret (A0 still holds it)
                 */
                (void)attach_fn(&port->socket, ROUTE_$NULL_SERVICE_REC, 0,
                                &attach_out, status_ret);
            }
        }

        /* 0x00E6A448-0x00E6A450 */
        port->active = port_info->status;

        /* 0x00E6A452-0x00E6A4AC */
        if (old_status == PORT_STATUS_CLOSED && *status_ret == status_$ok) {
            if (RIP_$STD_IDP_CHANNEL != -1) {
                idp_port = (uint16_t)port_index;
                XNS_IDP_$OS_ADD_PORT((uint16_t *)&RIP_$STD_IDP_CHANNEL,
                                     &idp_port, &idp_status);
            }
            /* "cmpi.w #-0x1,(0x00e1dc20).l" is a signed word compare */
            if ((int16_t)APP_$STD_IDP_CHANNEL != -1) {
                idp_port = (uint16_t)port_index;
                XNS_IDP_$OS_ADD_PORT((uint16_t *)&APP_$STD_IDP_CHANNEL,
                                     &idp_port, &idp_status);
            }
        }

        /* 0x00E6A4AE-0x00E6A4EC */
        if (*status_ret == status_$ok &&
            (((uint32_t)PORT_STATUS_DISABLE_STD >> (old_status & 0x1F)) & 1) &&
            (((uint32_t)PORT_STATUS_ROUTING_MASK >> (port_info->status & 0x1F)) & 1)) {
            if (RIP_$STD_IDP_CHANNEL != -1) {
                ROUTE_$INIT_ROUTING(port_index, (int8_t)0xFF);
            } else {
                /* 0x00E6A4E4 */
                *status_ret = status_$internet_network_port_not_open;
            }
        }

        /* 0x00E6A4EE-0x00E6A516 */
        if (*status_ret == status_$ok &&
            (((uint32_t)PORT_STATUS_DISABLE_N >> (old_status & 0x1F)) & 1) &&
            (((uint32_t)PORT_STATUS_N_ROUTING_MASK >> (port_info->status & 0x1F)) & 1)) {
            ROUTE_$INIT_ROUTING(port_index, 0);
        }

        /*
         * 0x00E6A518-0x00E6A594.  Three outcomes, and only the first restores
         * the old status:
         *   status != 0                      -> 0x00E6A590, port->active =
         *                                       old_status
         *   status == 0 and active != 1      -> 0x00E6A596, nothing restored
         *   status == 0 and active == 1      -> the close-side cleanup below
         */
        if (*status_ret != status_$ok) {
            /* 0x00E6A590 */
            port->active = old_status;
        } else if (port->active == PORT_STATUS_CLOSED) {
            driver_info = (route_$driver_info_t *)ARCH_VA_TO_PTR(port->driver_info);

            /* 0x00E6A52C-0x00E6A540 */
            if (driver_info->enter_status_1 != 0) {
                route_$port_status_fn_t enter_fn =
                    (route_$port_status_fn_t)ARCH_VA_TO_PTR(driver_info->enter_status_1);

                enter_fn(&port->socket, status_ret);
            }

            /* 0x00E6A542-0x00E6A58C */
            if (RIP_$STD_IDP_CHANNEL != -1) {
                idp_port = (uint16_t)port_index;
                XNS_IDP_$OS_DELETE_PORT((uint16_t *)&RIP_$STD_IDP_CHANNEL,
                                        &idp_port, &idp_status);
            }
            if ((int16_t)APP_$STD_IDP_CHANNEL != -1) {
                idp_port = (uint16_t)port_index;
                XNS_IDP_$OS_DELETE_PORT((uint16_t *)&APP_$STD_IDP_CHANNEL,
                                        &idp_port, &idp_status);
            }
        }
    }

    /* 0x00E6A596-0x00E6A5A2 */
    ML_$EXCLUSION_STOP(&ROUTE_$SERVICE_MUTEX);

    /*
     * 0x00E6A5A4-0x00E6A5BA.  Both calls reserve a discarded word result slot
     * ("subq.l #0x2,SP"), so RIP_$SEND_UPDATES is a Pascal function whose
     * result this caller throws away.
     */
    RIP_$SEND_UPDATES(0);
    RIP_$SEND_UPDATES((boolean)0xFF);

    /*
     * 0x00E6A5BC-0x00E6A5CA: the reply record.  The image re-forms the port
     * address from the saved 0x5C*port_index in D3 rather than from D4.
     */
    ROUTE_$SHORT_PORT(&ROUTE_$PORT_ARRAY[port_index], port_info);
}
