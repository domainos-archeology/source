/*
 * RIP_$TABLE_D and RIP_$TABLE - Routing table entry access
 *
 * RIP_$TABLE_D reads or writes one 44-byte routing table entry, with the
 * route's port identified by the port record's (port_type, socket) pair.
 * RIP_$TABLE is the compact SVC form over it: 16-byte records, ports
 * identified by index, always the standard route slot.
 *
 * Original addresses:
 *   RIP_$TABLE_D: 0x00E68E2C (356 bytes)
 *   RIP_$TABLE:   0x00E68F90 (242 bytes)
 * Both in the map's RIP_UNWIRED object at 0xE68864 (size 0x958).
 * Re-emitted from the disassembly 0x00E68E2C-0x00E68F8E and
 * 0x00E68F90-0x00E69080.
 */

#include "rip/rip_internal.h"
#include "misc/string.h"

/*
 * The route-type boolean RIP_$TABLE passes to RIP_$TABLE_D on both paths:
 * `pea (0xce,PC)` at 0x00E68FB2 and `pea (0x12,PC)` at 0x00E6906E both
 * resolve to 0x00E69082, the zero word after RIP_$TABLE's `rts`
 * (`gsk read 0x00E69080 8`: 4e 75 00 00 4e 56 ff f8).  False = the
 * standard route slot.
 */
static const boolean rip_$table_std_route = 0;     /* 0x00E69082 */

/*
 * RIP_$TABLE_D - Direct table entry access
 *
 * Parameters (five, at (0x8,A6)..(0x18,A6)):
 *   op_flag    - Domain boolean, by reference: negative = read, else write
 *   route_type - Domain boolean, by reference: negative = routes[1] (the
 *                non-standard slot), else routes[0]
 *   index      - pointer to the entry index word; MASKED IN PLACE to 0..63
 *                (`and.w D0w,(A3)` at 0x00E68E54 / 0x00E68F6A)
 *   buffer     - the rip_$table_d_buf_t record
 *   status_ret - status, cleared on entry (0x00E68E46)
 *
 * The write path builds the entry in an UNINITIALISED 44-byte local and
 * copies all of it into the table (0x00E68F6C-0x00E68F82): the route slot
 * the caller did not select is whatever the stack held, except that when
 * the index word is >= 0x40 (signed) that other slot's flags byte is
 * masked with 0x3F first (0x00E68EEA-0x00E68F02).  Reproduced as found.
 */
void RIP_$TABLE_D(boolean *op_flag, boolean *route_type, uint16_t *index,
                  rip_$table_d_buf_t *buffer, status_$t *status_ret)
{
    rip_$entry_t   local_entry;     /* (-0x30,A6): NOT initialised          */
    rip_$route_t  *route;           /* A0 / A4: &local_entry.routes[0 or 1] */
    route_$port_t *port;            /* A4 in the read path                  */
    int16_t        port_idx;        /* D0: ROUTE_$FIND_PORT result          */

    /* 0x00E68E44-0x00E68E46 */
    *status_ret = status_$ok;

    /* 0x00E68E4C-0x00E68E4E: `tst.b (A4) / bpl` */
    if (*op_flag < 0) {
        /*
         * READ (0x00E68E52-0x00E68EE2)
         */
        /* 0x00E68E52-0x00E68E54: *index &= 0x3F, written back. */
        *index &= RIP_TABLE_MASK;

        /* 0x00E68E56-0x00E68E6E: 0xE263BC + index * 0x2C, eleven longwords
         * into the local. */
        memcpy(&local_entry, &RIP_$INFO[*index], sizeof(rip_$entry_t));

        /* 0x00E68E72: buffer->dest_network (+4) = entry.network */
        buffer->dest_network = local_entry.network;

        /* 0x00E68E78-0x00E68E82: routes[1] at -0x18, routes[0] at -0x2C. */
        if (*route_type < 0) {
            route = &local_entry.routes[1];
        } else {
            route = &local_entry.routes[0];
        }

        /* 0x00E68E86-0x00E68E92: 4 + 4 + 2 bytes of nexthop to buffer + 8. */
        buffer->nexthop_network = route->nexthop.network;
        memcpy(buffer->nexthop_host, route->nexthop.host, 6);

        /* 0x00E68E94: buffer->expiration (+0) = route.expiration */
        buffer->expiration = route->expiration;

        /* 0x00E68E96-0x00E68E9C: metric byte, zero-extended to the word. */
        buffer->metric = (uint16_t)route->metric;

        /* 0x00E68EA0-0x00E68EAA: state = (flags & 0xC0) >> 6. */
        buffer->state = (uint16_t)((route->flags & 0xC0) >> RIP_STATE_SHIFT);

        /* 0x00E68EAE-0x00E68EB8: port byte, `cmpi.w #0x7 / bhi`. */
        if ((uint16_t)route->port <= 7) {
            /* 0x00E68EBA-0x00E68ED0: ROUTE_$PORTP[port] (0xE26EE8 + port*4),
             * then the port record's type word (+0x2E) and socket (+0x30). */
            port = ROUTE_$PORTP[route->port];
            buffer->port_network = port->port_type;
            buffer->port_socket  = port->socket;
        } else {
            /* 0x00E68EDA: one longword 0x00010000 over both words. */
            buffer->port_network = 0x0001;
            buffer->port_socket  = 0x0000;
        }
    } else {
        /*
         * WRITE (0x00E68EE6-0x00E68F82)
         */
        /* 0x00E68EE6-0x00E68F0C: pick the slot; an index word >= 0x40
         * (signed compare) masks the OTHER slot's flags byte in the
         * uninitialised local. */
        if (*route_type < 0) {
            if ((int16_t)*index >= 0x40) {
                local_entry.routes[0].flags &= 0x3F;    /* (-0x1c,A6) */
            }
            route = &local_entry.routes[1];             /* (-0x18,A6) */
        } else {
            if ((int16_t)*index >= 0x40) {
                local_entry.routes[1].flags &= 0x3F;    /* (-0x8,A6) */
            }
            route = &local_entry.routes[0];             /* (-0x2c,A6) */
        }

        /* 0x00E68F0E-0x00E68F22: ROUTE_$FIND_PORT(port_network word,
         * port_socket sign-extended to a longword). */
        port_idx = ROUTE_$FIND_PORT(buffer->port_network,
                                    (int32_t)(int16_t)buffer->port_socket);

        /* 0x00E68F24: the result byte is stored into the slot BEFORE it is
         * checked. */
        route->port = (uint8_t)port_idx;

        /* 0x00E68F28-0x00E68F3A: 0xFF means no such port. */
        if ((uint8_t)port_idx == 0xFF) {
            *status_ret = status_$internet_unknown_network_port;   /* 0x2B0003 */
            return;
        }

        /* 0x00E68F3C: entry.network = buffer->dest_network */
        local_entry.network = buffer->dest_network;

        /* 0x00E68F42-0x00E68F4E: 4 + 4 + 2 bytes of nexthop from buffer + 8. */
        route->nexthop.network = buffer->nexthop_network;
        memcpy(route->nexthop.host, buffer->nexthop_host, 6);

        /* 0x00E68F50: expiration */
        route->expiration = buffer->expiration;

        /* 0x00E68F52: metric = low byte of buffer->metric (+0x17). */
        route->metric = (uint8_t)buffer->metric;

        /* 0x00E68F58-0x00E68F64: flags = (flags & 0x3F) | (state byte << 6). */
        route->flags &= 0x3F;
        route->flags |= (uint8_t)((uint8_t)buffer->state << RIP_STATE_SHIFT);

        /* 0x00E68F68-0x00E68F6A: *index &= 0x3F, written back. */
        *index &= RIP_TABLE_MASK;

        /* 0x00E68F6C-0x00E68F82: the whole local, eleven longwords, into
         * 0xE263BC + index * 0x2C. */
        memcpy(&RIP_$INFO[*index], &local_entry, sizeof(rip_$entry_t));
    }

    /* 0x00E68F86-0x00E68F8E */
}

/*
 * RIP_$TABLE - Compact table entry access
 *
 * Parameters (three, at (0x8,A6)..(0x10,A6)):
 *   op_flag - Domain boolean, by reference: negative = read, else write
 *   index   - pointer to the entry index word (masked in place by TABLE_D)
 *   buffer  - the 16-byte rip_$table_buf_t record
 *
 * The intermediate rip_$table_d_buf_t at (-0x20,A6) is uninitialised.  On
 * the read path every field consumed is one TABLE_D wrote; on the write
 * path nexthop_host[0..1] and the top twelve bits of nexthop_host[2..5]
 * are never written and go to TABLE_D as whatever the stack held.
 * Reproduced as found.
 */
void RIP_$TABLE(boolean *op_flag, uint16_t *index, rip_$table_buf_t *buffer)
{
    rip_$table_d_buf_t d_buf;       /* (-0x20,A6): NOT initialised */
    status_$t          status;      /* (-0x24,A6): never read      */
    route_$port_t     *port;        /* A4                          */
    uint32_t           host_low;    /* the longword at d_buf + 0xE */

    /* 0x00E68FA4-0x00E68FA6: `tst.b (A2) / bpl` */
    if (*op_flag < 0) {
        /*
         * READ (0x00E68FA8-0x00E69006)
         */
        /* 0x00E68FA8-0x00E68FBC: RIP_$TABLE_D(op_flag, &0 (0x00E69082),
         * index, &d_buf, &status). */
        RIP_$TABLE_D(op_flag, (boolean *)&rip_$table_std_route, index,
                     &d_buf, &status);

        /* 0x00E68FC0: buffer->dest_network = d_buf.dest_network (+4) */
        buffer->dest_network = d_buf.dest_network;

        /* 0x00E68FC4-0x00E68FCE: the longword at d_buf + 0xE, i.e.
         * nexthop_host[2..5] big-endian, masked to 20 bits. */
        host_low = ((uint32_t)d_buf.nexthop_host[2] << 24)
                 | ((uint32_t)d_buf.nexthop_host[3] << 16)
                 | ((uint32_t)d_buf.nexthop_host[4] << 8)
                 |  (uint32_t)d_buf.nexthop_host[5];
        buffer->nexthop_host_low = host_low & 0xFFFFF;

        /* 0x00E68FD2: expiration (+0) */
        buffer->expiration = d_buf.expiration;

        /* 0x00E68FD8-0x00E68FEC: ROUTE_$FIND_PORT(port_network word,
         * port_socket sign-extended); the result byte is the port index. */
        buffer->port_index = (uint8_t)ROUTE_$FIND_PORT(
            d_buf.port_network, (int32_t)(int16_t)d_buf.port_socket);

        /* 0x00E68FF0: metric = low byte of d_buf.metric (+0x17). */
        buffer->metric = (uint8_t)d_buf.metric;

        /* 0x00E68FF6-0x00E69002: state_flags = (state_flags & 0x3F) |
         * (low byte of d_buf.state (+0x19) << 6). */
        buffer->state_flags &= 0x3F;
        buffer->state_flags |= (uint8_t)((uint8_t)d_buf.state << 6);
    } else {
        /*
         * WRITE (0x00E69008-0x00E69074)
         */
        /* 0x00E69008-0x00E69012: port_index byte, `cmpi.w #0x7 / bhi` -
         * an invalid index is silently ignored. */
        if ((uint16_t)buffer->port_index > 7) {
            return;
        }

        /* 0x00E69014-0x00E6901A */
        d_buf.expiration   = buffer->expiration;
        d_buf.dest_network = buffer->dest_network;

        /* 0x00E6901E-0x00E69030: the low 20 bits of the longword at
         * d_buf + 0xE are replaced, the top 12 kept (uninitialised). */
        host_low = ((uint32_t)d_buf.nexthop_host[2] << 24)
                 | ((uint32_t)d_buf.nexthop_host[3] << 16)
                 | ((uint32_t)d_buf.nexthop_host[4] << 8)
                 |  (uint32_t)d_buf.nexthop_host[5];
        host_low = (host_low & 0xFFF00000u) | (buffer->nexthop_host_low & 0xFFFFF);
        d_buf.nexthop_host[2] = (uint8_t)(host_low >> 24);
        d_buf.nexthop_host[3] = (uint8_t)(host_low >> 16);
        d_buf.nexthop_host[4] = (uint8_t)(host_low >> 8);
        d_buf.nexthop_host[5] = (uint8_t)host_low;

        /* 0x00E69034-0x00E6903A: metric byte, zero-extended. */
        d_buf.metric = (uint16_t)buffer->metric;

        /* 0x00E6903E-0x00E69048: state = (state_flags & 0xC0) >> 6. */
        d_buf.state = (uint16_t)((buffer->state_flags & 0xC0) >> 6);

        /* 0x00E6904C-0x00E69056: the port RECORD, 0xE2E0A0 + index * 0x5C
         * (ROUTE_$PORT_ARRAY, not the pointer table). */
        port = &ROUTE_$PORT_ARRAY[buffer->port_index];

        /* 0x00E6905A: nexthop_network (+8) = port->network */
        d_buf.nexthop_network = port->network;

        /* 0x00E6905E: one longword from port + 0x2E over port_network and
         * port_socket (+0x12, +0x14). */
        d_buf.port_network = port->port_type;
        d_buf.port_socket  = port->socket;

        /* 0x00E69064-0x00E69074: RIP_$TABLE_D(op_flag, &0, index, &d_buf,
         * &status). */
        RIP_$TABLE_D(op_flag, (boolean *)&rip_$table_std_route, index,
                     &d_buf, &status);
    }

    /* 0x00E69078-0x00E69080 */
}
