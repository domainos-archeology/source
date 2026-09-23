/*
 * peb/regs.c - PEB register load/unload and the per-AS FP context moves
 *
 * PEB_$LOAD_REGS copies a 28-byte FP state record into the board's input
 * registers; PEB_$UNLOAD_REGS reads the board's output registers into such
 * a record.  PEB_$GET_FP / PEB_$PUT_FP select the record of an address
 * space in the wired per-AS table (0x1C bytes each at 0xE84E80) and call
 * them.  The board is addressed at virtual address 0x7000 (a local in each
 * routine holds the constant); the input and output register sets differ,
 * so the two byte-offset lists below are not mirror images.
 *
 * Original addresses (SAU2 map: "I E5AD38 PEB_UNWIRED size = 200"):
 *   PEB_$LOAD_REGS:   0x00E5AE60 (70 bytes)
 *   PEB_$UNLOAD_REGS: 0x00E5AEA6 (70 bytes)
 *   PEB_$GET_FP:      0x00E5AEEC (38 bytes)
 *   PEB_$PUT_FP:      0x00E5AF12 (38 bytes)
 */

#include "peb/peb_internal.h"

#define PEB_REG32(base, off) (*(volatile uint32_t *)(void *)((base) + (off)))

/*
 * PEB_$LOAD_REGS - state record -> board input registers
 *
 *   00e5ae60    link.w A6,-0x4
 *   00e5ae64    pea (A2)
 *   00e5ae66    movea.l (0x8,A6),A0            ; state
 *   00e5ae6a    move.l #0x7000,(-0x4,A6)       ; base = 0x7000
 *   00e5ae72    lea (A0),A1
 *   00e5ae74    movea.l (-0x4,A6),A2
 *   00e5ae78    move.l (A1)+,(0x94,A2)         ; state+0x00 -> 0x7094
 *   00e5ae7c    move.l (A1)+,(0x98,A2)         ; state+0x04 -> 0x7098
 *   00e5ae80    lea (0x8,A0),A1
 *   00e5ae84    move.l (A1)+,(0x1b0,A2)        ; state+0x08 -> 0x71B0
 *   00e5ae88    move.l (A1)+,(0x1b4,A2)        ; state+0x0C -> 0x71B4
 *   00e5ae8c    move.l (0x10,A0),(0xf4,A2)     ; state+0x10 -> 0x70F4
 *   00e5ae92    move.l (0x14,A0),(0x84,A2)     ; state+0x14 -> 0x7084
 *   00e5ae98    move.l (0x18,A0),(0x104,A2)    ; state+0x18 -> 0x7104
 *   00e5ae9e    movea.l (-0x8,A6),A2
 *   00e5aea2    unlk A6
 *   00e5aea4    rts
 */
void PEB_$LOAD_REGS(peb_fp_state_t *state)
{
    volatile uint8_t *base = PEB_REG_PAGE_7000;   /* (-0x4,A6) */

    /* 0x00E5AE78-0x00E5AE98 */
    PEB_REG32(base, PEB_REG_DATA_OUT_0) = state->data_regs[0];
    PEB_REG32(base, PEB_REG_DATA_OUT_1) = state->data_regs[1];
    PEB_REG32(base, PEB_REG_STAT_OUT_0) = state->data_regs[2];
    PEB_REG32(base, PEB_REG_STAT_OUT_1) = state->data_regs[3];
    PEB_REG32(base, PEB_REG_STATUS)     = state->status_reg;
    PEB_REG32(base, PEB_REG_CTRL_IN)    = state->ctrl_reg;
    PEB_REG32(base, PEB_REG_CTRL_OUT)   = state->instr_counter;
}

/*
 * PEB_$UNLOAD_REGS - board output registers -> state record
 *
 *   00e5aea6    link.w A6,-0x4
 *   00e5aeaa    pea (A2)
 *   00e5aeac    movea.l (0x8,A6),A0            ; state
 *   00e5aeb0    move.l #0x7000,(-0x4,A6)       ; base = 0x7000
 *   00e5aeb8    movea.l (-0x4,A6),A1
 *   00e5aebc    lea (0x8c,A1),A2
 *   00e5aec0    move.l (A2)+,(A0)              ; 0x708C -> state+0x00
 *   00e5aec2    move.l (A2)+,(0x4,A0)          ; 0x7090 -> state+0x04
 *   00e5aec6    lea (0x1d0,A1),A2
 *   00e5aeca    move.l (A2)+,(0x8,A0)          ; 0x71D0 -> state+0x08
 *   00e5aece    move.l (A2)+,(0xc,A0)          ; 0x71D4 -> state+0x0C
 *   00e5aed2    move.l (0xf4,A1),(0x10,A0)     ; 0x70F4 -> state+0x10
 *   00e5aed8    move.l (0x1dc,A1),(0x14,A0)    ; 0x71DC -> state+0x14
 *   00e5aede    move.l (0x104,A1),(0x18,A0)    ; 0x7104 -> state+0x18
 *   00e5aee4    movea.l (-0x8,A6),A2
 *   00e5aee8    unlk A6
 *   00e5aeea    rts
 */
void PEB_$UNLOAD_REGS(peb_fp_state_t *state)
{
    volatile uint8_t *base = PEB_REG_PAGE_7000;   /* (-0x4,A6) */

    /* 0x00E5AEC0-0x00E5AEDE */
    state->data_regs[0]  = PEB_REG32(base, PEB_REG_DATA_IN_0);
    state->data_regs[1]  = PEB_REG32(base, PEB_REG_DATA_IN_1);
    state->data_regs[2]  = PEB_REG32(base, PEB_REG_STAT_IN_0);
    state->data_regs[3]  = PEB_REG32(base, PEB_REG_STAT_IN_1);
    state->status_reg    = PEB_REG32(base, PEB_REG_STATUS);
    state->ctrl_reg      = PEB_REG32(base, PEB_REG_MISC);
    state->instr_counter = PEB_REG32(base, PEB_REG_CTRL_OUT);
}

/*
 * PEB_$GET_FP - load the board from address space *asid's record
 *
 *   00e5aeec    link.w A6,0x0
 *   00e5aef0    movea.l (0x8,A6),A0            ; asid pointer
 *   00e5aef4    move.w (A0),D0w
 *   00e5aef6    lsl.w #0x2,D0w                 ; asid*4
 *   00e5aef8    move.w D0w,D1w
 *   00e5aefa    neg.w D0w
 *   00e5aefc    lsl.w #0x3,D1w                 ; asid*32
 *   00e5aefe    movea.l #0xe84e80,A1           ; PEB_$WIRED_DATA_START
 *   00e5af04    add.w D1w,D0w                  ; asid*28 (16-bit)
 *   00e5af06    pea (0x0,A1,D0w*0x1)
 *   00e5af0a    bsr.w 0x00e5ae60               ; PEB_$LOAD_REGS
 *   00e5af0e    unlk A6
 *   00e5af10    rts
 */
void PEB_$GET_FP(int16_t *asid)
{
    /* 0x00E5AEF4-0x00E5AF0A */
    PEB_$LOAD_REGS(peb_get_fp_state(*asid));
}

/*
 * PEB_$PUT_FP - save the board into address space *asid's record
 *
 *   00e5af12 .. 0x00e5af2c  same index arithmetic as PEB_$GET_FP
 *   00e5af30    bsr.w 0x00e5aea6               ; PEB_$UNLOAD_REGS
 *   00e5af34    unlk A6
 *   00e5af36    rts
 */
void PEB_$PUT_FP(int16_t *asid)
{
    /* 0x00E5AF1A-0x00E5AF30 */
    PEB_$UNLOAD_REGS(peb_get_fp_state(*asid));
}
