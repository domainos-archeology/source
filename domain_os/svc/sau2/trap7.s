|
| SVC_$TRAP7 - Domain/OS TRAP #7 System Call Dispatcher
|
| This is the entry point for 6-argument system calls in Domain/OS.
| User programs invoke system calls via TRAP #7 with:
|   - D0.w = syscall number (0-58)
|   - Arguments on user stack (up to 6 longwords)
|
| From: 0x00e7b1d8
|
| Note: TRAP #6 is not used for SVC calls (points to FIM_$UNDEF_TRAP).
|       This dispatcher handles M68K TRAP #7 instruction.
|
| Memory layout:
|   0x00e7b1cc: Branch to invalid syscall handler
|   0x00e7b1d0: Branch to illegal USP handler
|   0x00e7b1d4: Branch to bad user pointer handler
|   0x00e7b1d8: SVC_$TRAP7 main entry
|
| Address space check:
|   0xCC0000 = boundary between user and kernel space
|   USP and all argument pointers must be < 0xCC0000
|

        .text
        .even

|----------------------------------------------------------------------
| Branch stubs for error handlers (these precede main entry)
|----------------------------------------------------------------------

SVC_$TRAP7_INVALID_JUMP:
        bra.w   SVC_$INVALID_SYSCALL    | 0xe7b1cc: invalid syscall number

SVC_$TRAP7_ILLEGAL_USP_JUMP:
        bra.w   FIM_$ILLEGAL_USP        | 0xe7b1d0: USP >= 0xCC0000

SVC_$TRAP7_BAD_PTR_JUMP:
        bra.w   SVC_$BAD_USER_PTR       | 0xe7b1d4: argument pointer >= 0xCC0000

|----------------------------------------------------------------------
| SVC_$TRAP7 - Main system call entry point
|
| Input:
|   D0.w = syscall number (0x00-0x3A)
|   USP points to argument frame:
|     (USP+0x04) = arg1
|     (USP+0x08) = arg2
|     (USP+0x0C) = arg3
|     (USP+0x10) = arg4
|     (USP+0x14) = arg5
|     (USP+0x18) = arg6
|
| Processing:
|   1. Validate syscall number (< 0x3B)
|   2. Look up handler in SVC_$TRAP7_TABLE (defined in svc_tables.c)
|   3. Validate USP < 0xCC0000
|   4. Validate and copy 6 arguments from user stack
|   5. Call handler
|   6. Clean up stack and return via RTE
|----------------------------------------------------------------------

        .global SVC_$TRAP7

SVC_$TRAP7:
        cmp.w   #0x3B,%d0               | Check syscall number limit
        bcc.b   SVC_$TRAP7_INVALID_JUMP | Branch if D0 >= 59

        lsl.w   #2,%d0                  | D0 = syscall_num * 4 (table index)
        lea     SVC_$TRAP7_TABLE,%a0    | A0 = table base (from svc_tables.c)
        movea.l (0,%a0,%d0:w),%a0       | A0 = handler address from table

        move    %usp,%a1                | A1 = user stack pointer
        move.l  #0xCC0000,%d1           | D1 = user/kernel boundary

        cmpa.l  %d1,%a1                 | Check USP < 0xCC0000
        bhi.b   SVC_$TRAP7_ILLEGAL_USP_JUMP | Branch if USP in kernel space

        | Validate and push arg6 (offset 0x18)
        move.l  (0x18,%a1),%d0          | D0 = arg6
        cmp.l   %d0,%d1                 | Check arg6 < 0xCC0000
        bls.b   SVC_$TRAP7_BAD_PTR_JUMP | Branch if arg6 >= boundary
        move.l  %d0,-(%sp)              | Push arg6 to supervisor stack

        | Validate and push arg5 (offset 0x14)
        move.l  (0x14,%a1),%d0          | D0 = arg5
        cmp.l   %d0,%d1                 | Check arg5 < 0xCC0000
        bls.b   SVC_$TRAP7_BAD_PTR_JUMP | Branch if arg5 >= boundary
        move.l  %d0,-(%sp)              | Push arg5

        | Validate and push arg4 (offset 0x10)
        move.l  (0x10,%a1),%d0          | D0 = arg4
        cmp.l   %d0,%d1                 | Check arg4 < 0xCC0000
        bls.b   SVC_$TRAP7_BAD_PTR_JUMP | Branch if arg4 >= boundary
        move.l  %d0,-(%sp)              | Push arg4

        | Validate and push arg3 (offset 0x0C)
        move.l  (0x0C,%a1),%d0          | D0 = arg3
        cmp.l   %d0,%d1                 | Check arg3 < 0xCC0000
        bls.b   SVC_$TRAP7_BAD_PTR_JUMP | Branch if arg3 >= boundary
        move.l  %d0,-(%sp)              | Push arg3

        | Validate and push arg2 (offset 0x08)
        move.l  (0x08,%a1),%d0          | D0 = arg2
        cmp.l   %d0,%d1                 | Check arg2 < 0xCC0000
        bls.b   SVC_$TRAP7_BAD_PTR_JUMP | Branch if arg2 >= boundary
        move.l  %d0,-(%sp)              | Push arg2

        | Validate and push arg1 (offset 0x04)
        move.l  (0x04,%a1),%d0          | D0 = arg1
        cmp.l   %d0,%d1                 | Check arg1 < 0xCC0000
        bls.b   SVC_$TRAP7_BAD_PTR_JUMP | Branch if arg1 >= boundary
        move.l  %d0,-(%sp)              | Push arg1

        | Call the syscall handler
        jsr     (%a0)                   | Call handler(arg1,arg2,arg3,arg4,arg5,arg6)

        | Clean up and return to user mode
        adda.w  #0x18,%sp               | Remove 6 arguments (24 bytes)
        jmp     FIM_$EXIT               | Return via RTE

|----------------------------------------------------------------------
| External references
|----------------------------------------------------------------------

        .extern FIM_$EXIT               | Return from exception (RTE)
        .extern FIM_$ILLEGAL_USP        | Illegal USP handler
        .extern SVC_$INVALID_SYSCALL    | Invalid syscall handler (in trap5.s)
        .extern SVC_$BAD_USER_PTR       | Bad user pointer handler (in trap5.s)
        .extern SVC_$TRAP7_TABLE        | Syscall table (defined in svc_tables.c)

        .end
