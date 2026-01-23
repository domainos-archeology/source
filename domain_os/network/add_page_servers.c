/*
 * NETWORK_$ADD_PAGE_SERVERS - Create page server processes
 *
 * Creates network page server processes up to the requested count.
 * Page servers handle incoming page requests from remote nodes.
 *
 * Original address: 0x00E71DA4
 *
 * Assembly analysis:
 *   00e71da4    link.w A6,-0x4
 *   00e71da8    movem.l { A4 A3 A2 D2},-(SP)
 *   00e71dac    move.l (0x8,A6),D2          ; D2 = count_ptr
 *   00e71db0    movea.l (0xc,A6),A2         ; A2 = status_ret
 *   00e71db4    clr.l (A2)                  ; *status_ret = 0
 *   00e71db6    movea.l #0xe248fc,A0        ; Network data base
 *   00e71dbc    lea (0x322,A0),A3           ; A3 = &PAGE_SERVER_CNT
 *   00e71dc0    lea (0x34e,A0),A4           ; A4 = &REALLY_DISKLESS
 *   00e71dc4    bra.b 0x00e71df0            ; Jump to loop condition
 * loop:
 *   00e71dc8    tst.b (A4)                  ; Test REALLY_DISKLESS
 *   00e71dca    bpl.b 0x00e71dd2            ; If >= 0, continue
 *   00e71dcc    cmpi.w #0x1,(A3)            ; If diskless and count == 1
 *   00e71dd0    beq.b 0x00e71df8            ; Don't remove last server
 *   00e71dd2    pea (A2)                    ; Push status_ret
 *   00e71dd4    move.l #0x10000008,-(SP)    ; Process type (page server)
 *   00e71dda    move.l #0xe11548,-(SP)      ; NETWORK_$PAGE_SERVER entry
 *   00e71de0    jsr PROC1_$CREATE_P
 *   00e71de6    lea (0xc,SP),SP
 *   00e71dea    tst.l (A2)                  ; Check status
 *   00e71dec    bne.b 0x00e71df8            ; Exit on error
 *   00e71dee    addq.w #0x1,(A3)            ; Increment count
 * condition:
 *   00e71df0    movea.l D2,A0
 *   00e71df2    move.w (A0),D0w
 *   00e71df4    cmp.w (A3),D0w              ; While count < *count_ptr
 *   00e71df6    bgt.b 0x00e71dc8
 * done:
 *   00e71df8    movea.l #0xe248fc,A0
 *   00e71dfe    move.w (0x322,A0),D0w       ; Return current count
 *   ...
 *
 * Process type 0x10000008:
 *   - High byte 0x10: Network process type
 *   - Low byte 0x08: Stack type (network stack)
 */

#include "network/network.h"

/*
 * Process type for page server:
 *   - High word (0x1000): Network subsystem process type
 *   - Low word (0x0008): Stack allocation type
 */
#define PAGE_SERVER_PROCESS_TYPE    0x10000008

int16_t NETWORK_$ADD_PAGE_SERVERS(int16_t *count_ptr, status_$t *status_ret)
{
    *status_ret = status_$ok;

    /*
     * Create page server processes until we reach the requested count.
     *
     * Loop condition:
     *   - Continue while PAGE_SERVER_CNT < *count_ptr
     *   - On diskless nodes, don't go below 1 server (safety limit)
     */
    while (NETWORK_$PAGE_SERVER_CNT < *count_ptr) {
        /*
         * On diskless nodes (NETWORK_$REALLY_DISKLESS < 0), we need
         * at least one page server running. Don't create more if
         * we're already at the minimum.
         */
        if ((NETWORK_$REALLY_DISKLESS < 0) && (NETWORK_$PAGE_SERVER_CNT == 1)) {
            break;
        }

        /*
         * Create a new page server process.
         * Entry point: NETWORK_$PAGE_SERVER (0x00E11548)
         * Process type: 0x10000008 (network page server)
         */
        PROC1_$CREATE_P((void *)NETWORK_$PAGE_SERVER,
                        PAGE_SERVER_PROCESS_TYPE,
                        status_ret);

        if (*status_ret != status_$ok) {
            /* Process creation failed - return current count */
            break;
        }

        /* Successfully created a page server */
        NETWORK_$PAGE_SERVER_CNT++;
    }

    return NETWORK_$PAGE_SERVER_CNT;
}
