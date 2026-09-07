|
| SVC_$TRAP5 - Domain/OS TRAP #5 System Call Dispatcher
|
| This is the main entry point for complex system calls in Domain/OS.
| User programs invoke system calls via TRAP #5 with:
|   - D0.w = syscall number (0-98)
|   - Arguments on user stack (up to 5 longwords)
|
| From: 0x00e7b17c
|
| Memory layout:
|   0x00e7b170: Branch to invalid syscall handler
|   0x00e7b174: Branch to illegal USP handler
|   0x00e7b178: Branch to bad user pointer handler
|   0x00e7b17c: SVC_$TRAP5 main entry
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

SVC_$INVALID_JUMP:
        bra.w   SVC_$INVALID_SYSCALL    | 0xe7b170: invalid syscall number

SVC_$ILLEGAL_USP_JUMP:
        bra.w   FIM_$ILLEGAL_USP        | 0xe7b174: USP >= 0xCC0000

SVC_$BAD_PTR_JUMP:
        bra.w   SVC_$BAD_USER_PTR       | 0xe7b178: argument pointer >= 0xCC0000

|----------------------------------------------------------------------
| SVC_$TRAP5 - Main system call entry point
|
| Input:
|   D0.w = syscall number (0x00-0x62)
|   USP points to argument frame:
|     (USP+0x04) = arg1
|     (USP+0x08) = arg2
|     (USP+0x0C) = arg3
|     (USP+0x10) = arg4
|     (USP+0x14) = arg5
|
| Processing:
|   1. Validate syscall number (< 0x63)
|   2. Look up handler in SVC_$TRAP5_TABLE (defined in svc_tables.c)
|   3. Validate USP < 0xCC0000
|   4. Validate and copy up to 5 arguments from user stack
|   5. Call handler
|   6. Clean up stack and return via RTE
|----------------------------------------------------------------------

        .global SVC_$TRAP5

SVC_$TRAP5:
        cmp.w   #0x63,%d0               | Check syscall number limit
        bcc.b   SVC_$INVALID_JUMP       | Branch if D0 >= 99

        lsl.w   #2,%d0                  | D0 = syscall_num * 4 (table index)
        lea     SVC_$TRAP5_TABLE,%a0    | A0 = table base (from svc_tables.c)
        movea.l (0,%a0,%d0:w),%a0       | A0 = handler address from table

        move    %usp,%a1                | A1 = user stack pointer
        move.l  #0xCC0000,%d1           | D1 = user/kernel boundary

        cmpa.l  %d1,%a1                 | Check USP < 0xCC0000
        bhi.b   SVC_$ILLEGAL_USP_JUMP   | Branch if USP in kernel space

        | Validate and push arg5 (offset 0x14)
        move.l  (0x14,%a1),%d0          | D0 = arg5
        cmp.l   %d0,%d1                 | Check arg5 < 0xCC0000
        bls.b   SVC_$BAD_PTR_JUMP       | Branch if arg5 >= boundary
        move.l  %d0,-(%sp)              | Push arg5 to supervisor stack

        | Validate and push arg4 (offset 0x10)
        move.l  (0x10,%a1),%d0          | D0 = arg4
        cmp.l   %d0,%d1                 | Check arg4 < 0xCC0000
        bls.b   SVC_$BAD_PTR_JUMP       | Branch if arg4 >= boundary
        move.l  %d0,-(%sp)              | Push arg4

        | Validate and push arg3 (offset 0x0C)
        move.l  (0x0C,%a1),%d0          | D0 = arg3
        cmp.l   %d0,%d1                 | Check arg3 < 0xCC0000
        bls.b   SVC_$BAD_PTR_JUMP       | Branch if arg3 >= boundary
        move.l  %d0,-(%sp)              | Push arg3

        | Validate and push arg2 (offset 0x08)
        move.l  (0x08,%a1),%d0          | D0 = arg2
        cmp.l   %d0,%d1                 | Check arg2 < 0xCC0000
        bls.b   SVC_$BAD_PTR_JUMP       | Branch if arg2 >= boundary
        move.l  %d0,-(%sp)              | Push arg2

        | Validate and push arg1 (offset 0x04)
        move.l  (0x04,%a1),%d0          | D0 = arg1
        cmp.l   %d0,%d1                 | Check arg1 < 0xCC0000
        bls.b   SVC_$BAD_PTR_JUMP       | Branch if arg1 >= boundary
        move.l  %d0,-(%sp)              | Push arg1

        | Call the syscall handler
        jsr     (%a0)                   | Call handler(arg1,arg2,arg3,arg4,arg5)

        | Clean up and return to user mode
        adda.w  #0x14,%sp               | Remove 5 arguments (20 bytes)
        jmp     FIM_$EXIT               | Return via RTE

|----------------------------------------------------------------------
| The fault tail (0x00E7B298 SVC_$INVALID_SYSCALL, 0x00E7B2A0
| SVC_$BAD_USER_PTR, 0x00E7B2A6 SVC_$GENERATE_FAULT, 0x00E7B2C4
| SVC_$ILLEGAL_USP_UNLK, 0x00E7B2CC SVC_$UNIMPLEMENTED) that these stubs
| branch into is emitted by svc/sau2/trap8.s - it is the block that follows
| SVC_$TRAP8 in the image, and SVC_$TRAP8 reaches it with 8-bit branches that
| cannot relocate across object files.  TRAP #5 gets there through the bra.w
| stubs above.
|----------------------------------------------------------------------

|----------------------------------------------------------------------
| External references
|----------------------------------------------------------------------

        .extern FIM_$EXIT               | Return from exception (RTE)
        .extern FIM_$ILLEGAL_USP        | Illegal USP handler
        .extern SVC_$INVALID_SYSCALL    | 0xE7B298: invalid syscall (trap8.s)
        .extern SVC_$BAD_USER_PTR       | 0xE7B2A0: bad user pointer (trap8.s)
        .extern SVC_$TRAP5_TABLE        | Syscall table (defined in svc_tables.c)

        .end
