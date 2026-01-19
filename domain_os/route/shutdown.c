/*
 * ROUTE_$SHUTDOWN - Shutdown all routing ports
 *
 * Iterates through all active routing ports and calls ROUTE_$SERVICE
 * to shut them down gracefully. Uses different shutdown operation codes
 * based on port type:
 *   - Port type 1 (local) or 2 (routing): operation code 0x0008
 *   - Other types: operation code 0x0002 with shutdown type 2 (first)
 *     or 1 (subsequent ports)
 *
 * Original address: 0x00E6A5DC
 *
 * Assembly:
 * 00e6a5dc    link.w A6,-0x40
 * 00e6a5e0    movem.l {  A2 D3 D2},-(SP)
 * 00e6a5e4    moveq #0x7,D2             ; Loop counter (7 to 0)
 * 00e6a5e6    movea.l #0xe2e0a0,A0      ; Port array base
 * 00e6a5ec    clr.w D3w                 ; Port index = 0
 * 00e6a5ee    lea (A0),A2               ; A2 = current port
 * 00e6a5f0    tst.w (0x2c,A2)           ; Check port active
 * 00e6a5f4    beq.b 0x00e6a646          ; Skip if inactive
 * 00e6a5f6    pea (-0x38,A6)            ; Push short_info buffer
 * 00e6a5fa    pea (A2)                  ; Push port pointer
 * 00e6a5fc    bsr.w ROUTE_$SHORT_PORT
 * 00e6a600    addq.w #0x8,SP
 * 00e6a602    move.w (0x2e,A2),D0w      ; Get port_type
 * 00e6a606    cmpi.w #0x2,D0w           ; Type 2?
 * 00e6a60a    beq.b 0x00e6a612
 * 00e6a60c    cmpi.w #0x1,D0w           ; Type 1?
 * 00e6a610    bne.b 0x00e6a620          ; Neither - go to other path
 * 00e6a612    pea (-0x3c,A6)            ; status_ret
 * 00e6a616    pea (-0x38,A6)            ; short_info
 * 00e6a61a    pea (0x3e,PC)             ; Operation at 0xe6a65a (0x0008)
 * 00e6a61e    bra.b 0x00e6a63e
 * 00e6a620    tst.w D3w                 ; First port?
 * 00e6a622    bne.b 0x00e6a62c
 * 00e6a624    move.w #0x2,(-0x34,A6)    ; shutdown_type = 2
 * 00e6a62a    bra.b 0x00e6a632
 * 00e6a62c    move.w #0x1,(-0x34,A6)    ; shutdown_type = 1
 * 00e6a632    pea (-0x3c,A6)            ; status_ret
 * 00e6a636    pea (-0x38,A6)            ; short_info
 * 00e6a63a    pea (0x20,PC)             ; Operation at 0xe6a65c (0x0002)
 * 00e6a63e    bsr.w ROUTE_$SERVICE
 * 00e6a642    lea (0xc,SP),SP
 * 00e6a646    addq.w #0x1,D3w           ; index++
 * 00e6a648    lea (0x5c,A2),A2          ; Next port
 * 00e6a64c    dbf D2w,0x00e6a5f0        ; Loop
 * 00e6a650    movem.l (-0x4c,A6),{  D2 D3 A2}
 * 00e6a656    unlk A6
 * 00e6a658    rts
 */

#include "route/route_internal.h"

/* Shutdown operation codes (from constant data at 0xe6a65a and 0xe6a65c) */
static const uint16_t SHUTDOWN_OP_ROUTING = 0x0008;  /* For port types 1 and 2 */
static const uint16_t SHUTDOWN_OP_OTHER = 0x0002;    /* For other port types */

void ROUTE_$SHUTDOWN(void)
{
    int16_t i;
    int16_t port_count;
    route_$port_t *port;
    route_$short_port_t short_info;
    status_$t status;
    uint16_t shutdown_type;
    const uint16_t *operation;
    
    port_count = 0;
    
    /* Iterate through all 8 ports */
    for (i = 0; i < ROUTE_MAX_PORTS; i++) {
        port = &ROUTE_$PORT_ARRAY[i];
        
        /* Skip inactive ports */
        if (port->active == 0) {
            continue;
        }
        
        /* Extract short port info for ROUTE_$SERVICE */
        ROUTE_$SHORT_PORT(port, &short_info);
        
        /* Determine operation code based on port type */
        if (port->port_type == ROUTE_PORT_TYPE_ROUTING ||
            port->port_type == ROUTE_PORT_TYPE_LOCAL) {
            /*
             * Port types 1 (local) and 2 (routing) use the 0x0008 operation.
             */
            operation = &SHUTDOWN_OP_ROUTING;
        } else {
            /*
             * Other port types use the 0x0002 operation.
             * The short_info buffer has a shutdown_type field at offset 0x04
             * (overlapping with host_id) that indicates:
             *   - 2 for the first port shutdown
             *   - 1 for subsequent port shutdowns
             *
             * Note: This modifies short_info.host_id's high word. The original
             * code writes to offset -0x34 from frame pointer, which corresponds
             * to short_info offset 0x04 (network2 field in our structure).
             * Actually, looking at stack layout:
             *   -0x38: short_info start (12 bytes)
             *   -0x34: short_info + 4 = host_id
             * So it's setting the high 16 bits of host_id to shutdown_type.
             */
            shutdown_type = (port_count == 0) ? 2 : 1;
            /* Store in the appropriate location in short_info */
            /* The original code stores at offset 4 (as a word), overwriting
             * part of host_id. This appears to be intentional for passing
             * extra info to ROUTE_$SERVICE. */
            short_info.host_id = (short_info.host_id & 0x0000FFFF) | 
                                 ((uint32_t)shutdown_type << 16);
            operation = &SHUTDOWN_OP_OTHER;
        }
        
        /* Call ROUTE_$SERVICE to shutdown this port */
        ROUTE_$SERVICE((void *)operation, &short_info, &status);
        
        port_count++;
    }
}
