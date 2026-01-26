/*
 * proc1/sau2/init_stack.s - Process Stack Initialization (m68k/SAU2)
 *
 * Initializes a process's stack for first context switch.
 * Sets up the initial stack frame so that when the dispatcher
 * context-switches to this process, it will "return" to the
 * specified entry point.
 *
 * Original address: 0x00E20AA4 (40 bytes)
 */

        .text
        .even

/*
 * PCB register save offsets (from proc1.h)
 */
        .equ    PCB_SAVE_A5,    0x2C    /* Saved A5 */
        .equ    PCB_SAVE_A6,    0x30    /* Saved A6 (frame pointer) */
        .equ    PCB_SAVE_A7,    0x34    /* Saved A7 (SSP) */
        .equ    PCB_SAVE_USP,   0x38    /* Saved user stack pointer */

/*
 * INIT_STACK - Initialize process stack for first dispatch
 *
 * Sets up a new process's initial stack so that when the
 * dispatcher context-switches to it, the process will
 * begin execution at the specified entry point.
 *
 * The stack is set up as follows (growing downward):
 *   [SP+4]: entry_point - where the process starts
 *   [SP+0]: exit_handler - return address if process exits
 *
 * When the dispatcher does its "rts" after restoring registers,
 * it will "return" to exit_handler, which is the address
 * immediately after this function. If the process's entry_point
 * returns, it will then "return" to exit_handler.
 *
 * Parameters (on stack, stdcall convention):
 *   (4,SP)  - pcb: Pointer to process control block
 *   (8,SP)  - entry_ptr: Pointer to entry point address
 *   (12,SP) - sp_ptr: Pointer to stack pointer value
 *
 * The function also clears:
 *   pcb->save_a5 (0x2C): Initial A5 = 0
 *   pcb->save_a6 (0x30): Initial A6 = 0 (frame pointer)
 *   pcb->save_usp (0x38): Initial USP = 0
 *
 * And sets:
 *   pcb->save_a7 (0x34): Points to prepared stack
 *
 * Assembly (0x00E20AA4):
 *   movea.l (0xc,SP),A0          ; A0 = sp_ptr
 *   movea.l (A0),A0              ; A0 = *sp_ptr (current SP value)
 *   movea.l (0x8,SP),A1          ; A1 = entry_ptr
 *   move.l  (A1),-(A0)           ; Push *entry_ptr (entry point)
 *   lea     (0x1a,PC),A1         ; A1 = exit_handler address
 *   move.l  A1,-(A0)             ; Push exit_handler
 *   movea.l (0x4,SP),A1          ; A1 = pcb
 *   clr.l   (0x2c,A1)            ; pcb->save_a5 = 0
 *   clr.l   (0x30,A1)            ; pcb->save_a6 = 0
 *   move.l  A0,(0x34,A1)         ; pcb->save_a7 = A0 (prepared stack)
 *   clr.l   (0x38,A1)            ; pcb->save_usp = 0
 *   rts
 */
        .global INIT_STACK
INIT_STACK:
        movea.l (0xc,%sp),%a0           /* A0 = sp_ptr */
        movea.l (%a0),%a0               /* A0 = *sp_ptr (stack pointer value) */
        movea.l (0x8,%sp),%a1           /* A1 = entry_ptr */
        move.l  (%a1),-(%a0)            /* Push entry point onto stack */
        lea     (exit_handler,%pc),%a1  /* A1 = address of exit_handler */
        move.l  %a1,-(%a0)              /* Push exit_handler as return address */
        movea.l (0x4,%sp),%a1           /* A1 = pcb */
        clr.l   (PCB_SAVE_A5,%a1)       /* pcb->save_a5 = 0 */
        clr.l   (PCB_SAVE_A6,%a1)       /* pcb->save_a6 = 0 */
        move.l  %a0,(PCB_SAVE_A7,%a1)   /* pcb->save_a7 = prepared stack ptr */
        clr.l   (PCB_SAVE_USP,%a1)      /* pcb->save_usp = 0 */
        rts

/*
 * exit_handler - Process exit handler
 *
 * This is the return address pushed onto the initial stack.
 * If a process's entry point returns normally, execution
 * continues here. This would typically terminate the process.
 *
 * Address: 0x00E20ACC (immediately after INIT_STACK)
 *
 * TODO: Analyze what code actually exists at 0x00E20ACC
 * to determine the proper exit handling behavior.
 * For now, this is a placeholder that will trap.
 */
exit_handler:
        /*
         * The original code at 0x00E20ACC needs analysis.
         * It likely calls PROC1_$UNBIND or similar to
         * properly terminate the process.
         *
         * For now, use illegal instruction to trap if reached.
         */
        illegal

        .end
