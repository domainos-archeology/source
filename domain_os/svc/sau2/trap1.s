|
| SVC_$TRAP1 - Domain/OS TRAP #1 System Call Dispatcher
|
| TRAP #1 handles syscalls (0-65) that take 1 argument.
| User programs invoke system calls via TRAP #1 with:
|   - D0.w = syscall number (0-65)
|   - One argument on user stack at (USP+0x04)
|
| From: 0x00e7b05c
|
| Memory layout:
|   TRAP #0 dispatcher at 0x00e7b044 falls through here if syscall >= 32
|   0x00e7b05c: SVC_$TRAP1 main entry
|
| Address space check:
|   0xCC0000 = boundary between user and kernel space
|   USP and argument pointer must be < 0xCC0000
|

        .text
        .even

|----------------------------------------------------------------------
| SVC_$TRAP1 - 1-argument syscall dispatcher
|
| Input:
|   D0.w = syscall number (0x00-0x41, i.e. 0-65)
|   USP points to argument frame:
|     (USP+0x04) = arg1
|
| Processing:
|   1. Validate syscall number (< 0x42)
|   2. Look up handler in SVC_$TRAP1_TABLE (defined in svc_tables.c)
|   3. Validate USP < 0xCC0000
|   4. Validate and copy 1 argument from user stack
|   5. Call handler
|   6. Clean up stack and return via RTE
|
| Original bytes at 0xe7b05c:
|   b0 7c 00 42    cmp.w #0x42,D0
|   64 6c          bcc.b SVC_$TRAP1_BAD_PTR_JUMP
|   e5 48          lsl.w #2,D0
|   41 fa 02 f8    lea (SVC_$TRAP1_TABLE,PC),A0
|   20 70 00 00    movea.l (0,A0,D0.w),A0
|   4e 69          move USP,A1
|   b3 fc 00 cc 00 00  cmpa.l #0xCC0000,A1
|   62 5c          bhi.b SVC_$TRAP1_ILLEGAL_USP_JUMP
|   20 29 00 04    move.l (0x04,A1),D0
|   b0 bc 00 cc 00 00  cmp.l #0xCC0000,D0
|   62 0c          bhi.b SVC_$TRAP1_BAD_PTR_JUMP2
|   2f 00          move.l D0,-(SP)
|   4e 90          jsr (A0)
|   58 4f          addq.w #4,SP
|   4e f9 00 e2 28 bc  jmp FIM_$EXIT
|----------------------------------------------------------------------

        .global SVC_$TRAP1
        .global SVC_$TRAP1_RANGE_CHECK

| Entry point - also serves as range check fallthrough from TRAP0
SVC_$TRAP1:
SVC_$TRAP1_RANGE_CHECK:
        cmp.w   #0x42,%d0               | Check syscall number limit (<66)
        bcc.b   SVC_$TRAP1_INVALID_JUMP | Branch if D0 >= 66

        lsl.w   #2,%d0                  | D0 = syscall_num * 4 (table index)
        lea     SVC_$TRAP1_TABLE,%a0    | A0 = table base (from svc_tables.c)
        movea.l (0,%a0,%d0:w),%a0       | A0 = handler address from table

        move    %usp,%a1                | A1 = user stack pointer
        cmpa.l  #0xCC0000,%a1           | Check USP < 0xCC0000
        bhi.b   SVC_$TRAP1_ILLEGAL_USP_JUMP | Branch if USP in kernel space

        | Validate and push arg1 (offset 0x04)
        move.l  (0x04,%a1),%d0          | D0 = arg1
        cmp.l   #0xCC0000,%d0           | Check arg1 < 0xCC0000
        bhi.b   SVC_$TRAP1_BAD_PTR_JUMP | Branch if arg1 >= boundary
        move.l  %d0,-(%sp)              | Push arg1 to supervisor stack

        | Call the syscall handler
        jsr     (%a0)                   | Call handler(arg1)

        | Clean up and return to user mode
        addq.w  #4,%sp                  | Remove 1 argument (4 bytes)
        jmp     FIM_$EXIT               | Return via RTE

|----------------------------------------------------------------------
| Error handler jumps (use existing handlers from trap5.s)
|----------------------------------------------------------------------

SVC_$TRAP1_INVALID_JUMP:
        bra.w   SVC_$INVALID_SYSCALL    | Invalid syscall number

SVC_$TRAP1_ILLEGAL_USP_JUMP:
        bra.w   FIM_$ILLEGAL_USP        | USP >= 0xCC0000

SVC_$TRAP1_BAD_PTR_JUMP:
        bra.w   SVC_$BAD_USER_PTR       | Argument pointer >= 0xCC0000

|----------------------------------------------------------------------
| External references
|----------------------------------------------------------------------

        .extern FIM_$EXIT               | Return from exception (RTE)
        .extern FIM_$ILLEGAL_USP        | Illegal USP handler
        .extern SVC_$INVALID_SYSCALL    | Invalid syscall number handler
        .extern SVC_$BAD_USER_PTR       | Bad user pointer handler
        .extern SVC_$TRAP1_TABLE        | Syscall table (defined in svc_tables.c)

        .end
