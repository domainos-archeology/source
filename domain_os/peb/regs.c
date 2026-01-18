/*
 * peb/regs.c - PEB Register Load/Unload Functions
 *
 * Transfers FP register state between the PEB hardware and memory.
 *
 * Original addresses:
 *   PEB_$LOAD_REGS: 0x00E5AE60 (70 bytes)
 *   PEB_$UNLOAD_REGS: 0x00E5AEA6 (70 bytes)
 *   PEB_$GET_FP: 0x00E5AEEC (38 bytes)
 *   PEB_$PUT_FP: 0x00E5AF12 (38 bytes)
 */

#include "peb/peb_internal.h"

/*
 * PEB register layout (from base address 0x7000):
 *
 * Load registers (write to hardware):
 *   0x7094: Data out register 0
 *   0x7098: Data out register 1
 *   0x71B0: Status out register 0
 *   0x71B4: Status out register 1
 *   0x70F4: Exception status register
 *   0x7084: Control input register
 *   0x7104: Control output register
 *
 * Unload registers (read from hardware):
 *   0x708C: Data in register 0
 *   0x7090: Data in register 1
 *   0x71D0: Status in register 0
 *   0x71D4: Status in register 1
 *   0x70F4: Exception status register (same)
 *   0x71DC: Misc register
 *   0x7104: Control output register (same)
 */

/*
 * PEB_$LOAD_REGS - Load FP registers from memory to hardware
 *
 * Copies the 28-byte FP state from the given buffer to the
 * PEB hardware registers.
 *
 * Assembly analysis:
 *   00e5ae60    link.w A6,-0x4
 *   00e5ae64    pea (A2)
 *   00e5ae66    movea.l (0x8,A6),A0       ; state pointer
 *   00e5ae6a    move.l #0x7000,(-0x4,A6)  ; PEB base address
 *   00e5ae72    lea (A0),A1
 *   00e5ae74    movea.l (-0x4,A6),A2      ; A2 = 0x7000
 *   00e5ae78    move.l (A1)+,(0x94,A2)    ; state[0] -> 0x7094
 *   00e5ae7c    move.l (A1)+,(0x98,A2)    ; state[1] -> 0x7098
 *   00e5ae80    lea (0x8,A0),A1
 *   00e5ae84    move.l (A1)+,(0x1b0,A2)   ; state[2] -> 0x71B0
 *   00e5ae88    move.l (A1)+,(0x1b4,A2)   ; state[3] -> 0x71B4
 *   00e5ae8c    move.l (0x10,A0),(0xf4,A2)  ; state[4] -> 0x70F4
 *   00e5ae92    move.l (0x14,A0),(0x84,A2)  ; state[5] -> 0x7084
 *   00e5ae98    move.l (0x18,A0),(0x104,A2) ; state[6] -> 0x7104
 *   00e5ae9e    movea.l (-0x8,A6),A2
 *   00e5aea2    unlk A6
 *   00e5aea4    rts
 *
 * Parameters:
 *   state - Pointer to 28-byte FP state buffer
 */
void PEB_$LOAD_REGS(peb_fp_state_t *state)
{
    volatile uint32_t *base = (volatile uint32_t *)0x7000;

    /* Load data registers */
    *(volatile uint32_t *)((uint8_t *)base + 0x94) = state->data_regs[0];
    *(volatile uint32_t *)((uint8_t *)base + 0x98) = state->data_regs[1];

    /* Load status registers */
    *(volatile uint32_t *)((uint8_t *)base + 0x1B0) = state->data_regs[2];
    *(volatile uint32_t *)((uint8_t *)base + 0x1B4) = state->data_regs[3];

    /* Load exception status */
    *(volatile uint32_t *)((uint8_t *)base + 0xF4) = state->status_reg;

    /* Load control registers */
    *(volatile uint32_t *)((uint8_t *)base + 0x84) = state->ctrl_reg;
    *(volatile uint32_t *)((uint8_t *)base + 0x104) = state->instr_counter;
}

/*
 * PEB_$UNLOAD_REGS - Unload FP registers from hardware to memory
 *
 * Copies the FP state from the PEB hardware registers to the
 * given 28-byte buffer.
 *
 * Assembly analysis:
 *   00e5aea6    link.w A6,-0x4
 *   00e5aeaa    pea (A2)
 *   00e5aeac    movea.l (0x8,A6),A0       ; state pointer
 *   00e5aeb0    move.l #0x7000,(-0x4,A6)  ; PEB base address
 *   00e5aeb8    movea.l (-0x4,A6),A1      ; A1 = 0x7000
 *   00e5aebc    lea (0x8c,A1),A2          ; A2 = 0x708C
 *   00e5aec0    move.l (A2)+,(A0)         ; 0x708C -> state[0]
 *   00e5aec2    move.l (A2)+,(0x4,A0)     ; 0x7090 -> state[1]
 *   00e5aec6    lea (0x1d0,A1),A2         ; A2 = 0x71D0
 *   00e5aeca    move.l (A2)+,(0x8,A0)     ; 0x71D0 -> state[2]
 *   00e5aece    move.l (A2)+,(0xc,A0)     ; 0x71D4 -> state[3]
 *   00e5aed2    move.l (0xf4,A1),(0x10,A0)  ; 0x70F4 -> state[4]
 *   00e5aed8    move.l (0x1dc,A1),(0x14,A0) ; 0x71DC -> state[5]
 *   00e5aede    move.l (0x104,A1),(0x18,A0) ; 0x7104 -> state[6]
 *   00e5aee4    movea.l (-0x8,A6),A2
 *   00e5aee8    unlk A6
 *   00e5aeea    rts
 *
 * Parameters:
 *   state - Pointer to 28-byte FP state buffer
 */
void PEB_$UNLOAD_REGS(peb_fp_state_t *state)
{
    volatile uint32_t *base = (volatile uint32_t *)0x7000;

    /* Unload data registers */
    state->data_regs[0] = *(volatile uint32_t *)((uint8_t *)base + 0x8C);
    state->data_regs[1] = *(volatile uint32_t *)((uint8_t *)base + 0x90);

    /* Unload status registers */
    state->data_regs[2] = *(volatile uint32_t *)((uint8_t *)base + 0x1D0);
    state->data_regs[3] = *(volatile uint32_t *)((uint8_t *)base + 0x1D4);

    /* Unload exception status */
    state->status_reg = *(volatile uint32_t *)((uint8_t *)base + 0xF4);

    /* Unload misc/control registers */
    state->ctrl_reg = *(volatile uint32_t *)((uint8_t *)base + 0x1DC);
    state->instr_counter = *(volatile uint32_t *)((uint8_t *)base + 0x104);
}

/*
 * PEB_$GET_FP - Get FP context for address space
 *
 * Loads the FP registers from the saved state for the specified
 * address space ID. Uses the per-process state storage indexed
 * by AS ID.
 *
 * Assembly analysis:
 *   00e5aeec    link.w A6,0x0
 *   00e5aef0    movea.l (0x8,A6),A0       ; asid pointer
 *   00e5aef4    move.w (A0),D0w           ; *asid
 *   00e5aef6    lsl.w #0x2,D0w            ; asid * 4
 *   00e5aef8    move.w D0w,D1w
 *   00e5aefa    neg.w D0w                 ; -asid * 4
 *   00e5aefc    lsl.w #0x3,D1w            ; asid * 32
 *   00e5aefe    movea.l #0xe84e80,A1      ; PEB_$WIRED_DATA_START
 *   00e5af04    add.w D1w,D0w             ; (asid * 32) + (-asid * 4) = asid * 28
 *   00e5af06    pea (0x0,A1,D0w*0x1)      ; &WIRED_DATA_START[asid * 28]
 *   00e5af0a    bsr.w 0x00e5ae60          ; PEB_$LOAD_REGS
 *   00e5af0e    unlk A6
 *   00e5af10    rts
 *
 * Parameters:
 *   asid - Pointer to address space ID
 */
void PEB_$GET_FP(int16_t *asid)
{
    peb_fp_state_t *state = peb_get_fp_state(*asid);
    PEB_$LOAD_REGS(state);
}

/*
 * PEB_$PUT_FP - Put (save) FP context for address space
 *
 * Saves the current FP registers to the state buffer for the
 * specified address space ID.
 *
 * Assembly analysis:
 *   00e5af12    link.w A6,0x0
 *   00e5af16    movea.l (0x8,A6),A0       ; asid pointer
 *   00e5af1a    move.w (A0),D0w           ; *asid
 *   00e5af1c    lsl.w #0x2,D0w            ; asid * 4
 *   00e5af1e    move.w D0w,D1w
 *   00e5af20    neg.w D0w                 ; -asid * 4
 *   00e5af22    lsl.w #0x3,D1w            ; asid * 32
 *   00e5af24    movea.l #0xe84e80,A1      ; PEB_$WIRED_DATA_START
 *   00e5af2a    add.w D1w,D0w             ; asid * 28
 *   00e5af2c    pea (0x0,A1,D0w*0x1)
 *   00e5af30    bsr.w 0x00e5aea6          ; PEB_$UNLOAD_REGS
 *   00e5af34    unlk A6
 *   00e5af36    rts
 *
 * Parameters:
 *   asid - Pointer to address space ID
 */
void PEB_$PUT_FP(int16_t *asid)
{
    peb_fp_state_t *state = peb_get_fp_state(*asid);
    PEB_$UNLOAD_REGS(state);
}
