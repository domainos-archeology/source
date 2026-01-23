/*
 * NETWORK_$ADD_REQUEST_SERVERS - Create request server processes
 *
 * Creates network request server processes up to the requested count,
 * capped at a maximum of 3. Request servers handle remote file operations
 * and other network service requests.
 *
 * Original address: 0x00E71E0C
 *
 * Assembly analysis:
 *   00e71e0c    link.w A6,-0x4
 *   00e71e10    movem.l { A4 A3 A2 D2},-(SP)
 *   00e71e14    movea.l (0xc,A6),A2         ; A2 = status_ret
 *   00e71e18    clr.l (A2)                  ; *status_ret = 0
 *   00e71e1a    moveq #0x3,D2               ; D2 = 3 (max servers)
 *   00e71e1c    movea.l (0x8,A6),A0         ; A0 = count_ptr
 *   00e71e20    cmp.w (A0),D2w              ; if 3 < *count_ptr
 *   00e71e22    ble.b 0x00e71e26            ; use 3
 *   00e71e24    move.w (A0),D2w             ; else use *count_ptr
 *   00e71e26    movea.l #0xe248fc,A1        ; Network data base
 *   00e71e2c    lea (0x320,A1),A3           ; A3 = &REQUEST_SERVER_CNT
 *   00e71e30    lea (0x34e,A1),A4           ; A4 = &REALLY_DISKLESS
 *   00e71e34    bra.b 0x00e71e60            ; Jump to loop condition
 * loop:
 *   00e71e38    tst.b (A4)                  ; Test REALLY_DISKLESS
 *   00e71e3a    bpl.b 0x00e71e42            ; If >= 0, continue
 *   00e71e3c    cmpi.w #0x1,(A3)            ; If diskless and count == 1
 *   00e71e40    beq.b 0x00e71e64            ; Don't remove last server
 *   00e71e42    pea (A2)                    ; Push status_ret
 *   00e71e44    move.l #0x18000009,-(SP)    ; Process type (request server)
 *   00e71e4a    move.l #0xe118dc,-(SP)      ; NETWORK_$REQUEST_SERVER entry
 *   00e71e50    jsr PROC1_$CREATE_P
 *   00e71e56    lea (0xc,SP),SP
 *   00e71e5a    tst.l (A2)                  ; Check status
 *   00e71e5c    bne.b 0x00e71e64            ; Exit on error
 *   00e71e5e    addq.w #0x1,(A3)            ; Increment count
 * condition:
 *   00e71e60    cmp.w (A3),D2w              ; While D2 > count
 *   00e71e62    bgt.b 0x00e71e38
 * done:
 *   00e71e64    movea.l #0xe248fc,A0
 *   00e71e6a    move.w (0x320,A0),D0w       ; Return current count
 *   ...
 *
 * Process type 0x18000009:
 *   - High byte 0x18: Network request process type
 *   - Low byte 0x09: Stack type (request server stack)
 */

#include "network/network.h"

/*
 * Maximum number of request servers
 */
#define MAX_REQUEST_SERVERS         3

/*
 * Process type for request server:
 *   - High word (0x1800): Network request subsystem process type
 *   - Low word (0x0009): Stack allocation type
 */
#define REQUEST_SERVER_PROCESS_TYPE 0x18000009

int16_t NETWORK_$ADD_REQUEST_SERVERS(int16_t *count_ptr, status_$t *status_ret)
{
    int16_t target_count;

    *status_ret = status_$ok;

    /*
     * Cap the target count at MAX_REQUEST_SERVERS (3).
     * Original assembly: uses D2 as target, comparing and selecting min.
     */
    target_count = MAX_REQUEST_SERVERS;
    if (*count_ptr < MAX_REQUEST_SERVERS) {
        target_count = *count_ptr;
    }

    /*
     * Create request server processes until we reach the target count.
     *
     * Loop condition:
     *   - Continue while REQUEST_SERVER_CNT < target_count
     *   - On diskless nodes, don't go below 1 server (safety limit)
     */
    while (NETWORK_$REQUEST_SERVER_CNT < target_count) {
        /*
         * On diskless nodes (NETWORK_$REALLY_DISKLESS < 0), we need
         * at least one request server running. Don't create more if
         * we're already at the minimum.
         */
        if ((NETWORK_$REALLY_DISKLESS < 0) && (NETWORK_$REQUEST_SERVER_CNT == 1)) {
            break;
        }

        /*
         * Create a new request server process.
         * Entry point: NETWORK_$REQUEST_SERVER (0x00E118DC)
         * Process type: 0x18000009 (network request server)
         */
        PROC1_$CREATE_P((void *)NETWORK_$REQUEST_SERVER,
                        REQUEST_SERVER_PROCESS_TYPE,
                        status_ret);

        if (*status_ret != status_$ok) {
            /* Process creation failed - return current count */
            break;
        }

        /* Successfully created a request server */
        NETWORK_$REQUEST_SERVER_CNT++;
    }

    return NETWORK_$REQUEST_SERVER_CNT;
}
