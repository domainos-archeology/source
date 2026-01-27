/*
 * IO_$USE_INT_STACK - Switch from OS stack to interrupt stack
 *
 * This routine saves the current OS stack pointer and switches to a
 * dedicated interrupt stack. If already on the interrupt stack (indicated
 * by IO_$SAVED_OS_SP being non-zero), the switch is skipped.
 *
 * This is a pure assembly routine because it directly manipulates the
 * stack pointer, which cannot be expressed in C.
 *
 * Semantics:
 *   1. Pop return address into A1 (cannot use RTS after SP changes)
 *   2. If IO_$SAVED_OS_SP != 0, already on interrupt stack - jump to caller
 *   3. Save interrupted SR from caller's exception frame
 *   4. Save current SP as the OS stack pointer
 *   5. Switch SP to IO_$INT_STACK_BASE
 *   6. Jump to return address via A1
 *
 * Called by:
 *   - dispatch_vector_irq (0x00e2e85c) - FLIH dispatcher
 *   - PEB_$INT (0x00e2446c) - PEB interrupt handler
 *   - FUN_00e26f28 (0x00e26f28)
 *   - FUN_00e2b134 (0x00e2b134)
 *
 * Original address: 0x00e2e826 (32 bytes)
 *
 * Original bytes:
 *   0xe2e826: 22 5f                 movea.l (SP)+,A1
 *   0xe2e828: 4a b9 00 e2 e8 22     tst.l (0x00e2e822).l
 *   0xe2e82e: 66 14                 bne.b done
 *   0xe2e830: 33 ef 00 10 00 eb 2b f8   move.w (0x10,SP),(0x00eb2bf8).l
 *   0xe2e838: 23 cf 00 e2 e8 22     move.l SP,(0x00e2e822).l
 *   0xe2e83e: 2e 7c 00 eb 2b e8     movea.l #0xeb2be8,SP
 *   0xe2e844: 4e d1                 jmp (A1)
 */

        .text
        .even

/*
 * External references
 */
        .extern IO_$SAVED_OS_SP
        .extern IO_$SAVED_INT_SR

/*
 * Interrupt stack base address
 * On M68K, the stack grows downward, so this is the top of the
 * interrupt stack region. SP is set to this value when switching.
 */
        .set    IO_INT_STACK_BASE, 0x00EB2BE8

        .globl  IO_$USE_INT_STACK
        .globl  _IO_$USE_INT_STACK
IO_$USE_INT_STACK:
_IO_$USE_INT_STACK:
        movea.l (%sp)+, %a1                    /* Pop return address */
        tst.l   IO_$SAVED_OS_SP                /* Already on interrupt stack? */
        bne.b   .done                          /* If yes, skip switch */

        move.w  0x10(%sp), IO_$SAVED_INT_SR    /* Save interrupted SR from exception frame */
        move.l  %sp, IO_$SAVED_OS_SP           /* Save OS stack pointer */
        movea.l #IO_INT_STACK_BASE, %sp        /* Switch to interrupt stack */

.done:
        jmp     (%a1)                          /* Return (can't use RTS, SP changed) */
