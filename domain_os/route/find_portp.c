/*
 * ROUTE_$FIND_PORTP - Find port structure by network and socket
 *
 * Similar to ROUTE_$FIND_PORT, but returns a pointer to the port
 * structure instead of the port index. Useful when direct access
 * to the port structure is needed.
 *
 * Original address: 0x00E15B46
 *
 * Assembly:
 * 00e15b46    link.w A6,-0xc
 * 00e15b4a    movem.l {  A5 D3 D2},-(SP)
 * 00e15b4e    lea (0xe26ee4).l,A5       ; A5 = &ROUTE_$SOCK_ECVAL
 * 00e15b54    move.w (0x8,A6),D2w       ; D2 = network parameter
 * 00e15b58    move.l (0xa,A6),D0        ; D0 = socket parameter
 * 00e15b5c    moveq #0x7,D1             ; D1 = 7 (loop counter)
 * 00e15b5e    movea.l A5,A0             ; A0 = base pointer
 * 00e15b60    movea.l (0x4,A0),A1       ; A1 = port pointer at offset +4
 * 00e15b64    tst.w (0x2c,A1)           ; Test port->active
 * 00e15b68    beq.b 0x00e15b7e          ; If zero, skip
 * 00e15b6a    cmp.w (0x2e,A1),D2w       ; Compare port->port_type with network
 * 00e15b6e    bne.b 0x00e15b7e          ; If not equal, skip
 * 00e15b70    move.w (0x30,A1),D3w      ; D3 = port->socket
 * 00e15b74    ext.l D3                  ; Sign-extend to 32-bit
 * 00e15b76    cmp.l D0,D3               ; Compare with socket param
 * 00e15b78    bne.b 0x00e15b7e          ; If not equal, skip
 * 00e15b7a    movea.l A1,A0             ; Return port pointer
 * 00e15b7c    bra.b 0x00e15b86
 * 00e15b7e    addq.l #0x4,A0            ; Move to next pointer
 * 00e15b80    dbf D1w,0x00e15b60        ; Loop
 * 00e15b84    suba.l A0,A0              ; Return NULL
 * 00e15b86    movem.l (-0x18,A6),{  D2 D3 A5}
 * 00e15b8c    unlk A6
 * 00e15b8e    rts
 */

#include "route/route_internal.h"

route_$port_t *ROUTE_$FIND_PORTP(uint16_t network, int32_t socket)
{
    int16_t i;
    route_$port_t *port;

    /*
     * Iterate through all 8 port slots.
     * The original code uses ROUTE_$SOCK_ECVAL+4 as base for the pointer array.
     */
    for (i = 0; i < ROUTE_MAX_PORTS; i++) {
        port = ROUTE_$PORTP[i];
        
        /* Check if port is active */
        if (port->active == 0) {
            continue;
        }
        
        /* Check if network matches (using port_type field at 0x2E) */
        if (port->port_type != network) {
            continue;
        }
        
        /* Check if socket matches (sign-extended comparison) */
        if ((int32_t)(int16_t)port->socket != socket) {
            continue;
        }
        
        /* Found matching port - return pointer */
        return port;
    }
    
    /* No matching port found */
    return NULL;
}
