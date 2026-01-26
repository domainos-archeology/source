/*
 * fim/sau2/cleanup.s - FIM Cleanup Handler Routines (m68k/SAU2)
 *
 * These routines implement setjmp/longjmp-like cleanup handling
 * for the Domain/OS kernel. Each address space maintains a stack
 * of cleanup handlers that can be invoked on error conditions.
 *
 * Original addresses:
 *   FIM_$CLEANUP:      0x00E21634 (40 bytes)
 *   FIM_$RLS_CLEANUP:  0x00E2165C (22 bytes)
 *   FIM_$POP_SIGNAL:   0x00E21672 (12 bytes)
 *   FIM_$SIGNAL_FIRST: 0x00E2167E (10 bytes)
 *   FIM_$SIGNAL:       0x00E21688 (42 bytes)
 *   FIM_$GENERATE:     0x00E214A8 (6 bytes)
 */

        .text
        .even

/*
 * Cleanup handler stack head array
 * Indexed by PROC1_$CURRENT << 2
 * This is a PC-relative reference to the cleanup stack table
 */
        .equ    PROC1_CURRENT,  0x00E20608      /* Current process ID */

/*
 * FIM_$CLEANUP - Establish a cleanup handler
 *
 * Similar to setjmp - saves current context and pushes handler
 * onto the per-process cleanup handler stack.
 *
 * Parameters:
 *   4(SP) - Pointer to handler context buffer (5 longs)
 *
 * Handler context layout:
 *   0x00: Previous handler pointer (link)
 *   0x04: Return address
 *   0x08: Saved A5
 *   0x0C: Saved A6
 *   0x10: Saved SP
 *
 * Returns:
 *   D0 = status_$cleanup_handler_set (0x00120035)
 *
 * Assembly (0x00E21634):
 */
        .global FIM_$CLEANUP
FIM_$CLEANUP:
        move.w  (PROC1_CURRENT).l,%d0   /* D0 = PROC1_$CURRENT */
        movea.l (0x4,%sp),%a1           /* A1 = handler context ptr */
        lsl.w   #2,%d0                  /* D0 = process * 4 (index) */
        lea     (cleanup_stack_table,%pc),%a0 /* A0 = &cleanup_stack[0] */
        adda.w  %d0,%a0                 /* A0 = &cleanup_stack[process] */
        move.l  (%sp),%d1               /* D1 = return address */
        move.l  (%a0),%d0               /* D0 = current handler (link) */
        movea.l (0x4,%sp),%a1           /* A1 = handler context ptr */
        movem.l %d0/%d1/%a5/%a6/%sp,(%a1) /* Save link, ret, A5, A6, SP */
        move.l  %a1,(%a0)               /* Push handler onto stack */
        move.l  #0x00120035,%d0         /* status_$cleanup_handler_set */
        rts

/*
 * FIM_$RLS_CLEANUP - Release (pop) cleanup handler
 *
 * Removes the most recently established cleanup handler.
 * Called when exiting the scope that established the handler.
 *
 * Parameters:
 *   4(SP) - Pointer to handler context buffer
 *
 * Assembly (0x00E2165C):
 */
        .global FIM_$RLS_CLEANUP
FIM_$RLS_CLEANUP:
        move.w  (PROC1_CURRENT).l,%d0   /* D0 = PROC1_$CURRENT */
        movea.l (0x4,%sp),%a1           /* A1 = handler context ptr */
        lsl.w   #2,%d0                  /* D0 = process * 4 */
        lea     (cleanup_stack_table,%pc),%a0 /* A0 = &cleanup_stack[0] */
        adda.w  %d0,%a0                 /* A0 = &cleanup_stack[process] */
        move.l  (%a1),(%a0)             /* Pop: stack = handler->link */
        rts

/*
 * FIM_$POP_SIGNAL - Pop handler and continue
 *
 * Used to pop a handler after cleanup without restoring context.
 * Adjusts SP from the saved handler and jumps to return address.
 *
 * Assembly (0x00E21672):
 */
        .global FIM_$POP_SIGNAL
FIM_$POP_SIGNAL:
        movea.l (%sp)+,%a0              /* A0 = caller return address */
        movea.l (%sp),%a1               /* A1 = handler context ptr */
        movea.l (0x10,%a1),%sp          /* SP = handler->saved_sp */
        addq.w  #4,%sp                  /* Skip over our return addr */
        jmp     (%a0)                   /* Return to caller */

/*
 * FIM_$SIGNAL_FIRST - Signal first handler (entry point)
 *
 * Entry point that preserves A5/A6 before falling through to FIM_$SIGNAL.
 *
 * Parameters:
 *   4(SP) - Status code to deliver
 *
 * Assembly (0x00E2167E):
 */
        .global FIM_$SIGNAL_FIRST
FIM_$SIGNAL_FIRST:
        move.l  (0x4,%sp),%d0           /* D0 = status code */
        movem.l %a5/%a6,-(%sp)          /* Save A5, A6 */
        bra.b   signal_common           /* Fall through to FIM_$SIGNAL */

/*
 * FIM_$SIGNAL - Signal cleanup handlers (longjmp-like)
 *
 * Invokes the cleanup handler chain. If a handler is established,
 * this function does not return - it restores the handler's context
 * and jumps to the return address saved by FIM_$CLEANUP.
 *
 * If no handler is established, returns normally.
 *
 * Parameters:
 *   4(SP) - Status code to deliver (returned in D0)
 *
 * Assembly (0x00E21688):
 */
        .global FIM_$SIGNAL
FIM_$SIGNAL:
        addq.l  #4,%sp                  /* Pop return address */
        move.l  (%sp)+,%d0              /* D0 = status code */
signal_common:
        move.w  (PROC1_CURRENT).l,%d1   /* D1 = PROC1_$CURRENT */
        lea     (cleanup_stack_table,%pc),%a0 /* A0 = &cleanup_stack[0] */
        lsl.w   #2,%d1                  /* D1 = process * 4 */
        adda.w  %d1,%a0                 /* A0 = &cleanup_stack[process] */
        move.l  (%a0),%d1               /* D1 = current handler */
        beq.b   no_handler              /* If NULL, no handler */
        movea.l %d1,%a1                 /* A1 = handler context */
        move.l  (%a1)+,(%a0)            /* Pop handler: stack = link */
        movea.l (%a1)+,%a0              /* A0 = saved return address */
        movea.l (%a1)+,%a5              /* Restore A5 */
        movea.l (%a1)+,%a6              /* Restore A6 */
        subq.l  #4,%sp                  /* Make room for return */
        jmp     (%a0)                   /* Jump to saved address */
no_handler:
        movem.l (%sp)+,%a5/%a6          /* Restore A5, A6 */
        rts

/*
 * FIM_$GENERATE - Generate a fault/signal
 *
 * Small stub that pops return address, gets status, and
 * branches to the common fault generation code.
 *
 * Parameters:
 *   4(SP) - Status code
 *
 * Assembly (0x00E214A8):
 */
        .global FIM_$GENERATE
FIM_$GENERATE:
        addq.w  #4,%sp                  /* Pop return address */
        move.l  (%sp)+,%d0              /* D0 = status code */
        bra.b   generate_common         /* Branch to common code */

/*
 * Common fault generation code
 * This is called from FIM_$GENERATE and branches to the
 * main fault handler setup at 0x00E21458
 */
generate_common:
        /* Branch to fault generation code at 0x00E21458 */
        jmp     (0x00E21458).l

/*
 * Cleanup handler stack table
 * One entry per process (indexed by PROC1_$CURRENT << 2)
 * Each entry is a pointer to the head of the cleanup handler list
 *
 * This table is at offset 0x7E (126) from FIM_$CLEANUP
 * Address: 0x00E216B2
 */
cleanup_stack_table:
        .space  256                     /* 64 processes * 4 bytes each */

        .end
