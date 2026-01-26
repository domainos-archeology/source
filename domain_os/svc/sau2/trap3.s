|
| SVC_$TRAP3 - Domain/OS TRAP #3 System Call Dispatcher
|
| TRAP #3 handles syscalls (0-154) that take 3 arguments.
| User programs invoke system calls via TRAP #3 with:
|   - D0.w = syscall number (0-154)
|   - Three arguments on user stack at (USP+0x04), (USP+0x08), (USP+0x0C)
|
| From: 0x00e7b0d8
|
| Memory layout:
|   0x00e7b0d8: SVC_$TRAP3 main entry
|   0x00e7b67a: SVC_$TRAP3_TABLE (155 entries)
|
| Address space check:
|   0xCC0000 = boundary between user and kernel space
|   USP and all three argument pointers must be < 0xCC0000
|

        .text
        .even

|----------------------------------------------------------------------
| SVC_$TRAP3 - 3-argument syscall dispatcher
|
| Input:
|   D0.w = syscall number (0x00-0x9A, i.e. 0-154)
|   USP points to argument frame:
|     (USP+0x04) = arg1
|     (USP+0x08) = arg2
|     (USP+0x0C) = arg3
|
| Processing:
|   1. Validate syscall number (< 0x9B)
|   2. Look up handler in SVC_$TRAP3_TABLE (defined in svc_tables.c)
|   3. Validate USP < 0xCC0000
|   4. Validate and copy 3 arguments from user stack (arg3 first, then arg2, then arg1)
|   5. Call handler
|   6. Clean up stack and return via RTE
|
| Original bytes at 0xe7b0d8:
|   b0 7c 00 9b    cmp.w #0x9b,D0
|   64 f0          bcc.b SVC_$TRAP3_BAD_PTR_JUMP
|   e5 48          lsl.w #2,D0
|   41 fa 05 98    lea (SVC_$TRAP3_TABLE,PC),A0
|   20 70 00 00    movea.l (0,A0,D0.w),A0
|   4e 69          move USP,A1
|   22 3c 00 cc 00 00  move.l #0xCC0000,D1
|   b3 c1          cmpa.l D1,A1
|   62 de          bhi.b SVC_$TRAP3_ILLEGAL_USP_JUMP
|   20 29 00 0c    move.l (0x0c,A1),D0  (arg3)
|   b2 80          cmp.l D0,D1
|   63 7c          bls.b SVC_$TRAP3_BAD_PTR_JUMP2
|   2f 00          move.l D0,-(SP)     (push arg3)
|   20 29 00 08    move.l (0x08,A1),D0  (arg2)
|   b2 80          cmp.l D0,D1
|   63 72          bls.b SVC_$TRAP3_BAD_PTR_JUMP3
|   2f 00          move.l D0,-(SP)     (push arg2)
|   20 29 00 04    move.l (0x04,A1),D0  (arg1)
|   b2 80          cmp.l D0,D1
|   63 68          bls.b SVC_$TRAP3_BAD_PTR_JUMP4
|   2f 00          move.l D0,-(SP)     (push arg1)
|   4e 90          jsr (A0)
|   de fc 00 0c    adda.w #0x0c,SP      (cleanup 3 args = 12 bytes)
|   4e f9 00 e2 28 bc  jmp FIM_$EXIT
|----------------------------------------------------------------------

        .global SVC_$TRAP3

SVC_$TRAP3:
        cmp.w   #0x9B,%d0               | Check syscall number limit (<155)
        bcc.b   SVC_$TRAP3_INVALID_JUMP | Branch if D0 >= 155

        lsl.w   #2,%d0                  | D0 = syscall_num * 4 (table index)
        lea     SVC_$TRAP3_TABLE,%a0    | A0 = table base (from svc_tables.c)
        movea.l (0,%a0,%d0:w),%a0       | A0 = handler address from table

        move    %usp,%a1                | A1 = user stack pointer
        move.l  #0xCC0000,%d1           | D1 = user/kernel boundary

        cmpa.l  %d1,%a1                 | Check USP < 0xCC0000
        bhi.b   SVC_$TRAP3_ILLEGAL_USP_JUMP | Branch if USP in kernel space

        | Validate and push arg3 (offset 0x0C)
        move.l  (0x0C,%a1),%d0          | D0 = arg3
        cmp.l   %d0,%d1                 | Check arg3 < 0xCC0000
        bls.b   SVC_$TRAP3_BAD_PTR_JUMP | Branch if arg3 >= boundary
        move.l  %d0,-(%sp)              | Push arg3 to supervisor stack

        | Validate and push arg2 (offset 0x08)
        move.l  (0x08,%a1),%d0          | D0 = arg2
        cmp.l   %d0,%d1                 | Check arg2 < 0xCC0000
        bls.b   SVC_$TRAP3_BAD_PTR_JUMP | Branch if arg2 >= boundary
        move.l  %d0,-(%sp)              | Push arg2 to supervisor stack

        | Validate and push arg1 (offset 0x04)
        move.l  (0x04,%a1),%d0          | D0 = arg1
        cmp.l   %d0,%d1                 | Check arg1 < 0xCC0000
        bls.b   SVC_$TRAP3_BAD_PTR_JUMP | Branch if arg1 >= boundary
        move.l  %d0,-(%sp)              | Push arg1 to supervisor stack

        | Call the syscall handler
        jsr     (%a0)                   | Call handler(arg1, arg2, arg3)

        | Clean up and return to user mode
        adda.w  #12,%sp                 | Remove 3 arguments (12 bytes)
        jmp     FIM_$EXIT               | Return via RTE

|----------------------------------------------------------------------
| Error handler jumps (use existing handlers from trap5.s)
|----------------------------------------------------------------------

SVC_$TRAP3_INVALID_JUMP:
        bra.w   SVC_$INVALID_SYSCALL    | Invalid syscall number

SVC_$TRAP3_ILLEGAL_USP_JUMP:
        bra.w   FIM_$ILLEGAL_USP        | USP >= 0xCC0000

SVC_$TRAP3_BAD_PTR_JUMP:
        bra.w   SVC_$BAD_USER_PTR       | Argument pointer >= 0xCC0000

|----------------------------------------------------------------------
| External references
|----------------------------------------------------------------------

        .extern FIM_$EXIT               | Return from exception (RTE)
        .extern FIM_$ILLEGAL_USP        | Illegal USP handler
        .extern SVC_$INVALID_SYSCALL    | Invalid syscall number handler
        .extern SVC_$BAD_USER_PTR       | Bad user pointer handler
        .extern SVC_$TRAP3_TABLE        | Syscall table (defined in svc_tables.c)

        .end
