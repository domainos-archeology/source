/*
 * ROUTE_$SHORT_PORT - Extract short port info from port structure
 *
 * Copies key fields from a full port structure (0x5C bytes) into a
 * compact 12-byte format suitable for passing to other functions.
 *
 * Output format (12 bytes, one route_$short_port_t):
 *   +0x00: network (4 bytes) - from port_struct+0x00
 *   +0x04: status (2 bytes) + port_type (2 bytes) - one move.l from +0x2C
 *   +0x08: socket (2 bytes) - from port_struct+0x30
 *   +0x0A: queue_length/socket2 (2 bytes) - from port_struct+0x36
 *
 * Original address: 0x00E69C08
 *
 * Assembly:
 * 00e69c08    link.w A6,0x0
 * 00e69c0c    movea.l (0x8,A6),A0       ; A0 = port_struct
 * 00e69c10    movea.l (0xc,A6),A1       ; A1 = short_info
 * 00e69c14    move.l (A0),(A1)          ; Copy network (4 bytes)
 * 00e69c16    move.l (0x2c,A0),(0x4,A1) ; Copy host_id (4 bytes from +0x2C)
 * 00e69c1c    move.w (0x30,A0),(0x8,A1) ; Copy network2 (2 bytes from +0x30)
 * 00e69c22    move.w (0x36,A0),(0xa,A1) ; Copy socket (2 bytes from +0x36)
 * 00e69c28    unlk A6
 * 00e69c2a    rts
 */

#include "route/route_internal.h"

void ROUTE_$SHORT_PORT(route_$port_t *port_struct, route_$short_port_t *short_info)
{
    /* Copy network address from port structure offset 0x00 */
    short_info->network = port_struct->network;
    
    /*
     * 0x00E69C16 "move.l (0x2c,A0),(0x4,A1)" copies port+0x2C and port+0x2E
     * as one longword; the record calls the two halves status and port_type
     * (see route_$short_port_t), so copy them as the two words they are
     * rather than through a 32-bit view that would swap on a little-endian
     * host.
     */
    short_info->status = port_struct->active;
    short_info->port_type = port_struct->port_type;
    
    /* Copy socket from port structure offset 0x30 */
    short_info->socket = port_struct->socket;
    
    /* Copy secondary socket from port structure offset 0x36 */
    short_info->queue_length = port_struct->socket2;
}
