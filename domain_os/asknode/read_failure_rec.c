/*
 * ASKNODE_$READ_FAILURE_REC - Read network failure record
 *
 * Reads the current network failure record (16 bytes) from the global
 * NETWORK_$FAILURE_REC variable. If the network activity flag is negative
 * (indicating recent activity), clears byte 2 of the failure record.
 *
 * Original address: 0x00E658D0
 * Size: 52 bytes
 *
 * Assembly analysis:
 *   link.w   A6,0x0
 *   pea      (A5)
 *   lea      (0xe82408).l,A5        ; Load packet info base
 *   tst.b    (0x00e24c42).l         ; Test NETWORK_$ACTIVITY_FLAG
 *   bpl.b    skip_clear             ; Skip if positive
 *   clr.b    (0x00e24bf6).l         ; Clear byte 2 of failure rec
 * skip_clear:
 *   movea.l  #0xe24bf4,A0           ; Source = NETWORK_$FAILURE_REC
 *   movea.l  (0x8,A6),A1            ; Dest = param_1
 *   move.l   (A0)+,(A1)+            ; Copy 4 words (16 bytes)
 *   move.l   (A0)+,(A1)+
 *   move.l   (A0)+,(A1)+
 *   move.l   (A0)+,(A1)+
 *   movea.l  (-0x4,A6),A5
 *   unlk     A6
 *   rts
 *
 * Global references:
 *   0x00E24C42 = NETWORK_$ACTIVITY_FLAG
 *   0x00E24BF4 = NETWORK_$FAILURE_REC (start of 16-byte record)
 *   0x00E24BF6 = byte 2 within failure record
 *   0x00E24BF8 = word 1 of failure record
 *   0x00E24BFC = word 2 of failure record
 *   0x00E24C00 = word 3 of failure record
 */

#include "asknode/asknode_internal.h"

/*
 * Global failure record located at 0x00E24BF4
 * This is a 16-byte structure tracking network failures.
 */
extern uint32_t NETWORK_$FAILURE_REC;       /* 0x00E24BF4 */
extern uint8_t  NETWORK_$FAILURE_REC_BYTE2; /* 0x00E24BF6 - byte within record */
extern uint32_t NETWORK_$FAILURE_REC_W1;    /* 0x00E24BF8 */
extern uint32_t NETWORK_$FAILURE_REC_W2;    /* 0x00E24BFC */
extern uint32_t NETWORK_$FAILURE_REC_W3;    /* 0x00E24C00 */
extern int8_t   NETWORK_$ACTIVITY_FLAG;     /* 0x00E24C42 */

void ASKNODE_$READ_FAILURE_REC(uint32_t *record)
{
    /*
     * If network activity flag is negative (indicating recent activity),
     * clear the second byte of the failure record.
     */
    if (NETWORK_$ACTIVITY_FLAG < 0) {
        NETWORK_$FAILURE_REC_BYTE2 = 0;
    }

    /*
     * Copy the 16-byte failure record to the output buffer.
     * The record consists of 4 32-bit words.
     */
    record[0] = NETWORK_$FAILURE_REC;
    record[1] = NETWORK_$FAILURE_REC_W1;
    record[2] = NETWORK_$FAILURE_REC_W2;
    record[3] = NETWORK_$FAILURE_REC_W3;
}
