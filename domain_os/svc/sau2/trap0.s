|
| SVC_$TRAP0 - Domain/OS TRAP #0 System Call Dispatcher
|
| TRAP #0 handles "simple" system calls (0-31) that take no arguments
| or handle their own argument validation.  Unlike TRAP #1-5/7/8 this
| handler does NOT validate the user stack pointer or copy arguments.
|
| From: 0x00E7B044 (SAU2 map: "E7B044  SVC_$TRAP0", in segment SVC_CATCHER
| at 0xE7B044, size 0xE40).  This file covers 0x00E7B044..0x00E7B05B - the
| whole of SVC_$TRAP0, 0x18 bytes.
|
| Convention:
|   - D0.w = syscall number (0x00-0x1F)
|   - No arguments passed via user stack
|   - Handler called directly, responsible for own argument handling
|
| Out-of-range dispatch (the branch-chain trick):
|   The `bcc.b' below does not jump to an error handler; it jumps into the
|   *middle* of SVC_$TRAP1, at SVC_$TRAP1_RANGE_CHECK (0x00E7B060), which is
|   TRAP1's own `bcc.b'.  Nothing between the two branches touches the CCR,
|   so TRAP1's branch sees this compare's carry (still clear) and takes its
|   own out-of-range path.  Two range checks share one error stub.
|
| Tables:
|   SVC_$TRAP0_TABLE at 0xE7B2DE - handler addresses (32 entries, svc_tables.c)
|

        .include "svc/sau2/svc_macros.inc"

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
|   2. Look up handler in SVC_$TRAP0_TABLE
|   3. Call handler directly (no args)
|   4. Return via FIM_$EXIT (RTE)
|----------------------------------------------------------------------

        .global SVC_$TRAP0

SVC_$TRAP0:
        | 00e7b044  b0 7c 00 20: CMP.W #imm,D0 (see svc_macros.inc)
        cmp_w_imm 0x20, 0               | Check syscall number < 32
        bcc.b   SVC_$TRAP1_RANGE_CHECK  | 00e7b048  64 16 -> 0xE7B060

        lsl.w   #2,%d0                  | 00e7b04a  e5 48  D0 = syscall# * 4
        lea     (SVC_$TRAP0_TABLE:w,%pc),%a1 | 00e7b04c  43 fa 02 90 -> 0xE7B2DE
        movea.l (0,%a1,%d0:w),%a0       | 00e7b050  20 71 00 00  A0 = handler

        jsr     (%a0)                   | 00e7b054  4e 90  call handler (no args)
        jmp     FIM_$EXIT               | 00e7b056  4e f9 00 e2 28 bc

|----------------------------------------------------------------------
| External references
|----------------------------------------------------------------------

        .extern FIM_$EXIT               | 0xE228BC: return from exception (RTE)
        .extern SVC_$TRAP0_TABLE        | 0xE7B2DE: handler table (svc_tables.c)
        .extern SVC_$TRAP1_RANGE_CHECK  | 0xE7B060: TRAP1's range-check branch

        .end
