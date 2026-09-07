/*
 * PROC2_$ACKNOWLEDGE - Acknowledge signal delivery
 *
 * Called by the fault-interception layer when a signal handler has run.
 * It installs a new blocked-signal mask, clears the acknowledged signal
 * from the per-process masks, and -- for a job-control signal -- either
 * kills the process (when it has no job-control-capable parent) or stops
 * it and notifies the parent.
 *
 * Parameters:
 *   new_blocked - Pointer to the new blocked-signal mask (entry+0x78)
 *   signal      - Pointer to the signal number being acknowledged
 *   result      - Two longwords:
 *                   result[0] = the blocked mask now in effect (entry+0x78)
 *                   result[1] = 1 if flags bit 10 (0x0400) is set, else 0
 *
 * Original address: 0x00E3F338 (488 bytes)
 *
 * Full instruction trace (A4 = P2_INFO_ENTRY(cur_idx) + 0xE4 throughout, so
 * a displacement d means entry offset 0xE4 + d):
 *   00e3f338  link.w A6,-0x30
 *   00e3f33c  movem.l {A5 A4 A3 A2 D3 D2},-(SP)
 *   00e3f340  lea (0xe7be84).l,A5      ; PROC2 module data base (unused)
 *   00e3f346  movea.l (0xc,A6),A2      ; A2 = signal
 *   00e3f34a  movea.l (0x10,A6),A3     ; A3 = result
 *   00e3f34e  move.w (0x00e20608).l,D0w
 *   00e3f35a  add.w D0w,D0w
 *   00e3f35c  move.w (A2),D3w          ; *signal
 *   00e3f362  subq.w #0x1,D3w
 *   00e3f364  move.w (0x3eb6,A1),D0w   ; cur_idx = P2_PID_TO_INDEX[pid]
 *   00e3f36c  clr.l D3
 *   00e3f372  bset.l D1,D3             ; sig_mask = 1 << ((*signal - 1) & 31)
 *   00e3f378  muls.w #0xe4,D1
 *   00e3f37c  lea (0x0,A0,D1),A4
 *   00e3f384  jsr 0x00e20b12.l         ; ML_$LOCK(4)
 *   00e3f38c  movea.l (0x8,A6),A0
 *   00e3f390  move.l (A0),(-0x6c,A4)   ; entry->sig_blocked_2 = *new_blocked
 *   00e3f394  move.l (-0x64,A4),D1 / not.l D1 / and.l D3,D1   ; sig_mask_2
 *   00e3f39c  bne.b 0x00e3f3aa
 *   00e3f39e  move.l D3,D1 / not.l D1 / and.l D1,(-0x64,A4)
 *   00e3f3a6  st D2b                   ; was_set = TRUE
 *   00e3f3aa  clr.b D2b                ; was_set = FALSE
 *   00e3f3ac  move.l (-0x68,A4),D1 / not.l D1 / and.l D3,D1   ; sig_mask_3
 *   00e3f3b4  bne.b 0x00e3f3bc
 *   00e3f3b6  bset.b #0x2,(-0xba,A4)   ; flags |= 0x0400 (HIGH byte, bit 2)
 *   00e3f3bc  jsr 0x00e0a96c.l         ; FIM_$ACKNOWLEDGE()
 *   00e3f3c4  andi.l #-0x1980001,D1    ; sig_mask & 0xFE67FFFF
 *   00e3f3ca  bne.w 0x00e3f4d0
 *   00e3f3ce  move.l (-0x74,A4),D1 / not.l D1 / and.l D3,D1   ; sig_pending
 *   00e3f3d6  beq.w 0x00e3f4d0
 *   00e3f3da  tst.b D2b / bmi.b 0x00e3f3e8
 *   00e3f3de  bclr.b #0x5,(-0x63,A4)   ; sig_mask_2 &= ~(1 << 21)
 *   00e3f3e4  bra.w 0x00e3f4d0
 *   00e3f3e8  move.w (-0xd4,A4),D1w    ; entry->pgroup_table_idx (+0x10)
 *   00e3f3f2  lsl.w #0x3,D1w
 *   00e3f3f8  tst.w (0x3f32,A1)        ; PGROUP_ENTRY(pg)->leader_count
 *   00e3f3fc  beq.b 0x00e3f40a
 *   00e3f3fe  tst.w (-0xba,A4) / bmi.b 0x00e3f40a       ; flags bit 15
 *   00e3f404  tst.w (-0xd4,A4) / bne.b 0x00e3f434
 *   00e3f40a  btst.l #0x13,D3 / bne.b 0x00e3f434        ; signal 20 (SIGCONT)
 *   00e3f410  move.l #0x9010009,(-0x20,A6)
 *   00e3f418  pea (-0x1c,A6) / move.l (-0x20,A6),-(SP)
 *   00e3f420  move.w #0x9,-(SP)        ; SIGKILL
 *   00e3f424  move.w (-0xc8,A4),-(SP)  ; entry+0x1C, our own index
 *   00e3f428  bsr.w 0x00e3eb8c         ; PROC2_$DELIVER_SIGNAL_INTERNAL
 *   00e3f430  bra.w 0x00e3f4d0
 *   00e3f434  move.w (A2),(-0x50,A4)   ; entry->pad_94 = *signal
 *   00e3f438  andi.w #-0x31,(-0xba,A4) ; flags &= 0xFFCF
 *   00e3f43e  bset.b #0x6,(-0xb9,A4)   ; flags |= 0x0040 (LOW byte, bit 6)
 *   00e3f444  move.l #0x9010017,(-0x20,A6)
 *   00e3f44c  tst.w (-0xba,A4) / bmi.b 0x00e3f4a2
 *   00e3f452  move.w (-0xc6,A4),D1w    ; entry->parent_pgroup_idx (+0x1E)
 *   00e3f456  movea.l #0xe2b978,A1     ; PROC2_$EC
 *   00e3f45c  lsl.w #0x3,D1w / add.w / add.w            ; *24
 *   00e3f464  pea (-0xc,A1,D1w)        ; &PROC2_$EC[idx-1].cr_rec_ec
 *   00e3f468  jsr 0x00e206ee.l         ; EC_$ADVANCE
 *   00e3f470  move.w (-0xc6,A4),D0w
 *   00e3f47a  mulu.w #0xe4,D0
 *   00e3f482  btst.b #0x2,(-0xb9,A0)   ; parent flags LOW byte, bit 2 = 0x0004
 *   00e3f488  bne.b 0x00e3f4a2
 *   00e3f48a  pea (-0x1c,A6) / move.l (-0x20,A6),-(SP)
 *   00e3f492  move.w #0x17,-(SP)       ; SIGCHLD (23)
 *   00e3f496  move.w (-0xc6,A4),-(SP)
 *   00e3f49a  bsr.w 0x00e3eb8c         ; PROC2_$DELIVER_SIGNAL_INTERNAL
 *   00e3f4a2  subq.l #0x2,SP / pea (-0x1c,A6)
 *   00e3f4a8  move.w (-0x4a,A4),-(SP)  ; entry->level1_pid (+0x9A)
 *   00e3f4ac  jsr 0x00e147fa.l         ; PROC1_$SUSPEND -- stops us here
 *   00e3f4b4  jsr 0x00e20b62.l         ; ML_$UNLOCK(4)
 *   00e3f4c2  jsr 0x00e20b12.l         ; ML_$LOCK(4)
 *   00e3f4d0  move.l (-0x60,A4),D1 / not.l D1 / and.l D3,D1   ; sig_mask_1
 *   00e3f4d8  bne.b 0x00e3f4e2
 *   00e3f4da  move.l D3,D1 / not.l D1 / and.l D1,(-0x74,A4)   ; sig_pending
 *   00e3f4e2  subq.l #0x2,SP / move.w (-0xc8,A4),-(SP)
 *   00e3f4e8  bsr.w 0x00e3ecea         ; PROC2_$DELIVER_PENDING_INTERNAL
 *   00e3f4ee  jsr 0x00e20b62.l         ; ML_$UNLOCK(4)
 *   00e3f4fc  move.l (-0x6c,A4),(A3)   ; result[0]
 *   00e3f500  move.w (-0xba,A4),D1w / btst.l #0xa,D1
 *   00e3f50a  moveq #0x1,D1 / move.l D1,(0x4,A3)
 *   00e3f512  clr.l (0x4,A3)
 *
 * Notes:
 *  - The lock is dropped and retaken AFTER PROC1_$SUSPEND returns
 *    (0x00E3F4B4 / 0x00E3F4C2), i.e. the process sits stopped while still
 *    holding PROC2's lock 4.  Reproduced as found.
 *  - `bclr.b #0x5,(-0x63,A4)` at 0x00E3F3DE addresses the SECOND byte of
 *    the longword at entry+0x80, so it clears bit 21 of sig_mask_2 (the
 *    bit for signal 22, SIGTTOU), not a bit of the flags word.
 *  - The two `pea (-0x1c,A6)` status arguments are never examined.
 */

#include "proc2/proc2_internal.h"
#include "fim/fim.h"

/*
 * 0x00E3F3C4: andi.l #-0x1980001,D1 (= 0xFE67FFFF).  A signal whose bit
 * survives this mask is NOT job-control related and takes the short path to
 * the tail.  The complement is 0x01980000: bits 19, 20, 23 and 24, i.e.
 * signals 20, 21, 24 and 25.
 */
#define SIGNAL_NON_JOB_CONTROL_MASK 0xFE67FFFFu

/* 0x00E3F40A: btst.l #0x13,D3 -- bit 19 = signal 20, SIGCONT. */
#define SIG_MASK_SIGCONT 0x00080000u

/* 0x00E3F3DE: bclr.b #0x5,(-0x63,A4) -- bit 21 of the longword at +0x80. */
#define SIG_MASK_SIGTTOU 0x00200000u

void PROC2_$ACKNOWLEDGE(uint32_t *new_blocked, int16_t *signal,
                        uint32_t *result)
{
    proc2_info_t *entry;
    proc2_info_t *parent;
    int16_t cur_idx;
    int16_t parent_idx;
    uint32_t sig_mask;
    int8_t was_set;
    status_$t status;
    uint32_t deliver_param;

    /* 0x00E3F362-0x00E3F372: bset.l takes the bit number modulo 32 */
    sig_mask = 1u << ((uint16_t)(*signal - 1) & 0x1F);

    /* 0x00E3F364 */
    cur_idx = (int16_t)P2_PID_TO_INDEX(PROC1_$CURRENT);
    entry = P2_INFO_ENTRY(cur_idx);

    /* 0x00E3F384 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E3F390 */
    entry->sig_blocked_2 = *new_blocked;

    /* 0x00E3F394 */
    if ((sig_mask & ~entry->sig_mask_2) == 0) {
        entry->sig_mask_2 &= ~sig_mask;
        was_set = (int8_t)0xFF;
    } else {
        was_set = 0;
    }

    /* 0x00E3F3AC */
    if ((sig_mask & ~entry->sig_mask_3) == 0) {
        /* 0x00E3F3B6: bset.b #0x2 on the flags word's HIGH byte */
        entry->flags |= 0x0400;
    }

    /* 0x00E3F3BC */
    FIM_$ACKNOWLEDGE();

    /* 0x00E3F3C4 */
    if ((sig_mask & SIGNAL_NON_JOB_CONTROL_MASK) == 0 &&
        (sig_mask & ~entry->sig_pending) != 0) {

        /* 0x00E3F3DA */
        if (was_set >= 0) {
            /* 0x00E3F3DE */
            entry->sig_mask_2 &= ~SIG_MASK_SIGTTOU;
        } else {
            int send_kill;

            /* 0x00E3F3E8-0x00E3F408 */
            send_kill =
                (PGROUP_ENTRY(entry->pgroup_table_idx)->leader_count == 0) ||
                ((int16_t)entry->flags < 0) ||
                (entry->pgroup_table_idx == 0);

            /* 0x00E3F40A: SIGCONT always takes the stop path instead */
            if ((sig_mask & SIG_MASK_SIGCONT) != 0) {
                send_kill = 0;
            }

            if (send_kill) {
                /* 0x00E3F410 */
                deliver_param = 0x09010009;
                PROC2_$DELIVER_SIGNAL_INTERNAL((int16_t)entry->self_index,
                                               9 /* SIGKILL */,
                                               deliver_param, &status);
            } else {
                /* 0x00E3F434 */
                entry->pad_94 = (uint16_t)*signal;

                /* 0x00E3F438 */
                entry->flags &= (uint16_t)0xFFCF;

                /* 0x00E3F43E: bset.b #0x6 on the flags word's LOW byte */
                entry->flags |= 0x0040;

                /* 0x00E3F444 */
                deliver_param = 0x09010017;

                /* 0x00E3F44C */
                if ((int16_t)entry->flags >= 0) {
                    parent_idx = (int16_t)entry->parent_pgroup_idx;

                    /* 0x00E3F464 */
                    EC_$ADVANCE(PROC_CR_REC_EC(parent_idx));

                    /* 0x00E3F482: parent flags LOW byte, bit 2 */
                    parent = P2_INFO_ENTRY(parent_idx);
                    if ((parent->flags & 0x0004) == 0) {
                        /* 0x00E3F49A */
                        PROC2_$DELIVER_SIGNAL_INTERNAL(parent_idx,
                                                       0x17 /* SIGCHLD */,
                                                       deliver_param, &status);
                    }
                }

                /* 0x00E3F4AC: this is where the process actually stops */
                (void)PROC1_$SUSPEND(entry->level1_pid, &status);

                /* 0x00E3F4B4 / 0x00E3F4C2 */
                ML_$UNLOCK(PROC2_LOCK_ID);
                ML_$LOCK(PROC2_LOCK_ID);
            }
        }
    }

    /* 0x00E3F4D0 */
    if ((sig_mask & ~entry->sig_mask_1) == 0) {
        entry->sig_pending &= ~sig_mask;
    }

    /* 0x00E3F4E8 */
    PROC2_$DELIVER_PENDING_INTERNAL((int16_t)entry->self_index);

    /* 0x00E3F4EE */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E3F4FC */
    result[0] = entry->sig_blocked_2;

    /* 0x00E3F500: btst.l #0xa on the flags word = 0x0400 */
    result[1] = (entry->flags & 0x0400) ? 1u : 0u;
}
