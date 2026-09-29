/*
 * PROC2_$SET_VALID - Mark the current process valid
 *
 * Called during process startup.  It maps a stack file if the entry does
 * not have one yet, sets the valid bit in the process flags, and -- for a
 * process that is orphaned but does not have the alternate-ASID flag --
 * fills in the creation record from the parent entry.
 *
 * Original address: 0x00E73484 (364 bytes)
 *
 * Full instruction trace (A3 = P2_INFO_ENTRY(current_idx) + 0xE4, so a
 * displacement d means entry offset 0xE4 + d; A2 = the creation record):
 *   00e73484  link.w A6,-0x14
 *   00e73488  movem.l {A4 A3 A2},-(SP)
 *   00e7348c  move.w (0x00e20608).l,D0w   ; PROC1_$CURRENT
 *   00e7349e  move.w (0x3eb6,A1),D0w      ; PROC2_$DATA.pid_to_index[pid]
 *   00e734a4  muls.w #0xe4,D1
 *   00e734a8  lea (0x0,A0,D1),A3
 *   00e734ac  movea.l (-0x78,A3),A2       ; entry->cr_rec_2 (+0x6C)
 *   00e734b0  move.l (-0x8,A3),D1         ; entry->stack_uid.high (+0xDC)
 *   00e734b4  cmp.l (0x00e1737c).l,D1     ; UID_$NIL.high ONLY
 *   00e734ba  bne.b 0x00e73506
 *   00e734bc  move.l (0x00e2b92c).l,(0xb0,A2)  ; AS_$STACK_FILE_LOW
 *   00e734c4  move.l (0x00e2b960).l,(0xb4,A2)  ; AS_$INIT_STACK_FILE_SIZE
 *   00e734cc  pea (0x94,A2)               ; &cr_rec->status        (arg 6)
 *   00e734d0  pea (-0x8,A3)               ; &entry->stack_uid      (arg 5)
 *   00e734d4  pea (0x11a,PC)              ; cell 0x00E735F0        (arg 4)
 *   00e734d8  pea (0x11a,PC)              ; cell 0x00E735F4        (arg 3)
 *   00e734dc  pea (0xb4,A2)               ; &cr_rec->size          (arg 2)
 *   00e734e0  pea (0xb0,A2)               ; &cr_rec->addr_lo       (arg 1)
 *   00e734e4  jsr 0x00e43c04.l            ; MST_$MAP_AREA_AT
 *   00e734ee  tst.l (0x94,A2) / beq.b 0x00e734fa
 *   00e734f4  jsr 0x00e74398.l            ; PROC2_$DELETE (never returns)
 *   00e734fa  lea (-0x8,A3),A0
 *   00e734fe  move.l (A0)+,(0xa8,A2)      ; cr_rec->stack_uid = entry->stack_uid
 *   00e73502  move.l (A0)+,(0xac,A2)
 *   00e73506  jsr 0x00e20b12.l            ; ML_$LOCK(4)
 *   00e73514  bset.b #0x7,(-0xb9,A3)      ; flags |= 0x0080 (LOW byte, bit 7)
 *   00e7351c  jsr 0x00e20b62.l            ; ML_$UNLOCK(4)
 *   00e73528  move.w (-0xba,A3),D1w       ; flags
 *   00e7352c  btst.l #0xc,D1 / beq.w 0x00e735e6   ; 0x1000 must be set
 *   00e73534  btst.l #0xb,D1 / bne.w 0x00e735e6   ; 0x0800 must be clear
 *   00e7353c  move.w (0x00e2060a).l,D1w   ; PROC1_$AS_ID
 *   00e73548  lsl.w #0x3,D1w
 *   00e7354a  lea (0x10,A0,D1w),A1        ; &PROC2_$UNWIRED_DATA.uid[asid] (0xE7BE94)
 *   00e7354e  move.l (A1)+,(0x98,A2)      ; cr_rec->proc_uid
 *   00e73552  move.l (A1)+,(0x9c,A2)
 *   00e73556  move.w (-0xce,A3),D1w       ; entry->upid (+0x16)
 *   00e7355a  ext.l D1                    ; SIGN-extended
 *   00e7355c  move.l D1,(0xb8,A2)
 *   00e73560  lea (-0xdc,A3),A1           ; &entry->parent_uid (+0x08)
 *   00e73564  move.l (A1)+,(0xa0,A2)      ; cr_rec->parent_uid
 *   00e73568  move.l (A1)+,(0xa4,A2)
 *   00e7356c  lea (-0x8,A3),A1            ; &entry->stack_uid (+0xDC)
 *   00e73570  move.l (A1)+,(0xa8,A2)      ; cr_rec->stack_uid
 *   00e73574  move.l (A1)+,(0xac,A2)
 *   00e73578  clr.l (0x74,A2)
 *   00e7357c  clr.w (0x78,A2)
 *   00e73580  lea (0x7c,A2),A1 / clr.l (A1)+ x3   ; 0x7C, 0x80, 0x84
 *   00e7358a  clr.b (0x90,A2)
 *   00e7358e  clr.b (0xc8,A2)
 *   00e73592  move.w #0x1,(0xc6,A2)
 *   00e73598  btst.b #0x3,(0xc5,A2) / beq.b 0x00e735b2
 *   00e735a0  move.w (-0xc6,A3),D1w       ; entry->parent_pgroup_idx (+0x1E)
 *   00e735a6  lea (0x10,A0,D1w),A1        ; &PROC2_$UNWIRED_DATA.uid[that index]
 *   00e735aa  move.l (A1)+,(0xbc,A2)
 *   00e735ae  move.l (A1)+,(0xc0,A2)
 *   00e735b2  tst.w (-0xbe,A3)            ; entry->debugger_idx (+0x26)
 *   00e735b6  sne D1b / move.b D1b,(0x90,A2)
 *   00e735bc  movea.l A2,A0 / moveq #0xd,D0 / lea (0x4,A0),A1
 *   00e735c4  clr.l (-0x4,A1) / addq.l #0x4,A1 / dbf   ; 14 longs, 0x00..0x37
 *   00e735ce  lea (0x38,A2),A4 / moveq #0xd,D0 / movea.l A4,A1 / lea (0x4,A1),A0
 *   00e735dc  clr.l (-0x4,A0) / addq.l #0x4,A0 / dbf   ; 14 longs, 0x38..0x6F
 *   00e735e6  movem.l (-0x20,A6),{A2 A3 A4} / unlk A6 / rts
 *
 * Notes:
 *  - The creation record comes from entry+0x6C (cr_rec_2), the same pointer
 *    PROC2_$COMPLETE_VFORK uses, not from entry+0x68.
 *  - The "no stack file yet" test at 0x00E734B4 compares only the HIGH
 *    longword of the UID against UID_$NIL.
 *  - PROC2_$UNWIRED_DATA.uid at 0x00E735A6 is indexed by entry+0x1E, while the boolean
 *    written to cr_rec+0x90 comes from entry+0x26.  Both reproduced as found.
 */

#include "proc2/proc2_internal.h"

/*
 * The two by-reference constants MST_$MAP_AREA_AT is called with
 * (`pea (0x11a,PC)` twice at 0x00E734D4 / 0x00E734D8).  They sit in the
 * PROC2 code region just past the end of this function.
 *
 * MST_$MAP_AREA_AT reads its third argument as a longword
 * (`movea.l (0x10,A6),A0 / move.l (A0),-(SP)` at 0x00E43C24) and its
 * fourth as a single boolean byte (`move.b (A4),-(SP)` at 0x00E43C22 and
 * `tst.b (A4)` / `bpl` at 0x00E43C4E), which is why only the first of the
 * four bytes at 0x00E735F0 (0xFF 0x00 0x20 0x48) matters.
 */
/* Shared with PROC2_$COMPLETE_VFORK (`pea (-0x20e,PC)` at 0x00E73800), so
 * it is defined once here and declared in proc2_internal.h. */
const uint32_t proc2_$map_area_size_00e735f4 = 0x00004000;
static const int8_t proc2_$map_area_true_00e735f0 = (int8_t)0xFF;

/* cr_rec_t (the creation record reached through entry+0x6C) is shared with
 * PROC2_$COMPLETE_VFORK and lives in proc2_internal.h. */

void PROC2_$SET_VALID(void)
{
    int16_t current_idx;
    proc2_info_t *entry;
    cr_rec_t *cr_rec;
    uid_t *stack_uid_ptr;
    uint32_t *clear_ptr;
    int i;

    /* 0x00E7348C-0x00E734A8 */
    current_idx = (int16_t)PROC2_$DATA.pid_to_index[PROC1_$CURRENT];
    entry = P2_INFO_ENTRY(current_idx);

    /* 0x00E734AC: the record hangs off entry+0x6C, not entry+0x68 */
    cr_rec = (cr_rec_t *)entry->cr_rec_2;

    /* 0x00E734B0: &entry->stack_uid (entry+0xDC) */
    stack_uid_ptr = &entry->stack_uid;

    /*
     * 0x00E734B4: only the HIGH longword is compared against UID_$NIL.
     */
    if (stack_uid_ptr->high == UID_$NIL.high) {
        /* 0x00E734BC / 0x00E734C4 */
        cr_rec->addr_lo = AS_$STACK_FILE_LOW;
        cr_rec->size = AS_$INIT_STACK_FILE_SIZE;

        /* 0x00E734E4 */
        MST_$MAP_AREA_AT(&cr_rec->addr_lo, &cr_rec->size,
                         (void *)&proc2_$map_area_size_00e735f4,
                         (void *)&proc2_$map_area_true_00e735f0,
                         stack_uid_ptr, &cr_rec->status);

        /* 0x00E734EE: any non-zero status is fatal here */
        if (cr_rec->status != status_$ok) {
            PROC2_$DELETE();
            /* does not return */
        }

        /* 0x00E734FA */
        cr_rec->stack_uid = *stack_uid_ptr;
    }

    /* 0x00E73506-0x00E73520 */
    ML_$LOCK(PROC2_LOCK_ID);
    entry->flags |= 0x0080;          /* bset.b #0x7,(-0xb9,A3) */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E7352C / 0x00E73534 */
    if ((entry->flags & PROC2_FLAG_ORPHAN) != 0 &&
        (entry->flags & PROC2_FLAG_ALT_ASID) == 0) {

        /* 0x00E7354E */
        cr_rec->proc_uid = PROC2_$UNWIRED_DATA.uid[PROC1_$AS_ID];

        /* 0x00E73556: ext.l -- the upid is sign-extended into a longword */
        cr_rec->field_b8 = (int32_t)(int16_t)entry->upid;

        /* 0x00E73564 */
        cr_rec->parent_uid = entry->parent_uid;

        /* 0x00E73570 */
        cr_rec->stack_uid = *stack_uid_ptr;

        /* 0x00E73578-0x00E73592 */
        cr_rec->field_74 = 0;
        cr_rec->field_78 = 0;
        cr_rec->field_7c = 0;
        cr_rec->field_80 = 0;
        cr_rec->field_84 = 0;
        cr_rec->field_90 = 0;
        cr_rec->field_c8 = 0;
        cr_rec->count_c6 = 1;

        /* 0x00E73598: btst.b #0x3,(0xc5,A2) */
        if ((cr_rec->flags_c5 & 0x08) != 0) {
            /* 0x00E735A0: indexed by entry+0x1E, not by the debugger index */
            cr_rec->debugger_uid = PROC2_$UNWIRED_DATA.uid[entry->parent_pgroup_idx];
        }

        /* 0x00E735B2: sne on entry+0x26 */
        cr_rec->field_90 = (entry->debugger_idx != 0) ? 0xFF : 0x00;

        /*
         * 0x00E735BC: moveq #0xd + dbf = 14 longwords from cr_rec+0x00,
         * i.e. offsets 0x00..0x37.
         */
        clear_ptr = (uint32_t *)cr_rec;
        for (i = 0; i < 14; i++) {
            clear_ptr[i] = 0;
        }

        /*
         * 0x00E735CE: a second 14-longword clear starting at cr_rec+0x38,
         * i.e. offsets 0x38..0x6F.
         */
        clear_ptr = (uint32_t *)((uint8_t *)cr_rec + 0x38);
        for (i = 0; i < 14; i++) {
            clear_ptr[i] = 0;
        }
    }
}
