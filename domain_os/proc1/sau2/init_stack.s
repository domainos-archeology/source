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

        .section ".text.INIT_STACK","ax",@progbits
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
 *   [SP+0]: exit_handler - the address immediately after this function
 *
 * When the dispatcher does its "rts" after restoring registers, it
 * "returns" to proc1_$process_exit_handler (0x00E20ACC) with the entry
 * point still on the stack.  That handler sets the SR, pops the entry
 * point and `jsr`s it, so a process body that returns normally comes back
 * there and is unbound.
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
 * proc1_$process_exit_handler - initial return address of every level-1 process
 *
 * This is the return address INIT_STACK pushes underneath the entry point
 * (`lea (0x1a,PC),A1` at 0x00E20AB0).  The dispatcher resumes a brand-new
 * process by loading pcb->save_a7 and returning, which lands here with the
 * entry point still on the stack.
 *
 * Address: 0x00E20ACC (22 bytes, immediately after INIT_STACK)
 *
 * Original instruction bytes (0x00E20ACC..0x00E20AE1):
 *   46 fc 20 00        move    #0x2000,SR          ; supervisor, IPL 0, CCR clear
 *   20 5f              movea.l (A7)+,A0            ; A0 = entry point
 *   4e 90              jsr     (A0)                ; run the process body
 *   59 4f              subq.w  #0x4,A7             ; 4-byte status_$t slot
 *   48 57              pea     (A7)                ; &status  (2nd argument)
 *   3f 3a fb 2e        move.w  (-0x4d2,PC),-(SP)   ; PROC1_$CURRENT (0x00E20608)
 *   4e b9 00 e1 4e 24  jsr     0x00E14E24.l        ; PROC1_$UNBIND(pid, &status)
 *   4e 4f              trap    #15                 ; UNBIND must not return
 *
 * The SR is loaded outright rather than adjusted: a freshly dispatched
 * process starts at IPL 0 in supervisor state with a clear condition code,
 * regardless of what the dispatcher was running at.  If the process body
 * ever returns, the process unbinds itself and the trap catches a return
 * from PROC1_$UNBIND.
 */
        .global proc1_$process_exit_handler
exit_handler:
proc1_$process_exit_handler:
        move.w  #0x2000,%sr             /* supervisor, IPL 0, CCR cleared */
        movea.l (%sp)+,%a0              /* A0 = entry point pushed by INIT_STACK */
        jsr     (%a0)                   /* run the process body */

        /* The body returned: unbind this process. */
        subq.w  #0x4,%sp                /* 4-byte status_$t result slot */
        pea     (%sp)                   /* &status                (arg 2) */
        move.w  (PROC1_$CURRENT:w,%pc),-(%sp)  /* 0x00E20608     (arg 1) */
        jsr     PROC1_$UNBIND
        trap    #15                     /* PROC1_$UNBIND does not return */

        .end
