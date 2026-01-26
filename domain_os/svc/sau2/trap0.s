|
| SVC_$TRAP0 - Domain/OS TRAP #0 System Call Dispatcher
|
| TRAP #0 handles "simple" system calls (0-31) that take no arguments
| or handle their own argument validation. Unlike TRAP #5, this handler
| does NOT validate user stack pointers or copy arguments.
|
| From: 0x00e7b044
|
| Convention:
|   - D0.w = syscall number (0-31, 0x00-0x1F)
|   - No arguments passed via user stack
|   - Handler called directly, responsible for own argument handling
|
| Note: If syscall number >= 32, control falls through to TRAP1's
| range check, eventually reaching the error handler.
|

        .text
        .even

|----------------------------------------------------------------------
| SVC_$TRAP0 - Simple syscall dispatcher (no argument passing)
|
| Input:
|   D0.w = syscall number (0x00-0x1F)
|
| Processing:
|   1. Validate syscall number < 32
|   2. Look up handler in SVC_$TRAP0_TABLE (defined in svc_tables.c)
|   3. Call handler directly (no args)
|   4. Return via FIM_$EXIT (RTE)
|
| Original bytes at 0xe7b044:
|   b0 7c 00 20    cmp.w #0x20,D0
|   64 16          bcc.b SVC_$TRAP1_RANGE_CHECK
|   e5 48          lsl.w #2,D0
|   43 fa 02 90    lea (SVC_$TRAP0_TABLE,PC),A1
|   20 71 00 00    movea.l (0,A1,D0.w),A0
|   4e 90          jsr (A0)
|   4e f9 00 e2 28 bc  jmp FIM_$EXIT
|----------------------------------------------------------------------

        .global SVC_$TRAP0

SVC_$TRAP0:
        cmp.w   #0x20,%d0               | Check syscall number < 32
        bcc.b   SVC_$TRAP1_RANGE_CHECK  | If >= 32, fall through to TRAP1 check

        lsl.w   #2,%d0                  | D0 = syscall_num * 4 (table index)
        lea     SVC_$TRAP0_TABLE,%a1    | A1 = table base (from svc_tables.c)
        movea.l (0,%a1,%d0:w),%a0       | A0 = handler address

        jsr     (%a0)                   | Call handler (no arguments)
        jmp     FIM_$EXIT               | Return to user mode via RTE

|----------------------------------------------------------------------
| External references
|----------------------------------------------------------------------

        .extern FIM_$EXIT               | Return from exception (RTE)
        .extern SVC_$TRAP1_RANGE_CHECK  | TRAP1's range validation (fallthrough)
        .extern SVC_$TRAP0_TABLE        | Syscall table (defined in svc_tables.c)

        .end
