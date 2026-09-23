/*
 * peb/proc_cleanup.c - PEB process cleanup
 *
 * PEB_$PROC_CLEANUP is the process-exit hook: when the board is installed
 * and the current process has no PEB pages mapped, it calls the wired
 * helper, which (under IPL 7) waits for the board to go idle if this
 * process owns it, unmaps the mirror page, and zeroes the process's
 * 28-byte FP save record.
 *
 * Original addresses:
 *   PEB_$PROC_CLEANUP:     0x00E752BC (32 bytes; SAU2 map "I E752BC
 *                          PEB_WIRED size = 20")
 *   peb_$cleanup_internal: 0x00E70954 (148 bytes; inside "I E70810
 *                          PEB_WIRED size = 22C", no map symbol of its own)
 *
 * The status cell CRASH_SYSTEM is handed, 0x00E70950 (bytes 80 24 00 01 =
 * status_$peb_fpu_is_hung | 0x80000000), sits in the same code segment and
 * is also the cell PEB_$TOUCH reaches at 0x00E70864 (`pea (0xea,PC)`), so
 * it stays the shared object PEB_FPU_Is_Hung_Err in peb_data.c.
 */

#include "peb/peb_internal.h"

/*
 * peb_$cleanup_internal - wired cleanup body
 *
 *   00e70954    link.w A6,-0x10
 *   00e70958    movem.l {A5 A2 D2},-(SP)
 *   00e7095c    lea (0xe84e80).l,A5           ; A5 = PEB_$WIRED_DATA_START
 *   00e70962    movea.l #0xe24c78,A2          ; PEB globals
 *   00e70968    ori #0x700,SR                 ; IPL 7
 *   00e7096c    move.w (0x14,A2),D0w          ; owner_pid
 *   00e70970    cmp.w (0x00e20608).l,D0w      ; == PROC1_$CURRENT ?
 *   00e70976    bne.b 0x00e709b8              ; no -> skip to the clear
 *   00e70978    tst.w (0x00ff7000).l          ; PEB_CTL busy (bit 15)?
 *   00e7097e    bpl.b 0x00e709a8              ; idle -> remove page
 *   00e70980    move.w #0x270f,D0w            ; dbf count 9999 -> 10000 polls
 *   00e70984    movea.l #0xff7000,A0
 *   00e7098a    movea.l A0,A0
 *   00e7098c    tst.w (A0)
 *   00e7098e    bpl.b 0x00e709a8              ; idle -> remove page
 *   00e70990    dbf D0w,0x00e7098c
 *   00e70994    tst.w (A0)                    ; one last look
 *   00e70996    bpl.b 0x00e709a8
 *   00e70998    andi #-0x701,SR               ; IPL 0 (forced, not restored)
 *   00e7099c    pea (-0x4e,PC)                ; &0x00E70950 PEB_FPU_Is_Hung_Err
 *   00e709a0    jsr 0x00e1e700.l              ; CRASH_SYSTEM
 *   00e709a6    addq.w #0x4,SP
 *   00e709a8    pea (0x2d).w
 *   00e709ac    jsr 0x00e23d64.l              ; MMU_$REMOVE(0x2D)
 *   00e709b2    addq.w #0x4,SP
 *   00e709b4    clr.w (0x14,A2)               ; owner_pid = 0
 *   00e709b8    andi #-0x701,SR               ; IPL 0 (forced)
 *   00e709bc    move.w (0x00e2060a).l,D0w     ; PROC1_$AS_ID
 *   00e709c2    moveq #0x6,D1                 ; dbf count 6 -> 7 longwords
 *   00e709c4    lsl.w #0x2,D0w                ; asid*4
 *   00e709c6    move.w D0w,D2w
 *   00e709c8    neg.w D0w                     ; -asid*4
 *   00e709ca    lsl.w #0x3,D2w                ; asid*32
 *   00e709cc    add.w D2w,D0w                 ; asid*28
 *   00e709ce    lea (0x0,A5,D0w*0x1),A0       ; &WIRED_DATA_START[asid]
 *   00e709d2    moveq #0x4,D0
 *   00e709d4    clr.l (-0x4,A0,D0*0x1)        ; record[+0], +4, ... +0x18
 *   00e709d8    addq.l #0x4,D0
 *   00e709da    dbf D1w,0x00e709d4
 *   00e709de    movem.l (-0x1c,A6),{D2 A2 A5}
 *   00e709e4    unlk A6
 *   00e709e6    rts
 */
void peb_$cleanup_internal(void)
{
    uint16_t polls;
    int16_t i;
    peb_fp_state_t *state;

    /* 0x00E70968: `ori #0x700,SR` - IPL 7, nothing saved */
    SET_IPL7();

    /* 0x00E7096C-0x00E70976 */
    if (PEB_$OWNER_PID == PROC1_$CURRENT) {
        /* 0x00E70978-0x00E70996: poll PEB_CTL bit 15 up to 10001 times */
        if ((int16_t)PEB_CTL < 0) {
            polls = 9999;
            for (;;) {
                if ((int16_t)PEB_CTL >= 0) {
                    goto remove_page;
                }
                if (polls-- == 0) {
                    break;
                }
            }
            if ((int16_t)PEB_CTL < 0) {
                /* 0x00E70998-0x00E709A6: `andi #-0x701,SR` (forced IPL 0) before crashing */
                SET_IPL0();
                CRASH_SYSTEM(&PEB_FPU_Is_Hung_Err);
            }
        }
remove_page:
        /* 0x00E709A8-0x00E709B4 */
        MMU_$REMOVE(0x2D);
        PEB_$OWNER_PID = 0;
    }

    /* 0x00E709B8: `andi #-0x701,SR` - IPL forced to 0, not restored */
    SET_IPL0();

    /* 0x00E709BC-0x00E709DA: zero this address space's 7-longword record */
    state = peb_get_fp_state((int16_t)PROC1_$AS_ID);
    for (i = 0; i < 7; i++) {
        ((uint32_t *)(void *)state)[i] = 0;
    }
}

/*
 * PEB_$PROC_CLEANUP - process-exit hook
 *
 *   00e752bc    link.w A6,-0x8
 *   00e752c0    movea.l #0xe24c78,A0
 *   00e752c6    move.b (0x1a,A0),D0b          ; installed
 *   00e752ca    not.b D0b
 *   00e752cc    or.b (0x1f,A0),D0b            ; ~installed | mmu_installed
 *   00e752d0    bmi.b 0x00e752d8              ; bit 7 set -> nothing to do
 *   00e752d2    jsr 0x00e70954.l              ; peb_$cleanup_internal
 *   00e752d8    unlk A6
 *   00e752da    rts
 *
 * With both flags Domain booleans (0x00 or 0xFF), bit 7 of the OR is clear
 * only when installed == 0xFF and mmu_installed == 0x00: the board is
 * present and this process does not currently have it mapped.
 */
void PEB_$PROC_CLEANUP(void)
{
    uint8_t cond;

    /* 0x00E752C6-0x00E752D0 */
    cond = (uint8_t)(~PEB_$INSTALLED | PEB_$MMU_INSTALLED);
    if ((int8_t)cond >= 0) {
        /* 0x00E752D2 */
        peb_$cleanup_internal();
    }
}
