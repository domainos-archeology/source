/*
 * smd/dm_cond_event_wait.c - SMD_$DM_COND_EVENT_WAIT implementation
 *
 * Conditional wait for display manager events. Checks multiple event
 * sources including display units, power off, and request queues.
 *
 * Original address: 0x00E6EFF0
 *
 * Assembly (every basic block accounted for):
 *   00e6eff0    link.w A6,-0x24
 *   00e6eff4    movem.l {  A5 A4 A3 A2 D6 D5 D4 D3 D2},-(SP)
 *   00e6eff8    lea (0xe82b8c).l,A5             ; A5 = &SMD_GLOBALS
 *   00e6effe    move.l (0x8,A6),D4              ; D4 = event_type
 *   00e6f002    move.l (0xc,A6),D5              ; D5 = param2
 *   00e6f006    movea.l (0x14,A6),A0            ; A0 = status_ret
 *   00e6f00a    clr.l (A0)
 *   00e6f00c    move.w (0x00e2060a).l,D2w       ; D2 = PROC1_$AS_ID
 *   00e6f012    clr.w D0w                       ; dbf count 0 -> 1 iteration
 *   00e6f014    movea.l #0xe2e3fc,A1
 *   00e6f01a    moveq #0x1,D1                   ; D1 = unit number, 1-based
 *   00e6f01c    lea (0x10c,A1),A1               ; A1 = biased record for unit 1
 *   00e6f020    movea.l A1,A0
 *   00e6f022    move.l (-0xf4,A0),D3            ; D3 = rec->hw
 *   00e6f026    movea.l D3,A2
 *   00e6f028    tst.w (0x4c,A2)                 ; hw->field_4c == 0 ?
 *   00e6f02c    beq.b 0x00e6f064
 *   00e6f02e    cmp.w (-0xf0,A0),D2w            ; rec->owner_asid == asid ?
 *   00e6f032    bne.b 0x00e6f064
 *   00e6f034    move.w (0x4c,A2),D6w
 *   00e6f038    btst.l #0xe,D6                  ; bit 14
 *   00e6f03c    beq.b 0x00e6f04c
 *   00e6f03e    movea.l D4,A3
 *   00e6f040    move.w #0x4,(A3)                ; *event_type = 4
 *   00e6f044    bclr.b #0x6,(0x4c,A2)           ; high byte bit 6 = word bit 14
 *   00e6f04a    bra.b 0x00e6f05c
 *   00e6f04c    tst.w D6w
 *   00e6f04e    bpl.b 0x00e6f05c                ; neither bit -> *event_type
 *                                               ;                 left alone
 *   00e6f050    movea.l D4,A3
 *   00e6f052    move.w #0x3,(A3)                ; *event_type = 3
 *   00e6f056    bclr.b #0x7,(0x4c,A2)           ; high byte bit 7 = word bit 15
 *   00e6f05c    movea.l D5,A3
 *   00e6f05e    move.w D1w,(A3)                 ; *param2 = unit number
 *   00e6f060    bra.w 0x00e6f188                ; return
 *   00e6f064    addq.w #0x1,D1w
 *   00e6f066    lea (0x10c,A1),A1
 *   00e6f06a    dbf D0w,0x00e6f020
 *   00e6f06e    jsr 0x00e2428c.l                ; MMU_$POWER_OFF
 *   00e6f074    tst.b D0b
 *   00e6f076    bpl.b 0x00e6f08c
 *   00e6f078    tst.b (0x1da2,A5)               ; power_off_reported
 *   00e6f07c    bmi.b 0x00e6f08c
 *   00e6f07e    movea.l D4,A1
 *   00e6f080    move.w #0x6,(A1)                ; *event_type = 6
 *   00e6f084    move.b D0b,(0x1da2,A5)
 *   00e6f088    bra.w 0x00e6f188                ; return
 *   00e6f08c    move.b D0b,(0x1da2,A5)
 *   00e6f090    move.w (0x17f2,A5),D1w          ; request_queue_head
 *   00e6f094    cmp.w (0x17f0,A5),D1w           ; == request_queue_tail ?
 *   00e6f098    beq.w 0x00e6f134
 *   00e6f09c    movea.l D4,A1
 *   00e6f09e    move.w #0x1,(A1)                ; *event_type = 1
 *   00e6f0a2    subq.l #0x2,SP
 *   00e6f0a4    move.w #0x8,-(SP)
 *   00e6f0a8    jsr 0x00e20b12.l                ; ML_$LOCK(8)
 *   00e6f0ae    addq.w #0x4,SP
 *   00e6f0b0    move.w (0x17f2,A5),D1w
 *   00e6f0b4    cmp.w (0x17f0,A5),D1w           ; re-test under the lock
 *   00e6f0b8    beq.b 0x00e6f126
 *   00e6f0ba    move.w (0x17f0,A5),D1w          ; D1 = tail (read index)
 *   00e6f0be    movea.l D5,A1                   ; A1 = param2
 *   00e6f0c0    ext.l D1
 *   00e6f0c2    lsl.l #0x2,D1                   ; D1 = tail*4
 *   00e6f0c4    move.l D1,D0
 *   00e6f0c6    lsl.l #0x3,D0                   ; D0 = tail*32
 *   00e6f0c8    add.l D0,D1                     ; D1 = tail*36
 *   00e6f0ca    lea (0x0,A5,D1*0x1),A0
 *   00e6f0ce    move.w (0x17d0,A0),(A1)         ; *param2 = entry->request_type
 *                                               ; entry = globals+0x17D0+tail*36
 *                                               ;       = request_queue[tail] (declared at 0x17D0)
 *   00e6f0d2    movea.l (0x10,A6),A3            ; A3 = param3
 *   00e6f0d6    move.w (0x17d2,A0),(A3)         ; *param3 = entry->param_count
 *   00e6f0da    move.w (A3),D0w
 *   00e6f0dc    subq.w #0x1,D0w
 *   00e6f0de    bmi.b 0x00e6f0f6                ; no parameters
 *   00e6f0e0    moveq #0x4,D1
 *   00e6f0e2    addq.l #0x4,A1                  ; A1 = param2 + 4
 *   00e6f0e4    lea (0x0,A0,D1*0x1),A4
 *   00e6f0e8    move.w (0x17d0,A4),(-0x2,A1)    ; param2[1+k] = entry->params[k]
 *   00e6f0ee    addq.l #0x2,A1
 *   00e6f0f0    addq.l #0x2,D1
 *   00e6f0f2    dbf D0w,0x00e6f0e4
 *   00e6f0f6    cmpi.w #0x28,(0x17f0,A5)
 *   00e6f0fc    blt.b 0x00e6f106
 *   00e6f0fe    move.w #0x1,(0x17f0,A5)         ; wrap the read index to 1
 *   00e6f104    bra.b 0x00e6f10a
 *   00e6f106    addq.w #0x1,(0x17f0,A5)
 *   00e6f10a    subq.l #0x2,SP
 *   00e6f10c    move.w #0x8,-(SP)
 *   00e6f110    jsr 0x00e20b62.l                ; ML_$UNLOCK(8)
 *   00e6f116    addq.w #0x4,SP
 *   00e6f118    move.l #0xe2e3fc,-(SP)          ; &SMD_$WIRED_DATA.ec_1 by value
 *   00e6f11e    jsr 0x00e206ee.l                ; EC_$ADVANCE
 *   00e6f124    bra.b 0x00e6f188                ; return
 *   00e6f126    subq.l #0x2,SP
 *   00e6f128    move.w #0x8,-(SP)
 *   00e6f12c    jsr 0x00e20b62.l                ; ML_$UNLOCK(8)
 *   00e6f132    addq.w #0x4,SP
 *   00e6f134    move.w D2w,D1w
 *   00e6f136    add.w D1w,D1w
 *   00e6f138    move.w (0x48,A5,D1w*0x1),D0w    ; asid_to_unit[asid]
 *   00e6f13c    bne.b 0x00e6f14a
 *   00e6f13e    movea.l (0x14,A6),A0
 *   00e6f142    move.l #0x130004,(A0)           ; invalid use of driver proc
 *   00e6f148    bra.b 0x00e6f188
 *   00e6f14a    move.w D0w,D1w
 *   00e6f14c    movea.l #0xe2e3fc,A1
 *   00e6f152    mulu.w #0x10c,D1
 *   00e6f156    lea (0x0,A1,D1*0x1),A1          ; A1 = biased unit record
 *   00e6f15a    movea.l (-0xf4,A1),A0           ; A0 = rec->hw
 *   00e6f15e    cmp.w (-0xec,A1),D2w            ; rec->field_08 == asid ?
 *   00e6f162    bne.b 0x00e6f182
 *   00e6f164    move.l (0x1c,A0),D1             ; hw->field_1c
 *   00e6f168    addq.l #0x1,D1
 *   00e6f16a    cmp.l (0x10,A0),D1              ; > hw->op_ec.value ?
 *   00e6f16e    bgt.b 0x00e6f182
 *   00e6f170    movea.l D4,A3
 *   00e6f172    move.w #0x2,(A3)                ; *event_type = 2
 *   00e6f176    move.l (0x10,A0),(0x1c,A0)      ; hw->field_1c = op_ec.value
 *   00e6f17c    clr.w (-0xec,A1)                ; rec->field_08 = 0
 *   00e6f180    bra.b 0x00e6f188
 *   00e6f182    movea.l D4,A3
 *   00e6f184    move.w #0x9,(A3)                ; *event_type = 9
 *   00e6f188    movem.l (-0x48,A6),{  D2 D3 D4 D5 D6 A2 A3 A4 A5}
 *   00e6f18e    unlk A6
 *   00e6f190    rts
 */

#include "smd/smd_internal.h"
#include "ec/ec.h"
#include "ml/ml.h"
#include "mmu/mmu.h"

/* hw->field_4c flag bits, numbered within the 16-bit word.  The original
 * clears them with byte operations on the *high* byte of the word
 * (bclr.b #6 / #7 at 0x00E6F044 / 0x00E6F056), i.e. word bits 14 and 15. */
#define SMD_HW_EVENT_TRACKING 0x4000u
#define SMD_HW_EVENT_CURSOR 0x8000u

/*
 * SMD_$DM_COND_EVENT_WAIT - Conditional event wait for display manager
 *
 * Checks multiple event sources and returns as soon as one has an event.
 *
 * Parameters:
 *   event_type  - Output: receives event type code
 *   param2      - Output: unit number, or the request record (request_type
 *                 in word 0, the request's parameters from word 1 on)
 *   param3      - Output: receives the request's parameter count
 *   status_ret  - Output: receives status code
 *
 * Event types returned:
 *   1 = Request queue message available
 *   2 = Display buffer event
 *   3 = Display unit event (cursor flag set)
 *   4 = Display unit event (tracking flag set)
 *   6 = Power off event
 *   9 = No event
 *
 * NOTE: when a unit's field_4c is non-zero but neither the cursor nor the
 * tracking bit is set, the original writes the unit number to *param2 and
 * returns *without touching* *event_type; that path is preserved below.
 */
void SMD_$DM_COND_EVENT_WAIT(uint16_t *event_type, int16_t *param2,
                             int16_t *param3, status_$t *status_ret)
{
    uint16_t asid;
    int16_t counter;
    int16_t unit_num;
    smd_display_unit_t *rec;
    smd_display_hw_t *hw;
    uint16_t hw_flags;
    int8_t power_status;
    smd_request_entry_t *req_entry;
    int16_t param_idx;

    /* 0x00e6f00a */
    *status_ret = status_$ok;

    /* 0x00e6f00c */
    asid = PROC1_$AS_ID;

    /*
     * 0x00e6f012-0x00e6f06a: scan the display units.  "clr.w D0w" plus a
     * single dbf makes this exactly one iteration - unit 1 - and A1 starts at
     * base + 1*0x10C, the biased record pointer.
     */
    counter = 0;
    unit_num = 1;
    while (1) {
        rec = smd_$unit_rec(unit_num);
        hw = rec->hw;

        /* 0x00e6f028 / 0x00e6f02e */
        if (hw->field_4c != 0 && asid == rec->owner_asid) {
            hw_flags = hw->field_4c;

            if ((hw_flags & SMD_HW_EVENT_TRACKING) != 0) {
                /* 0x00e6f040 / 0x00e6f044 */
                *event_type = 4;
                hw->field_4c = (uint16_t)(hw->field_4c & ~SMD_HW_EVENT_TRACKING);
            } else if ((int16_t)hw_flags < 0) {
                /* 0x00e6f052 / 0x00e6f056 */
                *event_type = 3;
                hw->field_4c = (uint16_t)(hw->field_4c & ~SMD_HW_EVENT_CURSOR);
            }
            /* else: *event_type is deliberately left as the caller set it */

            /* 0x00e6f05e: reached from all three arms */
            *param2 = unit_num;
            return;
        }

        /* 0x00e6f064-0x00e6f06a */
        unit_num++;
        counter--;
        if (counter == -1) {
            break;
        }
    }

    /* 0x00e6f06e-0x00e6f08c: power off */
    power_status = MMU_$POWER_OFF();
    if (power_status < 0 && SMD_GLOBALS.power_off_reported >= 0) {
        *event_type = SMD_EVTYPE_POWER_OFF;
        SMD_GLOBALS.power_off_reported = power_status;
        return;
    }
    SMD_GLOBALS.power_off_reported = power_status;

    /* 0x00e6f090-0x00e6f098: anything queued? */
    if (SMD_GLOBALS.request_queue_head != SMD_GLOBALS.request_queue_tail) {
        /* 0x00e6f09e: set before the lock is taken, and not undone if the
         * queue turns out to be empty after all. */
        *event_type = 1;

        ML_$LOCK(SMD_REQUEST_LOCK);

        if (SMD_GLOBALS.request_queue_head != SMD_GLOBALS.request_queue_tail) {
            /* 0x00e6f0ba-0x00e6f0ca: the entry index is 1-based
             * (A5 + 0x17D0 + tail*36; request_queue is declared at 0x17D0). */
            req_entry = &SMD_GLOBALS.request_queue[SMD_GLOBALS.request_queue_tail];

            *param2 = (int16_t)req_entry->request_type;
            *param3 = (int16_t)req_entry->param_count;

            /* 0x00e6f0da-0x00e6f0f2: the parameters follow the request type
             * in the caller's record, i.e. at param2[1], param2[2], ... */
            param_idx = (int16_t)(*param3 - 1);
            if (param_idx >= 0) {
                int16_t k = 0;
                do {
                    param2[1 + k] = (int16_t)req_entry->params[k];
                    k++;
                    param_idx--;
                } while (param_idx >= 0);
            }

            /* 0x00e6f0f6-0x00e6f106: advance the 1-based read index. */
            if (SMD_GLOBALS.request_queue_tail >= SMD_REQUEST_QUEUE_MAX) {
                SMD_GLOBALS.request_queue_tail = 1;
            } else {
                SMD_GLOBALS.request_queue_tail++;
            }

            ML_$UNLOCK(SMD_REQUEST_LOCK);

            /* 0x00e6f118 */
            EC_$ADVANCE(&SMD_$WIRED_DATA.ec_1);
            return;
        }

        /* 0x00e6f126 */
        ML_$UNLOCK(SMD_REQUEST_LOCK);
    }

    /* 0x00e6f134-0x00e6f142 */
    if (SMD_GLOBALS.asid_to_unit[asid] == 0) {
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /* 0x00e6f14a-0x00e6f184 */
    {
        smd_display_unit_t *disp_rec =
            smd_$unit_rec((int16_t)SMD_GLOBALS.asid_to_unit[asid]);
        smd_display_hw_t *disp_hw = disp_rec->hw;

        if (asid == disp_rec->field_08 &&
            (int32_t)(disp_hw->field_1c + 1) <= disp_hw->op_ec.value) {
            /* Buffer operation completed */
            *event_type = 2;
            disp_hw->field_1c = (uint32_t)disp_hw->op_ec.value;
            disp_rec->field_08 = 0;
        } else {
            *event_type = SMD_EVTYPE_SIGNAL;
        }
    }
}
