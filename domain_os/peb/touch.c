/*
 * peb/touch.c - PEB Context Establishment
 *
 * Establishes PEB context for the current process when accessing
 * PEB registers. This handles the context switch of FP state
 * between processes.
 *
 * Original address: 0x00E70810 (320 bytes)
 */

#include "peb/peb_internal.h"
#include "mmu/mmu.h"

/*
 * PEB_$TOUCH - Touch PEB to establish context
 *
 * Called when accessing PEB registers to ensure proper process context.
 * If the access address is in the PEB range (0x7000-0x7400) and the PEB
 * is not currently mapped for this process, saves the previous owner's
 * state and loads this process's state.
 *
 * This function is the core of PEB context switching. It:
 * 1. Checks if access is in PEB address range
 * 2. Waits for PEB to become not busy (up to 10000 iterations)
 * 3. Saves previous owner's FP state if needed
 * 4. Installs MMU mapping for new owner
 * 5. Loads new owner's FP state
 * 6. Updates owner tracking variables
 *
 * Assembly analysis:
 *   00e70810    link.w A6,-0x14
 *   00e70814    movem.l { A5 A3 A2 D2},-(SP)
 *   00e70818    lea (0xe84e80).l,A5       ; PEB_$WIRED_DATA_START
 *   00e7081e    movea.l (0x8,A6),A0       ; addr pointer
 *   00e70822    clr.b D2b                 ; result = 0
 *   00e70824    move.l (A0),D0            ; *addr
 *   00e70826    cmpi.l #0x7000,D0         ; if addr < 0x7000
 *   00e7082c    bcs.w 0x00e70944          ;   return 0
 *   00e70830    cmpi.l #0x7400,D0         ; if addr >= 0x7400
 *   00e70836    bcc.w 0x00e70944          ;   return 0
 *   00e7083a    tst.b (0x00e24c97).l      ; if mmu_installed < 0
 *   00e70840    bmi.w 0x00e70944          ;   return 0
 *   00e70844    st D2b                    ; result = 0xFF
 *   00e70846    tst.w (0x00ff7000).l      ; Check PEB busy
 *   00e7084c    bpl.b 0x00e70870          ; If not busy, continue
 *   00e7084e    move.w #0x270f,D0w        ; Loop count = 9999
 *   00e70852    movea.l #0xff7000,A0
 *   00e70858    tst.w (A0)                ; Check busy
 *   00e7085a    bpl.b 0x00e70870          ; If not busy, break
 *   00e7085c    dbf D0w,0x00e70858        ; Loop
 *   00e70860    tst.w (A0)                ; Final check
 *   00e70862    bpl.b 0x00e70870
 *   00e70864    pea (0xea,PC)             ; "PEB FPU Is Hung Err"
 *   00e70868    jsr 0x00e1e700.l          ; CRASH_SYSTEM
 *   ... (context switch logic)
 *
 * Parameters:
 *   addr - Pointer to access address being touched
 *
 * Returns:
 *   0xFF if PEB context was established, 0 otherwise
 */
uint8_t PEB_$TOUCH(uint32_t *addr)
{
    uint32_t access_addr;
    int16_t i;
    int16_t prev_asid;
    peb_fp_state_t *state;

    access_addr = *addr;

    /* Check if access is in PEB register range */
    if (access_addr < 0x7000 || access_addr >= 0x7400) {
        return 0;
    }

    /* Check if MMU mapping is already installed for this process */
    if (PEB_$MMU_INSTALLED < 0) {
        return 0;
    }

    /* Wait for PEB to become not busy */
    if (PEB_CTL < 0) {
        /* PEB is busy - wait with timeout */
        for (i = 0; i < 10000; i++) {
            if (PEB_CTL >= 0) {
                goto peb_ready;
            }
        }
        /* Still busy after timeout */
        if (PEB_CTL < 0) {
            CRASH_SYSTEM(PEB_FPU_Is_Hung_Err);
        }
    }

peb_ready:
    /* Install private mapping for current process */
    MMU_$INSTALL_PRIVATE(0x2D, 0x7000, MMU_FLAGS(PROC1_$AS_ID, 6));

    /* Check if there's a previous owner whose state needs saving */
    if (PEB_$OWNER_PID != 0) {
        /* Save previous owner's state */
        prev_asid = PEB_$OWNER_ASID;
        state = peb_get_fp_state(prev_asid);

        /* Unload registers from hardware to previous owner's save area */
        /* Using direct hardware access since we're doing context switch */
        {
            volatile uint32_t *base = (volatile uint32_t *)0x7000;

            state->data_regs[0] = *(volatile uint32_t *)((uint8_t *)base + 0x8C);
            state->data_regs[1] = *(volatile uint32_t *)((uint8_t *)base + 0x90);
            state->data_regs[2] = *(volatile uint32_t *)((uint8_t *)base + 0x1D0);
            state->data_regs[3] = *(volatile uint32_t *)((uint8_t *)base + 0x1D4);
            state->status_reg = *(volatile uint32_t *)((uint8_t *)base + 0xF4);
            state->ctrl_reg = *(volatile uint32_t *)((uint8_t *)base + 0x1DC);
            state->instr_counter = *(volatile uint32_t *)((uint8_t *)base + 0x104);
        }
    }

    /* Load current process's state */
    state = peb_get_fp_state(PROC1_$AS_ID);

    /* Load registers from current process's save area to hardware */
    {
        volatile uint32_t *base = (volatile uint32_t *)0x7000;

        *(volatile uint32_t *)((uint8_t *)base + 0x94) = state->data_regs[0];
        *(volatile uint32_t *)((uint8_t *)base + 0x98) = state->data_regs[1];
        *(volatile uint32_t *)((uint8_t *)base + 0x1B0) = state->data_regs[2];
        *(volatile uint32_t *)((uint8_t *)base + 0x1B4) = state->data_regs[3];
        *(volatile uint32_t *)((uint8_t *)base + 0xF4) = state->status_reg;
        *(volatile uint32_t *)((uint8_t *)base + 0x84) = state->ctrl_reg;
        *(volatile uint32_t *)((uint8_t *)base + 0x104) = state->instr_counter;
    }

    /* Update owner tracking */
    PEB_$OWNER_PID = PROC1_$CURRENT;
    PEB_$OWNER_ASID = PROC1_$AS_ID;

    return 0xFF;
}
