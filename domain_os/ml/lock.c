/*
 * ML_$LOCK - Acquire a resource lock
 *
 * Acquires a resource lock, blocking the calling process if the lock is
 * already held.  Lock ordering is enforced by PROC1_$SET_LOCK.
 *
 * Original address: 0x00E20B12 (68 bytes).
 *
 * Full instruction trace:
 *   00e20b12  move.w (0x4,SP),D0w            ; D0 = resource_id
 *   00e20b16  bsr.b 0x00e20ae8               ; proc1_$set_lock_body
 *                                            ; (the body of PROC1_$SET_LOCK,
 *                                            ;  whose gate at 0x00E20AE4 is
 *                                            ;  exactly the move.w above)
 * .Lretry:
 *   00e20b18  lea (0xaa,PC),A0               ; A0 = 0xE20B1A+0xAA = 0xE20BC4
 *                                            ;    = ML_$LOCK_BYTES
 *   00e20b1c  move.w (0x4,SP),D0w            ; reload resource_id
 *   00e20b20  ori #0x700,SR                  ; raise to IPL 7 (no SR saved)
 *   00e20b24  bset.b #0x0,(0x0,A0,D0w*0x1)   ; test-and-set the lock byte
 *   00e20b2a  bne.b 0x00e20b32               ; already held -> wait
 *   00e20b2c  andi #-0x701,SR                ; forced IPL 0 (NOT an SR restore)
 *   00e20b30  rts
 *   00e20b32  move.l A3,-(SP)
 *   00e20b34  move.l A4,-(SP)
 *   00e20b36  lsl.w #0x4,D0w                 ; 16-byte stride into LOCK_EVENTS
 *   00e20b38  addq.l #0x1,(0x2c,A0,D0w*0x1)  ; ++ev->wait_count
 *   00e20b3c  move.l (0x2c,A0,D0w*0x1),-(SP) ; wait_vals[0] = ev->wait_count
 *   00e20b40  lea (SP),A3                    ; A3 = wait_vals
 *   00e20b42  pea (0x20,A0,D0w*0x1)          ; ec_list[0] = &ev->ec
 *   00e20b46  lea (SP),A4                    ; A4 = ec_list
 *   00e20b48  moveq #0x1,D0                  ; num_ecs = 1
 *   00e20b4a  bsr.w 0x00e2065a               ; PROC1_$EC_WAITN (A1 = pcb)
 *   00e20b4e  addq.w #0x8,SP
 *   00e20b50  movea.l (SP)+,A4
 *   00e20b52  movea.l (SP)+,A3
 *   00e20b54  bra.b 0x00e20b18               ; retry (does NOT re-enter set_lock)
 *
 * Note that the retry branch targets 0x00E20B18, i.e. the lock-ordering /
 * PCB bookkeeping at 0x00E20AE8 runs exactly once per ML_$LOCK call.
 *
 * Interrupts: the acquire path raises to IPL 7 and leaves through a forced
 * IPL 0; there is no saved SR to restore.  The contended path keeps IPL 7
 * across PROC1_$EC_WAITN (the dispatcher lowers it when the process runs
 * again) and 0x00E20B18 re-raises on every retry.
 */

#include "ml/ml_internal.h"

void ML_$LOCK(int16_t resource_id)
{
    proc1_t *pcb;
    ec_$eventcount_t *ec_list[1];
    int32_t wait_vals[1];
    uint8_t old_lock_byte;
    ml_$lock_event_t *evp;

    /*
     * 0x00E20B12/0x00E20B16: the gate PROC1_$SET_LOCK at 0x00E20AE4 is the
     * single instruction `move.w (0x4,SP),D0w` falling through into the body
     * at 0x00E20AE8; ML_$LOCK loads D0 itself and `bsr`s straight to the
     * body.  Calling the gate here is the same code with the same argument.
     * The body leaves PROC1_$CURRENT_PCB in A1, which ML_$LOCK then uses.
     */
    PROC1_$SET_LOCK((uint16_t)resource_id);
    pcb = PROC1_$CURRENT_PCB;

    for (;;) {
        /* 0x00E20B20: ori #0x700,SR -- no SR is saved */
        SET_IPL7();

        /* 0x00E20B24: bset.b #0,(0,A0,D0w) -- test-and-set bit 0 */
        old_lock_byte = ML_$LOCK_BYTES[resource_id];
        ML_$LOCK_BYTES[resource_id] = (uint8_t)(old_lock_byte | 0x01);

        if ((old_lock_byte & 0x01) == 0) {
            /* 0x00E20B2C: andi #-0x701,SR -- forced IPL 0, then rts */
            SET_IPL0();
            return;
        }

        /*
         * 0x00E20B32..0x00E20B4A: contended.  Bump the lock's wait counter
         * and sleep on the lock's event count until it catches up.
         * Interrupts stay at IPL 7 across the call.
         */
        evp = &ML_$LOCK_EVENTS[resource_id];

        evp->wait_count++;                  /* 0x00E20B38 */
        wait_vals[0] = evp->wait_count;     /* 0x00E20B3C */
        ec_list[0] = &evp->ec;              /* 0x00E20B42 */

        PROC1_$EC_WAITN(pcb, ec_list, wait_vals, 1);

        /* 0x00E20B54: bra.b 0x00E20B18 -- retry the acquire */
    }
}
