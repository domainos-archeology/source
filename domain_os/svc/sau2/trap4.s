|
| SVC_$TRAP4 - Domain/OS TRAP #4 System Call Dispatcher
|
| TRAP #4 handles syscalls (0-130) that take 4 arguments.
| User programs invoke system calls via TRAP #4 with:
|   - D0.w = syscall number (0-130)
|   - Four arguments on the user stack at (USP+0x04) .. (USP+0x10)
|
| From: 0x00E7B120 (SAU2 map: "E7B120  SVC_$TRAP4").  This file covers
| 0x00E7B120..0x00E7B17B - SVC_$TRAP4 and the three branch stubs that follow
| it; 0x5C bytes total, no pad (SVC_$TRAP5 starts on the next longword).
|
| The three stubs at 0x00E7B170/74/78 are the shared error exits for the
| second half of the dispatcher block: SVC_$TRAP3, SVC_$TRAP5 and SVC_$TRAP7
| branch into them (8-bit, cross-object), as does SVC_$TRAP1 via its own
| bra.w bounce.  All three are therefore global.
|
| Address space check:
|   0xCC0000 = boundary between user and kernel space
|   USP and all four argument pointers must be < 0xCC0000
|
| Tables:
|   SVC_$TRAP4_TABLE at 0xE7B8E6 - handler addresses (131 entries, svc_tables.c)
|

        .include "svc/sau2/svc_macros.inc"

        .section ".text.SVC_$TRAP4","ax",@progbits
        .even

|----------------------------------------------------------------------
| SVC_$TRAP4 - 4-argument syscall dispatcher
|
| Input:
|   D0.w = syscall number (0x00-0x82)
|   (USP+0x04) = arg1 .. (USP+0x10) = arg4
|
| Processing:
|   1. Validate syscall number (< 0x83)
|   2. Look up handler in SVC_$TRAP4_TABLE
|   3. Validate USP < 0xCC0000
|   4. Validate and push arg4..arg1 (highest address first)
|   5. Call handler, pop 16 bytes, return via RTE
|----------------------------------------------------------------------

        .global SVC_$TRAP4

SVC_$TRAP4:
        | 00e7b120  b0 7c 00 83: CMP.W #imm,D0 (see svc_macros.inc)
        cmp_w_imm 0x83, 0               | Check syscall number < 131
        bcc.b   SVC_$TRAP4_INVALID_STUB | 00e7b124  64 4a -> 0xE7B170

        lsl.w   #2,%d0                  | 00e7b126  e5 48  D0 = syscall# * 4
        lea     (SVC_$TRAP4_TABLE:w,%pc),%a0 | 00e7b128  41 fa 07 bc -> 0xE7B8E6
        movea.l (0,%a0,%d0:w),%a0       | 00e7b12c  20 70 00 00  A0 = handler

        move    %usp,%a1                | 00e7b130  4e 69
        move.l  #0xCC0000,%d1           | 00e7b132  22 3c 00 cc 00 00
        cmpa.l  %d1,%a1                 | 00e7b138  b3 c1  USP < 0xCC0000?
        bhi.b   SVC_$TRAP4_ILLEGAL_USP_STUB | 00e7b13a  62 38 -> 0xE7B174

        move.l  (0x10,%a1),%d0          | 00e7b13c  20 29 00 10  D0 = arg4
        cmp.l   %d0,%d1                 | 00e7b140  b2 80
        bls.b   SVC_$TRAP4_BAD_PTR_STUB | 00e7b142  63 34 -> 0xE7B178
        move.l  %d0,-(%sp)              | 00e7b144  2f 00  push arg4

        move.l  (0x0C,%a1),%d0          | 00e7b146  20 29 00 0c  D0 = arg3
        cmp.l   %d0,%d1                 | 00e7b14a  b2 80
        bls.b   SVC_$TRAP4_BAD_PTR_STUB | 00e7b14c  63 2a -> 0xE7B178
        move.l  %d0,-(%sp)              | 00e7b14e  2f 00  push arg3

        move.l  (0x08,%a1),%d0          | 00e7b150  20 29 00 08  D0 = arg2
        cmp.l   %d0,%d1                 | 00e7b154  b2 80
        bls.b   SVC_$TRAP4_BAD_PTR_STUB | 00e7b156  63 20 -> 0xE7B178
        move.l  %d0,-(%sp)              | 00e7b158  2f 00  push arg2

        move.l  (0x04,%a1),%d0          | 00e7b15a  20 29 00 04  D0 = arg1
        cmp.l   %d0,%d1                 | 00e7b15e  b2 80
        bls.b   SVC_$TRAP4_BAD_PTR_STUB | 00e7b160  63 16 -> 0xE7B178
        move.l  %d0,-(%sp)              | 00e7b162  2f 00  push arg1

        jsr     (%a0)                   | 00e7b164  4e 90  call handler

        adda.w  #16,%sp                 | 00e7b166  de fc 00 10  pop 4 arguments
        jmp     FIM_$EXIT               | 00e7b16a  4e f9 00 e2 28 bc

|----------------------------------------------------------------------
| Shared branch stubs (0x00E7B170, 0x00E7B174, 0x00E7B178)
|
| SVC_$TRAP4_ILLEGAL_USP_STUB targets SVC_$ILLEGAL_USP_JMP (0x00E7B2C6),
| not SVC_$ILLEGAL_USP_UNLK (0x00E7B2C4): the fixed-argument dispatchers
| build no LINK frame and must skip the `unlk %a6'.
|----------------------------------------------------------------------

        .global SVC_$TRAP4_INVALID_STUB
        .global SVC_$TRAP4_ILLEGAL_USP_STUB
        .global SVC_$TRAP4_BAD_PTR_STUB

SVC_$TRAP4_INVALID_STUB:
        bra.w   SVC_$TRAP8_INVALID      | 00e7b170  60 00 01 1c -> 0xE7B28E

SVC_$TRAP4_ILLEGAL_USP_STUB:
        bra.w   SVC_$ILLEGAL_USP_JMP    | 00e7b174  60 00 01 50 -> 0xE7B2C6

SVC_$TRAP4_BAD_PTR_STUB:
        bra.w   SVC_$BAD_USER_PTR       | 00e7b178  60 00 01 26 -> 0xE7B2A0

|----------------------------------------------------------------------
| External references
|----------------------------------------------------------------------

        .extern FIM_$EXIT               | 0xE228BC: return from exception (RTE)
        .extern SVC_$TRAP4_TABLE        | 0xE7B8E6: handler table (svc_tables.c)
        .extern SVC_$TRAP8_INVALID      | 0xE7B28E: invalid syscall number
        .extern SVC_$ILLEGAL_USP_JMP    | 0xE7B2C6: jmp FIM_$ILLEGAL_USP (no unlk)
        .extern SVC_$BAD_USER_PTR       | 0xE7B2A0: argument pointer >= 0xCC0000

        .end
