*
* SVC_$TRAP4 - Domain/OS TRAP #4 System Call Dispatcher
*
* TRAP #4 handles syscalls (0-130) that take 4 arguments.
* User programs invoke system calls via TRAP #4 with:
*   - D0.w = syscall number (0-130)
*   - Four arguments on user stack at (USP+0x04), (USP+0x08), (USP+0x0C), (USP+0x10)
*
* From: 0x00e7b120
*
* Memory layout:
*   0x00e7b120: SVC_$TRAP4 main entry
*   0x00e7b8e6: SVC_$TRAP4_TABLE (131 entries)
*
* Address space check:
*   0xCC0000 = boundary between user and kernel space
*   USP and all four argument pointers must be < 0xCC0000
*

        .text
        .even

*----------------------------------------------------------------------
* SVC_$TRAP4 - 4-argument syscall dispatcher
*
* Input:
*   D0.w = syscall number (0x00-0x82, i.e. 0-130)
*   USP points to argument frame:
*     (USP+0x04) = arg1
*     (USP+0x08) = arg2
*     (USP+0x0C) = arg3
*     (USP+0x10) = arg4
*
* Processing:
*   1. Validate syscall number (< 0x83)
*   2. Look up handler in SVC_$TRAP4_TABLE (defined in svc_tables.c)
*   3. Validate USP < 0xCC0000
*   4. Validate and copy 4 arguments from user stack (arg4 first, then arg3, arg2, arg1)
*   5. Call handler
*   6. Clean up stack and return via RTE
*
* Original bytes at 0xe7b120:
*   b0 7c 00 83    cmp.w #$83,D0
*   64 4a          bcc.b SVC_$TRAP4_INVALID_JUMP
*   e5 48          lsl.w #2,D0
*   41 fa 07 bc    lea (SVC_$TRAP4_TABLE,PC),A0
*   20 70 00 00    movea.l (0,A0,D0.w),A0
*   4e 69          move USP,A1
*   22 3c 00 cc 00 00  move.l #$CC0000,D1
*   b3 c1          cmpa.l D1,A1
*   62 38          bhi.b SVC_$TRAP4_ILLEGAL_USP_JUMP
*   20 29 00 10    move.l ($10,A1),D0  (arg4)
*   b2 80          cmp.l D0,D1
*   63 34          bls.b SVC_$TRAP4_BAD_PTR_JUMP
*   2f 00          move.l D0,-(SP)     (push arg4)
*   20 29 00 0c    move.l ($0c,A1),D0  (arg3)
*   b2 80          cmp.l D0,D1
*   63 2a          bls.b SVC_$TRAP4_BAD_PTR_JUMP
*   2f 00          move.l D0,-(SP)     (push arg3)
*   20 29 00 08    move.l ($08,A1),D0  (arg2)
*   b2 80          cmp.l D0,D1
*   63 20          bls.b SVC_$TRAP4_BAD_PTR_JUMP
*   2f 00          move.l D0,-(SP)     (push arg2)
*   20 29 00 04    move.l ($04,A1),D0  (arg1)
*   b2 80          cmp.l D0,D1
*   63 16          bls.b SVC_$TRAP4_BAD_PTR_JUMP
*   2f 00          move.l D0,-(SP)     (push arg1)
*   4e 90          jsr (A0)
*   de fc 00 10    adda.w #$10,SP      (cleanup 4 args = 16 bytes)
*   4e f9 00 e2 28 bc  jmp FIM_$EXIT
*----------------------------------------------------------------------

        .global SVC_$TRAP4

SVC_$TRAP4:
        cmp.w   #$83,D0                 * Check syscall number limit (<131)
        bcc.b   SVC_$TRAP4_INVALID_JUMP * Branch if D0 >= 131

        lsl.w   #2,D0                   * D0 = syscall_num * 4 (table index)
        lea     SVC_$TRAP4_TABLE,A0     * A0 = table base (from svc_tables.c)
        movea.l (0,A0,D0.w),A0          * A0 = handler address from table

        move    USP,A1                  * A1 = user stack pointer
        move.l  #$CC0000,D1             * D1 = user/kernel boundary

        cmpa.l  D1,A1                   * Check USP < 0xCC0000
        bhi.b   SVC_$TRAP4_ILLEGAL_USP_JUMP * Branch if USP in kernel space

        * Validate and push arg4 (offset 0x10)
        move.l  (0x10,A1),D0            * D0 = arg4
        cmp.l   D0,D1                   * Check arg4 < 0xCC0000
        bls.b   SVC_$TRAP4_BAD_PTR_JUMP * Branch if arg4 >= boundary
        move.l  D0,-(SP)                * Push arg4 to supervisor stack

        * Validate and push arg3 (offset 0x0C)
        move.l  (0x0C,A1),D0            * D0 = arg3
        cmp.l   D0,D1                   * Check arg3 < 0xCC0000
        bls.b   SVC_$TRAP4_BAD_PTR_JUMP * Branch if arg3 >= boundary
        move.l  D0,-(SP)                * Push arg3 to supervisor stack

        * Validate and push arg2 (offset 0x08)
        move.l  (0x08,A1),D0            * D0 = arg2
        cmp.l   D0,D1                   * Check arg2 < 0xCC0000
        bls.b   SVC_$TRAP4_BAD_PTR_JUMP * Branch if arg2 >= boundary
        move.l  D0,-(SP)                * Push arg2 to supervisor stack

        * Validate and push arg1 (offset 0x04)
        move.l  (0x04,A1),D0            * D0 = arg1
        cmp.l   D0,D1                   * Check arg1 < 0xCC0000
        bls.b   SVC_$TRAP4_BAD_PTR_JUMP * Branch if arg1 >= boundary
        move.l  D0,-(SP)                * Push arg1 to supervisor stack

        * Call the syscall handler
        jsr     (A0)                    * Call handler(arg1, arg2, arg3, arg4)

        * Clean up and return to user mode
        adda.w  #16,SP                  * Remove 4 arguments (16 bytes)
        jmp     FIM_$EXIT               * Return via RTE

*----------------------------------------------------------------------
* Error handler jumps (use existing handlers from trap5.s)
*----------------------------------------------------------------------

SVC_$TRAP4_INVALID_JUMP:
        bra.w   SVC_$INVALID_SYSCALL    * Invalid syscall number

SVC_$TRAP4_ILLEGAL_USP_JUMP:
        bra.w   FIM_$ILLEGAL_USP        * USP >= 0xCC0000

SVC_$TRAP4_BAD_PTR_JUMP:
        bra.w   SVC_$BAD_USER_PTR       * Argument pointer >= 0xCC0000

*----------------------------------------------------------------------
* External references
*----------------------------------------------------------------------

        .extern FIM_$EXIT               * Return from exception (RTE)
        .extern FIM_$ILLEGAL_USP        * Illegal USP handler
        .extern SVC_$INVALID_SYSCALL    * Invalid syscall number handler
        .extern SVC_$BAD_USER_PTR       * Bad user pointer handler
        .extern SVC_$TRAP4_TABLE        * Syscall table (defined in svc_tables.c)

        .end
