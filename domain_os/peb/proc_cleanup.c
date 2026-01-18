/*
 * peb/proc_cleanup.c - PEB Process Cleanup
 *
 * Cleans up PEB resources when a process terminates.
 *
 * Original addresses:
 *   PEB_$PROC_CLEANUP: 0x00E752BC (32 bytes)
 *   peb_$cleanup_internal: 0x00E70954 (148 bytes)
 */

#include "peb/peb_internal.h"
#include "mmu/mmu.h"

/*
 * peb_$cleanup_internal - Internal cleanup helper
 *
 * Does the actual cleanup work:
 * 1. If current process owns PEB, waits for it to become not busy
 * 2. Removes MMU mapping for private registers
 * 3. Clears the per-process FP state storage
 *
 * Assembly analysis:
 *   00e70954    link.w A6,-0x10
 *   00e70958    movem.l { A5 A2 D2},-(SP)
 *   00e7095c    lea (0xe84e80).l,A5       ; PEB_$WIRED_DATA_START
 *   00e70962    movea.l #0xe24c78,A2      ; PEB globals
 *   00e70968    ori #0x700,SR             ; Disable interrupts
 *   00e7096c    move.w (0x14,A2),D0w      ; owner_pid
 *   00e70970    cmp.w (0x00e20608).l,D0w  ; Compare with PROC1_$CURRENT
 *   00e70976    bne.b 0x00e709b8          ; If not owner, skip
 *   00e70978    tst.w (0x00ff7000).l      ; Check PEB busy
 *   00e7097e    bpl.b 0x00e709a8          ; If not busy, continue
 *   00e70980    move.w #0x270f,D0w        ; Loop count = 9999
 *   00e70984    movea.l #0xff7000,A0
 *   00e7098a    movea.l A0,A0
 *   00e7098c    tst.w (A0)                ; Check busy
 *   00e7098e    bpl.b 0x00e709a8          ; If not busy, break
 *   00e70990    dbf D0w,0x00e7098c        ; Loop
 *   00e70994    tst.w (A0)                ; Final check
 *   00e70996    bpl.b 0x00e709a8
 *   00e70998    andi #-0x701,SR           ; Enable interrupts
 *   00e7099c    pea (-0x4e,PC)            ; "PEB FPU Is Hung Err"
 *   00e709a0    jsr 0x00e1e700.l          ; CRASH_SYSTEM
 *   00e709a6    addq.w #0x4,SP
 *   00e709a8    pea (0x2d).w              ; PPN 0x2D
 *   00e709ac    jsr 0x00e23d64.l          ; MMU_$REMOVE
 *   00e709b2    addq.w #0x4,SP
 *   00e709b4    clr.w (0x14,A2)           ; Clear owner_pid
 *   00e709b8    andi #-0x701,SR           ; Enable interrupts
 *   00e709bc    move.w (0x00e2060a).l,D0w ; PROC1_$AS_ID
 *   00e709c2    moveq #0x6,D1             ; Loop 7 times (7 longwords = 28 bytes)
 *   00e709c4    lsl.w #0x2,D0w            ; asid * 4
 *   00e709c6    move.w D0w,D2w
 *   00e709c8    neg.w D0w                 ; -asid * 4
 *   00e709ca    lsl.w #0x3,D2w            ; asid * 32
 *   00e709cc    add.w D2w,D0w             ; asid * 28
 *   00e709ce    lea (0x0,A5,D0w*0x1),A0   ; &WIRED_DATA_START[asid * 28]
 *   00e709d2    moveq #0x4,D0             ; Starting offset
 *   00e709d4    clr.l (-0x4,A0,D0*0x1)    ; Clear longword
 *   00e709d8    addq.l #0x4,D0            ; Next offset
 *   00e709da    dbf D1w,0x00e709d4        ; Loop
 *   00e709de    movem.l (-0x1c,A6),{ D2 A2 A5}
 *   00e709e4    unlk A6
 *   00e709e6    rts
 */
void peb_$cleanup_internal(void)
{
    int16_t i;
    peb_fp_state_t *state;

    /* Disable interrupts for critical section */
    /* In actual m68k code this uses ORI #0x700,SR */

    /* Check if current process owns the PEB */
    if (PEB_$OWNER_PID == PROC1_$CURRENT) {
        /* Wait for PEB to become not busy */
        if (PEB_CTL < 0) {
            for (i = 0; i < 10000; i++) {
                if (PEB_CTL >= 0) {
                    goto peb_ready;
                }
            }
            /* Still busy - crash */
            if (PEB_CTL < 0) {
                /* Re-enable interrupts before crash */
                CRASH_SYSTEM(PEB_FPU_Is_Hung_Err);
            }
        }
peb_ready:
        /* Remove private register mapping */
        MMU_$REMOVE(0x2D);

        /* Clear owner */
        PEB_$OWNER_PID = 0;
    }

    /* Re-enable interrupts */

    /* Clear the per-process FP state storage */
    state = peb_get_fp_state(PROC1_$AS_ID);

    /* Zero all 7 longwords (28 bytes) */
    state->data_regs[0] = 0;
    state->data_regs[1] = 0;
    state->data_regs[2] = 0;
    state->data_regs[3] = 0;
    state->status_reg = 0;
    state->ctrl_reg = 0;
    state->instr_counter = 0;
}

/*
 * PEB_$PROC_CLEANUP - Clean up PEB state for process termination
 *
 * Called during process cleanup to release PEB resources.
 * Only does cleanup if PEB is installed and MMU is not already set up.
 *
 * Assembly analysis:
 *   00e752bc    link.w A6,-0x8
 *   00e752c0    movea.l #0xe24c78,A0      ; PEB globals
 *   00e752c6    move.b (0x1a,A0),D0b      ; installed flag
 *   00e752ca    not.b D0b                 ; ~installed
 *   00e752cc    or.b (0x1f,A0),D0b        ; ~installed | mmu_installed
 *   00e752d0    bmi.b 0x00e752d8          ; If < 0, skip cleanup
 *   00e752d2    jsr 0x00e70954.l          ; peb_$cleanup_internal
 *   00e752d8    unlk A6
 *   00e752da    rts
 *
 * The condition ~installed | mmu_installed < 0 means:
 * - Skip if installed is 0xFF (PEB hardware present) OR mmu_installed is 0xFF
 * - Only do cleanup if neither flag is set (software-only mode)
 *
 * Actually looking more carefully: if installed < 0 (0xFF), ~installed >= 0
 * So ~installed | mmu_installed < 0 only if mmu_installed < 0
 * This means: skip cleanup if MMU is installed for this process
 */
void PEB_$PROC_CLEANUP(void)
{
    uint8_t cond;

    /* Check if cleanup is needed */
    /* Condition: ~installed | mmu_installed < 0 means skip */
    cond = ~PEB_$INSTALLED | PEB_$MMU_INSTALLED;

    if ((int8_t)cond >= 0) {
        /* Do the cleanup */
        peb_$cleanup_internal();
    }
}
