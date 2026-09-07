/*
 * PROC2_$SUSPEND - Suspend a process
 *
 * Suspends the specified process.  Suspending the caller is a one-shot
 * operation; suspending another process may have to wait, because
 * PROC1_$SUSPEND only *requests* the suspension of a process that is
 * running or inhibited.  The wait is bounded by a 0x78-tick deadline on
 * TIME_$CLOCKH, after which the target is resumed again and
 * status_$proc2_suspend_timed_out is returned.
 *
 * Parameters:
 *   proc_uid   - Pointer to process UID to suspend
 *   status_ret - Returns status (0 on success)
 *
 * Original address: 0x00E4126A (366 bytes)
 * Nested Pascal procedure at 0x00E4120C (94 bytes)
 *
 * Frame slots of PROC2_$SUSPEND that the nested procedure reaches through
 * the static link (`movea.l (A6),A2` at 0x00E41214):
 *   (-0x04,A6) resume_status   (-0x08,A6) status
 *   (-0x10,A6) index           (-0x12,A6) suspend_result (a Domain boolean)
 *
 * Full instruction trace of PROC2_$SUSPEND:
 *   00e4126a  link.w A6,-0x14
 *   00e4126e  movem.l {A5 A4 A3 A2 D4 D3 D2},-(SP)
 *   00e41272  lea (0xe7be84).l,A5      ; PROC2 module data base (unused)
 *   00e4127a  movea.l (0x8,A6),A2      ; A2 = proc_uid
 *   00e41282  jsr 0x00e20b12.l         ; ML_$LOCK(4)
 *   00e4128a  pea (-0x8,A6) / pea (A2) / bsr.w 0x00e4068e  ; PROC2_$FIND_INDEX
 *   00e41296  move.w D0w,(-0x10,A6)    ; index
 *   00e4129a  tst.w (-0x6,A6)          ; LOW word of status
 *   00e4129e  bne.w 0x00e413bc
 *   00e412a2  move.w (0x00e20608).l,D1w  ; PROC1_$CURRENT
 *   00e412ae  mulu.w #0xe4,D1
 *   00e412b2  lea (A2),A3
 *   00e412b4  lea (0x0,A0,D1w),A1      ; 0xEA551C + pid*0xE4
 *   00e412b8  moveq #0x1,D1            ; dead: D1 is not read again
 *   00e412ba  lea (-0xe4,A1),A4        ; = &P2_INFO_TABLE[pid - 1]
 *   00e412be  cmpm.l (A4)+,(A3)+       ; compare the 8-byte UID
 *   00e412c2  cmpm.l (A4)+,(A3)+
 *   00e412c6  jsr 0x00e20b62.l         ; equal: ML_$UNLOCK(4) ...
 *   00e412d4  bsr.w 0x00e4120c         ; ... proc2_$suspend_try ...
 *   00e412d8  bra.w 0x00e413c8         ; ... and return without unlocking
 *   00e412dc  move.l (0x00e205f6).l,D3 ; PROC1_$SUSPEND_EC.value
 *   00e412e2  addq.l #0x1,D3           ; wait_val
 *   00e412e4  bsr.w 0x00e4120c         ; proc2_$suspend_try
 *   00e412e8  tst.b D0b / bpl.w 0x00e413bc
 *   00e412ee  movea.l #0xe2b0d4,A1 / movea.l A1,A3      ; &TIME_$CLOCKH
 *   00e412f6  movea.l #0x0,A1 / movea.l A1,A4           ; NULL
 *   00e412fe  movea.l #0xe2b0d4,A1 / move.l A1,D4       ; &TIME_$CLOCKH
 *   00e41306  bra.w 0x00e413b4         ; test at the bottom of the loop
 *   00e4130c  jsr 0x00e20b62.l         ; ML_$UNLOCK(4)
 *   00e4131a  clr.l -(SP)              ; vals[2] = 0
 *   00e4131c  moveq #0x78,D0 / add.l (A0),D0 / move.l D0,-(SP)
 *                                      ; vals[1] = TIME_$CLOCKH + 0x78
 *   00e41324  move.l D3,-(SP)          ; vals[0] = wait_val
 *   00e41326  pea (A4)                 ; ecs[2] = NULL
 *   00e41328  pea (A3)                 ; ecs[1] = &TIME_$CLOCKH
 *   00e4132a  move.l #0xe205f6,-(SP)   ; ecs[0] = &PROC1_$SUSPEND_EC
 *   00e41330  jsr 0x00e20610.l         ; EC_$WAIT
 *   00e41336  lea (0x18,SP),SP
 *   00e4133a  move.w D0w,D2w           ; 0-based index of the EC that fired
 *   00e4133c  jsr 0x00e20b12.l         ; ML_$LOCK(4)
 *   00e4134a  pea (-0x8,A6) / pea (A2) / bsr.w 0x00e4068e
 *   00e41356  move.w D0w,(-0x10,A6)
 *   00e4135a  tst.w (-0x6,A6) / bne.b 0x00e413bc
 *   00e41360  tst.w D2w / bne.b 0x00e41388
 *   00e41364  subq.l #0x2,SP / pea (-0x8,A6)
 *   00e4136a  muls.w #0xe4,D0
 *   00e41374  move.w (-0x4a,A0,D0),-(SP)  ; entry->level1_pid
 *   00e41378  jsr 0x00e14876.l         ; PROC1_$SUSPENDP
 *   00e41380  move.b D0b,(-0x12,A6)    ; suspend_result = its boolean result
 *   00e41384  addq.l #0x1,D3           ; wait_val++
 *   00e41386  bra.b 0x00e413b4
 *   00e41388  st (-0x12,A6)            ; timeout: suspend_result = TRUE
 *   00e4138c  subq.l #0x2,SP / pea (-0x4,A6)
 *   00e413a0  move.w (-0x4a,A0,D0),-(SP)
 *   00e413a4  jsr 0x00e1476e.l         ; PROC1_$RESUME
 *   00e413ac  move.l #0x190005,(-0x8,A6)  ; status_$proc2_suspend_timed_out
 *   00e413b4  tst.b (-0x12,A6) / bpl.w 0x00e4130c
 *   00e413bc  jsr 0x00e20b62.l         ; ML_$UNLOCK(4)
 *   00e413c8  movea.l (0xc,A6),A1 / move.l (-0x8,A6),(A1)
 *
 * Two things worth recording:
 *
 *  - The "is this me?" test at 0x00E412B4 indexes the process table with
 *    PROC1_$CURRENT (a level-1 PID) scaled by 0xE4, i.e. it evaluates
 *    P2_INFO_ENTRY(PROC1_$CURRENT).  Every other PROC2 routine maps the PID
 *    through P2_PID_TO_INDEX first.  This is reproduced as found.
 *
 *  - Every status test in this function and in its nested procedure is
 *    `tst.w (-0x6,An)`, the LOW word of the status longword, not the high
 *    word and not the whole thing.
 */

#include "proc2/proc2_internal.h"

/*
 * proc2_$suspend_try - the nested Pascal procedure at 0x00E4120C
 *
 * It takes no stack arguments; it reads and writes PROC2_$SUSPEND's frame
 * through the static link.  The flattening passes the three touched slots
 * by reference (index is only read, so it is passed by value).
 *
 * Returns the Domain boolean left in D0: TRUE (0xFF) if PROC1_$SUSPEND
 * reported no error, FALSE (0) otherwise.
 *
 *   00e41214  movea.l (A6),A2          ; A2 = PROC2_$SUSPEND's A6
 *   00e41216  st D2b                   ; result = TRUE
 *   00e41218  subq.l #0x2,SP
 *   00e4121a  pea (-0x8,A2)            ; &parent.status
 *   00e4121e  move.w (-0x10,A2),D0w    ; parent.index
 *   00e41228  mulu.w #0xe4,D0
 *   00e4122c  move.w (-0x4a,A0,D0w),-(SP)  ; entry->level1_pid (+0x9A)
 *   00e41230  jsr 0x00e147fa.l         ; PROC1_$SUSPEND
 *   00e41238  move.b D0b,(-0x12,A2)    ; parent.suspend_result
 *   00e4123c  tst.w (-0x6,A2) / beq.b 0x00e4125e
 *   00e41242  cmpi.l #0xa0004,(-0x8,A2)
 *   00e4124c  move.l #0x190007,(-0x8,A2)   ; status_$proc2_already_suspended
 *   00e41256  bset.b #0x7,(-0x8,A2)        ; else set bit 31 ("fatal")
 *   00e4125c  clr.b D2b                    ; result = FALSE
 */
static int8_t proc2_$suspend_try(int16_t index, status_$t *status,
                                 int8_t *suspend_result)
{
    proc2_info_t *info;
    int8_t result = (int8_t)0xFF;   /* 0x00E41216: st D2b */

    /* 0x00E41222 */
    info = P2_INFO_ENTRY(index);

    /* 0x00E41230 */
    *suspend_result = PROC1_$SUSPEND(info->level1_pid, status);

    /* 0x00E4123C: the LOW word of the status */
    if ((*status & 0xFFFF) != 0) {
        if (*status == status_$process_already_suspended) {
            /* 0x00E4124C */
            *status = status_$proc2_already_suspended;
        } else {
            /* 0x00E41256: bset.b #0x7 on the first byte = bit 31 */
            *status |= (status_$t)0x80000000u;
        }

        /* 0x00E4125C */
        result = 0;
    }

    return result;
}

void PROC2_$SUSPEND(uid_t *proc_uid, status_$t *status_ret)
{
    proc2_info_t *info;
    proc2_info_t *self;
    status_$t status;
    status_$t resume_status;
    int16_t index;
    int16_t wait_result;
    int8_t suspend_result;
    int32_t wait_val;

    /* 0x00E41282 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E41290 */
    index = PROC2_$FIND_INDEX(proc_uid, &status);

    /* 0x00E4129A */
    if ((status & 0xFFFF) == 0) {
        /*
         * 0x00E412B4/0x00E412BA: indexed by the level-1 PID, not by
         * P2_PID_TO_INDEX(PROC1_$CURRENT); see the note in the header.
         */
        self = P2_INFO_ENTRY(PROC1_$CURRENT);

        /* 0x00E412BE/0x00E412C2: the whole 8-byte UID */
        if (proc_uid->high == self->uid.high &&
            proc_uid->low == self->uid.low) {
            /*
             * 0x00E412C6: suspending ourselves.  The lock is dropped first
             * because PROC1_$SUSPEND will not return until we are resumed,
             * and the shared unlock at 0x00E413BC is skipped.
             */
            ML_$UNLOCK(PROC2_LOCK_ID);
            (void)proc2_$suspend_try(index, &status, &suspend_result);
            *status_ret = status;
            return;
        }

        /* 0x00E412DC */
        wait_val = PROC1_$SUSPEND_EC.value + 1;

        /* 0x00E412E4/0x00E412E8 */
        if (proc2_$suspend_try(index, &status, &suspend_result) < 0) {
            /* 0x00E41306: the loop test is at the bottom, entered first */
            while (suspend_result >= 0) {
                /* 0x00E4130C */
                ML_$UNLOCK(PROC2_LOCK_ID);

                /*
                 * 0x00E4131A-0x00E41336: two 3-element arrays by value,
                 * 24 bytes, cleaned with `lea (0x18,SP),SP`.  The clock
                 * eventcount supplies the 0x78-tick deadline.
                 */
                wait_result = EC_$WAIT(
                    (ec_$wait_ecs_t){{ &PROC1_$SUSPEND_EC,
                                       (ec_$eventcount_t *)&TIME_$CLOCKH,
                                       NULL }},
                    (ec_$wait_vals_t){{ wait_val,
                                        (int32_t)(TIME_$CLOCKH + 0x78),
                                        0 }});

                /* 0x00E4133C */
                ML_$LOCK(PROC2_LOCK_ID);

                /* 0x00E41350: the process may have died while we waited */
                index = PROC2_$FIND_INDEX(proc_uid, &status);

                /* 0x00E4135A */
                if ((status & 0xFFFF) != 0) {
                    break;
                }

                info = P2_INFO_ENTRY(index);

                /* 0x00E41360: EC_$WAIT's result is 0-based */
                if (wait_result == 0) {
                    /*
                     * 0x00E41378: PROC1_$SUSPENDP reports whether the
                     * target has now actually stopped; its boolean result
                     * becomes the loop condition.
                     */
                    suspend_result = PROC1_$SUSPENDP(info->level1_pid,
                                                     &status);

                    /* 0x00E41384 */
                    wait_val++;
                } else {
                    /* 0x00E41388: TRUE ends the loop */
                    suspend_result = (int8_t)0xFF;

                    /* 0x00E413A4 */
                    PROC1_$RESUME(info->level1_pid, &resume_status);

                    /* 0x00E413AC */
                    status = status_$proc2_suspend_timed_out;
                }
            }
        }
    }

    /* 0x00E413BC */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E413C8 */
    *status_ret = status;
}
