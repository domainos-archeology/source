/*
 * ROUTE_$GET_EC - Get event count for a port
 *
 * Registers and returns an event count for the specified port.
 * The port is identified by network/socket pair within the port_info
 * structure. Supports two EC types:
 *   - Type 0: Socket event count (from SOCK_$EVENT_COUNTERS array)
 *   - Type 1: Port event count (embedded in port structure at offset 0x38)
 *
 * Original address: 0x00E69C2C
 *
 * Assembly:
 * 00e69c2c    link.w A6,-0x8
 * 00e69c30    movem.l {  A4 A3 A2},-(SP)
 * 00e69c34    movea.l (0x8,A6),A2       ; A2 = port_info
 * 00e69c38    movea.l (0x14,A6),A3      ; A3 = status_ret
 * 00e69c3c    move.w (0x8,A2),D0w       ; D0 = port_info->socket (at +8)
 * 00e69c40    subq.l #0x2,SP
 * 00e69c42    ext.l D0                  ; Sign-extend socket
 * 00e69c44    move.l D0,-(SP)           ; Push socket
 * 00e69c46    move.w (0x6,A2),-(SP)     ; Push network (at +6)
 * 00e69c4a    jsr ROUTE_$FIND_PORT
 * 00e69c50    addq.w #0x8,SP
 * 00e69c52    cmpi.w #-0x1,D0w          ; Check if port found
 * 00e69c56    bne.b 0x00e69c60
 * 00e69c58    move.l #0x2b0003,(A3)     ; Port not found
 * 00e69c5e    bra.b 0x00e69cc2
 * 00e69c60    moveq #0x5c,D1            ; Port size
 * 00e69c62    movea.l #0xe2e0a0,A0      ; Port array base
 * 00e69c68    muls.w D0w,D1             ; D1 = index * 0x5C
 * 00e69c6a    cmpi.w #0x2,(0x2e,A0,D1)  ; Check port_type == 2 (routing)
 * 00e69c70    beq.b 0x00e69c7a
 * 00e69c72    move.l #0x2b0009,(A3)     ; Not in routing mode
 * 00e69c78    bra.b 0x00e69cc2
 * 00e69c7a    movea.l (0xc,A6),A1       ; A1 = ec_type pointer
 * 00e69c7e    lea (0x0,A0,D1),A2        ; A2 = &port[index]
 * 00e69c82    move.w (A1),D0w           ; D0 = *ec_type
 * 00e69c84    beq.b 0x00e69c8e          ; Type 0: socket EC
 * 00e69c86    cmpi.w #0x1,D0w           ; Type 1?
 * 00e69c8a    beq.b 0x00e69ca8          ; Type 1: port EC
 * 00e69c8c    bra.b 0x00e69cbc          ; Invalid type
 * 00e69c8e    pea (A3)                  ; status_ret
 * 00e69c90    move.w (0x30,A2),D0w      ; D0 = port->socket
 * 00e69c94    movea.l #0xe28db4,A4      ; SOCK_$EVENT_COUNTERS base
 * 00e69c9a    lsl.w #0x2,D0w            ; D0 *= 4
 * 00e69c9c    lea (0x0,A4,D0w),A4       ; A4 += socket*4
 * 00e69ca0    movea.l (-0x4,A4),A0      ; Get EC pointer (offset by -4)
 * 00e69ca4    pea (A0)
 * 00e69ca6    bra.b 0x00e69cae
 * 00e69ca8    pea (A3)                  ; status_ret
 * 00e69caa    pea (0x38,A2)             ; &port->port_ec
 * 00e69cae    jsr EC2_$REGISTER_EC1
 * 00e69cb4    movea.l (0x10,A6),A1      ; A1 = ec_ret
 * 00e69cb8    move.l A0,(A1)            ; *ec_ret = result
 * 00e69cba    bra.b 0x00e69cc2
 * 00e69cbc    move.l #0x2b0012,(A3)     ; Invalid EC type
 * 00e69cc2    movem.l (-0x14,A6),{  A2 A3 A4}
 * 00e69cc8    unlk A6
 * 00e69cca    rts
 */

#include "route/route_internal.h"

/*
 * Port info structure layout (passed to ROUTE_$GET_EC):
 *   +0x00: unknown (6 bytes)
 *   +0x06: network (2 bytes)
 *   +0x08: socket (2 bytes)
 */
typedef struct route_$port_info_t {
    uint8_t     _unknown[6];    /* 0x00: Unknown fields */
    uint16_t    network;        /* 0x06: Network identifier */
    int16_t     socket;         /* 0x08: Socket identifier */
} route_$port_info_t;

void ROUTE_$GET_EC(void *port_info, int16_t *ec_type, void **ec_ret,
                   status_$t *status_ret)
{
    route_$port_info_t *info = (route_$port_info_t *)port_info;
    int16_t port_index;
    route_$port_t *port;
    ec_$eventcount_t *ec1;
    void *ec2;
    
    /* Find the port by network/socket */
    port_index = ROUTE_$FIND_PORT(info->network, (int32_t)info->socket);
    
    if (port_index == -1) {
        *status_ret = status_$internet_unknown_network_port;
        return;
    }
    
    /* Get port pointer using array indexing */
    port = &ROUTE_$PORT_ARRAY[port_index];
    
    /* Check if port is in routing mode (type == 2) */
    if (port->port_type != ROUTE_PORT_TYPE_ROUTING) {
        *status_ret = status_$route_not_routing_mode;
        return;
    }
    
    /* Get appropriate event count based on type */
    if (*ec_type == 0) {
        /*
         * Type 0: Socket event count
         * The original code accesses SOCK_$EVENT_COUNTERS with an offset
         * calculation that effectively does: SOCK_$EVENT_COUNTERS[socket - 1]
         * (due to the -4 byte offset in the assembly)
         */
        ec1 = SOCK_$EVENT_COUNTERS[port->socket - 1];
    } else if (*ec_type == 1) {
        /*
         * Type 1: Port event count
         * The port structure has an embedded EC at offset 0x38
         */
        ec1 = (ec_$eventcount_t *)port->port_ec;
    } else {
        /* Invalid EC type */
        *status_ret = status_$route_invalid_ec_type;
        return;
    }
    
    /* Register the EC1 and return the EC2 handle */
    ec2 = EC2_$REGISTER_EC1(ec1, status_ret);
    *ec_ret = ec2;
}
