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
    int16_t port_count;                 /* D3w */
    route_$port_t *port;                /* A2 */
    route_$short_port_t short_info;     /* A6-0x38 */
    status_$t status;                   /* A6-0x3C */
    uint16_t shutdown_type;
    const uint16_t *operation;
    
    port_count = 0;
    
    /* 0x00E6A5E4-0x00E6A64C: dbf with D2 = 7 runs the body 8 times */
    for (i = 0; i < ROUTE_MAX_PORTS; i++) {
        port = &ROUTE_$PORT_ARRAY[i];
        
        /*
         * 0x00E6A5F0 "tst.w (0x2c,A2)" / "beq.b 0x00E6A646": an inactive port
         * skips to the loop step, which still bumps the counter, so
         * port_count is the port index, not the number of ports shut down.
         */
        if (port->active != 0) {
            /* 0x00E6A5F6-0x00E6A600 */
            ROUTE_$SHORT_PORT(port, &short_info);
            
            /* 0x00E6A602-0x00E6A610 */
            if (port->port_type == ROUTE_PORT_TYPE_ROUTING ||
                port->port_type == ROUTE_PORT_TYPE_LOCAL) {
                /* 0x00E6A61A: the constant word 0x0008 at 0x00E6A65A */
                operation = &SHUTDOWN_OP_ROUTING;
            } else {
                /*
                 * 0x00E6A620-0x00E6A630: the shutdown type goes into the
                 * record's status word at +0x04 (frame slot A6-0x34, i.e.
                 * short_info + 4) - 2 for the first port, 1 afterwards.
                 */
                shutdown_type = (port_count == 0) ? 2 : 1;
                short_info.status = shutdown_type;
                /* 0x00E6A63A: the constant word 0x0002 at 0x00E6A65C */
                operation = &SHUTDOWN_OP_OTHER;
            }
            
            /* 0x00E6A63E */
            ROUTE_$SERVICE(operation, &short_info, &status);
        }
        
        /* 0x00E6A646: unconditional, inactive ports included */
        port_count++;
    }
}
