/*
 * ROUTE_$FIND_PORT - Find port index by network and socket
 *
 * Searches through all routing ports to find one matching the given
 * network and socket numbers. Returns the port index if found.
 *
 * The function iterates through the ROUTE_$PORTP (8 port pointers)
 * checking each port's active status, network, and socket fields.
 *
 * Original address: 0x00E15AF8
 *
 * Assembly:
 * 00e15af8    link.w A6,-0xc
 * 00e15afc    movem.l {  A5 D4 D3 D2},-(SP)
 * 00e15b00    lea (0xe26ee4).l,A5       ; A5 = &ROUTE_$SOCK_ECVAL
 * 00e15b06    move.w (0x8,A6),D3w       ; D3 = network parameter
 * 00e15b0a    move.l (0xa,A6),D0        ; D0 = socket parameter (32-bit)
 * 00e15b0e    moveq #0x7,D1             ; D1 = 7 (loop counter)
 * 00e15b10    clr.w D2w                 ; D2 = 0 (index)
 * 00e15b12    movea.l A5,A0             ; A0 = base pointer
 * 00e15b14    movea.l (0x4,A0),A1       ; A1 = port pointer at offset +4
 * 00e15b18    tst.w (0x2c,A1)           ; Test port->active
 * 00e15b1c    beq.b 0x00e15b32          ; If zero, skip
 * 00e15b1e    cmp.w (0x2e,A1),D3w       ; Compare port->port_type with network
 * 00e15b22    bne.b 0x00e15b32          ; If not equal, skip
 * 00e15b24    move.w (0x30,A1),D4w      ; D4 = port->socket
 * 00e15b28    ext.l D4                  ; Sign-extend to 32-bit
 * 00e15b2a    cmp.l D0,D4               ; Compare with socket param
 * 00e15b2c    bne.b 0x00e15b32          ; If not equal, skip
 * 00e15b2e    move.w D2w,D0w            ; Return index
 * 00e15b30    bra.b 0x00e15b3c
 * 00e15b32    addq.w #0x1,D2w           ; index++
 * 00e15b34    addq.l #0x4,A0            ; Move to next pointer
 * 00e15b36    dbf D1w,0x00e15b14        ; Loop
 * 00e15b3a    moveq #-0x1,D0            ; Return -1
 * 00e15b3c    movem.l (-0x1c,A6),{  D2 D3 D4 A5}
 * 00e15b42    unlk A6
 * 00e15b44    rts
 */

#include "route/route_internal.h"

int16_t ROUTE_$FIND_PORT(uint16_t network, int32_t socket)
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
        
        /* Found matching port */
        return i;
    }
    
    /* No matching port found */
    return -1;
}
