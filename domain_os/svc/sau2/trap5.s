*
* SVC_$TRAP5 - Domain/OS TRAP #5 System Call Dispatcher
*
* This is the main entry point for complex system calls in Domain/OS.
* User programs invoke system calls via TRAP #5 with:
*   - D0.w = syscall number (0-98)
*   - Arguments on user stack (up to 5 longwords)
*
* From: 0x00e7b17c
*
* Memory layout:
*   0x00e7b170: Branch to invalid syscall handler
*   0x00e7b174: Branch to illegal USP handler
*   0x00e7b178: Branch to bad user pointer handler
*   0x00e7b17c: SVC_$TRAP5 main entry
*
* Address space check:
*   0xCC0000 = boundary between user and kernel space
*   USP and all argument pointers must be < 0xCC0000
*

        .text
        .even

*----------------------------------------------------------------------
* Branch stubs for error handlers (these precede main entry)
*----------------------------------------------------------------------

SVC_$INVALID_JUMP:
        bra.w   SVC_$INVALID_SYSCALL    * 0xe7b170: invalid syscall number

SVC_$ILLEGAL_USP_JUMP:
        bra.w   FIM_$ILLEGAL_USP        * 0xe7b174: USP >= 0xCC0000

SVC_$BAD_PTR_JUMP:
        bra.w   SVC_$BAD_USER_PTR       * 0xe7b178: argument pointer >= 0xCC0000

*----------------------------------------------------------------------
* SVC_$TRAP5 - Main system call entry point
*
* Input:
*   D0.w = syscall number (0x00-0x62)
*   USP points to argument frame:
*     (USP+0x04) = arg1
*     (USP+0x08) = arg2
*     (USP+0x0C) = arg3
*     (USP+0x10) = arg4
*     (USP+0x14) = arg5
*
* Processing:
*   1. Validate syscall number (< 0x63)
*   2. Look up handler in SVC_$TRAP5_TABLE (defined in svc_tables.c)
*   3. Validate USP < 0xCC0000
*   4. Validate and copy up to 5 arguments from user stack
*   5. Call handler
*   6. Clean up stack and return via RTE
*----------------------------------------------------------------------

        .global SVC_$TRAP5
        .global SVC_$INVALID_SYSCALL
        .global SVC_$BAD_USER_PTR
        .global SVC_$UNIMPLEMENTED

SVC_$TRAP5:
        cmp.w   #$63,D0                 * Check syscall number limit
        bcc.b   SVC_$INVALID_JUMP       * Branch if D0 >= 99

        lsl.w   #2,D0                   * D0 = syscall_num * 4 (table index)
        lea     SVC_$TRAP5_TABLE,A0     * A0 = table base (from svc_tables.c)
        movea.l (0,A0,D0.w),A0          * A0 = handler address from table

        move    USP,A1                  * A1 = user stack pointer
        move.l  #$CC0000,D1             * D1 = user/kernel boundary

        cmpa.l  D1,A1                   * Check USP < 0xCC0000
        bhi.b   SVC_$ILLEGAL_USP_JUMP   * Branch if USP in kernel space

        * Validate and push arg5 (offset 0x14)
        move.l  (0x14,A1),D0            * D0 = arg5
        cmp.l   D0,D1                   * Check arg5 < 0xCC0000
        bls.b   SVC_$BAD_PTR_JUMP       * Branch if arg5 >= boundary
        move.l  D0,-(SP)                * Push arg5 to supervisor stack

        * Validate and push arg4 (offset 0x10)
        move.l  (0x10,A1),D0            * D0 = arg4
        cmp.l   D0,D1                   * Check arg4 < 0xCC0000
        bls.b   SVC_$BAD_PTR_JUMP       * Branch if arg4 >= boundary
        move.l  D0,-(SP)                * Push arg4

        * Validate and push arg3 (offset 0x0C)
        move.l  (0x0C,A1),D0            * D0 = arg3
        cmp.l   D0,D1                   * Check arg3 < 0xCC0000
        bls.b   SVC_$BAD_PTR_JUMP       * Branch if arg3 >= boundary
        move.l  D0,-(SP)                * Push arg3

        * Validate and push arg2 (offset 0x08)
        move.l  (0x08,A1),D0            * D0 = arg2
        cmp.l   D0,D1                   * Check arg2 < 0xCC0000
        bls.b   SVC_$BAD_PTR_JUMP       * Branch if arg2 >= boundary
        move.l  D0,-(SP)                * Push arg2

        * Validate and push arg1 (offset 0x04)
        move.l  (0x04,A1),D0            * D0 = arg1
        cmp.l   D0,D1                   * Check arg1 < 0xCC0000
        bls.b   SVC_$BAD_PTR_JUMP       * Branch if arg1 >= boundary
        move.l  D0,-(SP)                * Push arg1

        * Call the syscall handler
        jsr     (A0)                    * Call handler(arg1,arg2,arg3,arg4,arg5)

        * Clean up and return to user mode
        adda.w  #$14,SP                 * Remove 5 arguments (20 bytes)
        jmp     FIM_$EXIT               * Return via RTE

*----------------------------------------------------------------------
* SVC_$INVALID_SYSCALL - Invalid syscall number handler
*
* Called when syscall number >= 99.
* Generates a fault with status_$fault_invalid_SVC_code (0x00120007).
*
* From: 0x00e7b28e
*----------------------------------------------------------------------

SVC_$INVALID_SYSCALL:
        move.l  #$00120007,D0           * status_$fault_invalid_SVC_code
        bra.b   SVC_$GENERATE_FAULT

*----------------------------------------------------------------------
* SVC_$BAD_USER_PTR - Bad user pointer handler
*
* Called when a syscall argument pointer >= 0xCC0000.
* Generates a fault with status_$fault_protection_boundary_violation.
*
* From: 0x00e7b2a0
*----------------------------------------------------------------------

SVC_$BAD_USER_PTR:
        move.l  #$0012000B,D0           * status_$fault_protection_boundary_violation
        * Fall through to SVC_$GENERATE_FAULT

*----------------------------------------------------------------------
* SVC_$GENERATE_FAULT - Common fault generation code
*
* Input:
*   D0 = fault status code
*
* Sets up fault frame and calls FIM_$GENERATE to deliver the fault.
*----------------------------------------------------------------------

SVC_$GENERATE_FAULT:
        move.w  PROC1_$CURRENT,D1       * Get current process index
        lsl.w   #2,D1                   * D1 = index * 4
        lea     OS_STACK_BASE,A0        * A0 = stack base array
        movea.l (0,A0,D1.w),A1          * A1 = current process stack
        lea     (-0x08,A1),SP           * Set SP for fault frame
        move.l  D0,-(SP)                * Push status code
        jsr     FIM_$GENERATE           * Generate the fault
        jmp     FIM_$ILLEGAL_USP        * Final handler (not reached normally)

*----------------------------------------------------------------------
* SVC_$UNIMPLEMENTED - Unimplemented syscall handler
*
* Called for syscalls that are not yet implemented.
* Generates a fault with status_$fault_unimplemented_SVC (0x0012001c).
*
* From: 0x00e7b2cc
*----------------------------------------------------------------------

SVC_$UNIMPLEMENTED:
        move.l  #$0012001C,D0           * status_$fault_unimplemented_SVC
        bra.b   SVC_$GENERATE_FAULT

*----------------------------------------------------------------------
* External references
*----------------------------------------------------------------------

        .extern FIM_$EXIT               * Return from exception (RTE)
        .extern FIM_$ILLEGAL_USP        * Illegal USP handler
        .extern FIM_$GENERATE           * Fault generation
        .extern PROC1_$CURRENT          * Current process index
        .extern OS_STACK_BASE           * Process stack base array
        .extern SVC_$TRAP5_TABLE        * Syscall table (defined in svc_tables.c)

        .end
