*
* SVC_$TRAP2 - Domain/OS TRAP #2 System Call Dispatcher
*
* TRAP #2 handles syscalls (0-132) that take 2 arguments.
* User programs invoke system calls via TRAP #2 with:
*   - D0.w = syscall number (0-132)
*   - Two arguments on user stack at (USP+0x04) and (USP+0x08)
*
* From: 0x00e7b094
*
* Memory layout:
*   0x00e7b094: SVC_$TRAP2 main entry
*   0x00e7b466: SVC_$TRAP2_TABLE (133 entries)
*
* Address space check:
*   0xCC0000 = boundary between user and kernel space
*   USP and both argument pointers must be < 0xCC0000
*

        .text
        .even

*----------------------------------------------------------------------
* SVC_$TRAP2 - 2-argument syscall dispatcher
*
* Input:
*   D0.w = syscall number (0x00-0x84, i.e. 0-132)
*   USP points to argument frame:
*     (USP+0x04) = arg1
*     (USP+0x08) = arg2
*
* Processing:
*   1. Validate syscall number (< 0x85)
*   2. Look up handler in SVC_$TRAP2_TABLE (defined in svc_tables.c)
*   3. Validate USP < 0xCC0000
*   4. Validate and copy 2 arguments from user stack (arg2 first, then arg1)
*   5. Call handler
*   6. Clean up stack and return via RTE
*
* Original bytes at 0xe7b094:
*   b0 7c 00 85    cmp.w #$85,D0
*   64 34          bcc.b SVC_$TRAP2_BAD_PTR_JUMP
*   e5 48          lsl.w #2,D0
*   41 fa 03 c8    lea (SVC_$TRAP2_TABLE,PC),A0
*   20 70 00 00    movea.l (0,A0,D0.w),A0
*   4e 69          move USP,A1
*   22 3c 00 cc 00 00  move.l #$CC0000,D1
*   b3 c1          cmpa.l D1,A1
*   62 22          bhi.b SVC_$TRAP2_ILLEGAL_USP_JUMP
*   20 29 00 08    move.l ($08,A1),D0  (arg2)
*   b2 80          cmp.l D0,D1
*   63 d6          bls.b SVC_$TRAP2_BAD_PTR_JUMP2
*   2f 00          move.l D0,-(SP)     (push arg2)
*   20 29 00 04    move.l ($04,A1),D0  (arg1)
*   b2 80          cmp.l D0,D1
*   63 cc          bls.b SVC_$TRAP2_BAD_PTR_JUMP3
*   2f 00          move.l D0,-(SP)     (push arg1)
*   4e 90          jsr (A0)
*   50 4f          addq.w #8,SP        (cleanup 2 args = 8 bytes)
*   4e f9 00 e2 28 bc  jmp FIM_$EXIT
*----------------------------------------------------------------------

        .global SVC_$TRAP2

SVC_$TRAP2:
        cmp.w   #$85,D0                 * Check syscall number limit (<133)
        bcc.b   SVC_$TRAP2_INVALID_JUMP * Branch if D0 >= 133

        lsl.w   #2,D0                   * D0 = syscall_num * 4 (table index)
        lea     SVC_$TRAP2_TABLE,A0     * A0 = table base (from svc_tables.c)
        movea.l (0,A0,D0.w),A0          * A0 = handler address from table

        move    USP,A1                  * A1 = user stack pointer
        move.l  #$CC0000,D1             * D1 = user/kernel boundary

        cmpa.l  D1,A1                   * Check USP < 0xCC0000
        bhi.b   SVC_$TRAP2_ILLEGAL_USP_JUMP * Branch if USP in kernel space

        * Validate and push arg2 (offset 0x08)
        move.l  (0x08,A1),D0            * D0 = arg2
        cmp.l   D0,D1                   * Check arg2 < 0xCC0000
        bls.b   SVC_$TRAP2_BAD_PTR_JUMP * Branch if arg2 >= boundary
        move.l  D0,-(SP)                * Push arg2 to supervisor stack

        * Validate and push arg1 (offset 0x04)
        move.l  (0x04,A1),D0            * D0 = arg1
        cmp.l   D0,D1                   * Check arg1 < 0xCC0000
        bls.b   SVC_$TRAP2_BAD_PTR_JUMP * Branch if arg1 >= boundary
        move.l  D0,-(SP)                * Push arg1 to supervisor stack

        * Call the syscall handler
        jsr     (A0)                    * Call handler(arg1, arg2)

        * Clean up and return to user mode
        addq.w  #8,SP                   * Remove 2 arguments (8 bytes)
        jmp     FIM_$EXIT               * Return via RTE

*----------------------------------------------------------------------
* Error handler jumps (use existing handlers from trap5.s)
*----------------------------------------------------------------------

SVC_$TRAP2_INVALID_JUMP:
        bra.w   SVC_$INVALID_SYSCALL    * Invalid syscall number

SVC_$TRAP2_ILLEGAL_USP_JUMP:
        bra.w   FIM_$ILLEGAL_USP        * USP >= 0xCC0000

SVC_$TRAP2_BAD_PTR_JUMP:
        bra.w   SVC_$BAD_USER_PTR       * Argument pointer >= 0xCC0000

*----------------------------------------------------------------------
* External references
*----------------------------------------------------------------------

        .extern FIM_$EXIT               * Return from exception (RTE)
        .extern FIM_$ILLEGAL_USP        * Illegal USP handler
        .extern SVC_$INVALID_SYSCALL    * Invalid syscall number handler
        .extern SVC_$BAD_USER_PTR       * Bad user pointer handler
        .extern SVC_$TRAP2_TABLE        * Syscall table (defined in svc_tables.c)

        .end
