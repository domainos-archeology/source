|
| SVC_$TRAP7 - Domain/OS TRAP #7 System Call Dispatcher
|
| TRAP #7 handles syscalls (0-58) that take 6 arguments.
| User programs invoke system calls via TRAP #7 with:
|   - D0.w = syscall number (0-58)
|   - Six arguments on the user stack at (USP+0x04) .. (USP+0x18)
|
| From: 0x00E7B1D8 (SAU2 map: "E7B1D8  SVC_$TRAP7").  This file covers
| 0x00E7B1D8..0x00E7B23F - SVC_$TRAP7 and its one branch stub; 0x68 bytes,
| no pad (SVC_$TRAP8 starts at 0x00E7B240).
|
| Note: TRAP #6 is not used for SVC calls (its vector points at
|       FIM_$UNDEF_TRAP), which is why there is no trap6.s and no
|       SVC_$TRAP6 in the SAU2 map.
|
| TRAP7 is the furthest dispatcher from SVC_$TRAP4's shared stubs, and its
| two backward branches sit right on the 8-bit limit:
|
|   range check  -> SVC_$TRAP4_INVALID_STUB     (0x00E7B170), disp -110
|   bad USP      -> SVC_$TRAP4_ILLEGAL_USP_STUB (0x00E7B174), disp -128
|
| -128 is the largest displacement a bcc.b can hold, so any size change in
| trap4.s/trap5.s (or a reordering of the objects) breaks the link rather
| than silently mis-branching.  The six bad-pointer tests instead use a
| local stub, SVC_$TRAP7_BAD_PTR_STUB, because 0x00E7B178 is out of reach.
|
| Address space check:
|   0xCC0000 = boundary between user and kernel space
|   USP and all six argument pointers must be < 0xCC0000
|
| Tables:
|   SVC_$TRAP7_TABLE at 0xE7BC7E - handler addresses (59 entries, svc_tables.c)
|

        .include "svc/sau2/svc_macros.inc"

        .text
        .even

|----------------------------------------------------------------------
| SVC_$TRAP7 - 6-argument syscall dispatcher
|
| Input:
|   D0.w = syscall number (0x00-0x3A)
|   (USP+0x04) = arg1 .. (USP+0x18) = arg6
|
| Processing:
|   1. Validate syscall number (< 0x3B)
|   2. Look up handler in SVC_$TRAP7_TABLE
|   3. Validate USP < 0xCC0000
|   4. Validate and push arg6..arg1 (highest address first)
|   5. Call handler, pop 24 bytes, return via RTE
|----------------------------------------------------------------------

        .global SVC_$TRAP7

SVC_$TRAP7:
        | 00e7b1d8  b0 7c 00 3b: CMP.W #imm,D0 (see svc_macros.inc)
        cmp_w_imm 0x3B, 0               | Check syscall number < 59
        bcc.b   SVC_$TRAP4_INVALID_STUB | 00e7b1dc  64 92 -> 0xE7B170

        lsl.w   #2,%d0                  | 00e7b1de  e5 48  D0 = syscall# * 4
        lea     (SVC_$TRAP7_TABLE:w,%pc),%a0 | 00e7b1e0  41 fa 0a 9c -> 0xE7BC7E
        movea.l (0,%a0,%d0:w),%a0       | 00e7b1e4  20 70 00 00  A0 = handler

        move    %usp,%a1                | 00e7b1e8  4e 69
        move.l  #0xCC0000,%d1           | 00e7b1ea  22 3c 00 cc 00 00
        cmpa.l  %d1,%a1                 | 00e7b1f0  b3 c1  USP < 0xCC0000?
        bhi.b   SVC_$TRAP4_ILLEGAL_USP_STUB | 00e7b1f2  62 80 -> 0xE7B174

        move.l  (0x18,%a1),%d0          | 00e7b1f4  20 29 00 18  D0 = arg6
        cmp.l   %d0,%d1                 | 00e7b1f8  b2 80
        bls.b   SVC_$TRAP7_BAD_PTR_STUB | 00e7b1fa  63 40 -> 0xE7B23C
        move.l  %d0,-(%sp)              | 00e7b1fc  2f 00  push arg6

        move.l  (0x14,%a1),%d0          | 00e7b1fe  20 29 00 14  D0 = arg5
        cmp.l   %d0,%d1                 | 00e7b202  b2 80
        bls.b   SVC_$TRAP7_BAD_PTR_STUB | 00e7b204  63 36 -> 0xE7B23C
        move.l  %d0,-(%sp)              | 00e7b206  2f 00  push arg5

        move.l  (0x10,%a1),%d0          | 00e7b208  20 29 00 10  D0 = arg4
        cmp.l   %d0,%d1                 | 00e7b20c  b2 80
        bls.b   SVC_$TRAP7_BAD_PTR_STUB | 00e7b20e  63 2c -> 0xE7B23C
        move.l  %d0,-(%sp)              | 00e7b210  2f 00  push arg4

        move.l  (0x0C,%a1),%d0          | 00e7b212  20 29 00 0c  D0 = arg3
        cmp.l   %d0,%d1                 | 00e7b216  b2 80
        bls.b   SVC_$TRAP7_BAD_PTR_STUB | 00e7b218  63 22 -> 0xE7B23C
        move.l  %d0,-(%sp)              | 00e7b21a  2f 00  push arg3

        move.l  (0x08,%a1),%d0          | 00e7b21c  20 29 00 08  D0 = arg2
        cmp.l   %d0,%d1                 | 00e7b220  b2 80
        bls.b   SVC_$TRAP7_BAD_PTR_STUB | 00e7b222  63 18 -> 0xE7B23C
        move.l  %d0,-(%sp)              | 00e7b224  2f 00  push arg2

        move.l  (0x04,%a1),%d0          | 00e7b226  20 29 00 04  D0 = arg1
        cmp.l   %d0,%d1                 | 00e7b22a  b2 80
        bls.b   SVC_$TRAP7_BAD_PTR_STUB | 00e7b22c  63 0e -> 0xE7B23C
        move.l  %d0,-(%sp)              | 00e7b22e  2f 00  push arg1

        jsr     (%a0)                   | 00e7b230  4e 90  call handler

        adda.w  #24,%sp                 | 00e7b232  de fc 00 18  pop 6 arguments
        jmp     FIM_$EXIT               | 00e7b236  4e f9 00 e2 28 bc

|----------------------------------------------------------------------
| SVC_$TRAP7_BAD_PTR_STUB - 0x00E7B23C
|
| The last four bytes before SVC_$TRAP8 (trap8.s documents it from the
| other side).
|----------------------------------------------------------------------

        .global SVC_$TRAP7_BAD_PTR_STUB

SVC_$TRAP7_BAD_PTR_STUB:
        bra.w   SVC_$BAD_USER_PTR       | 00e7b23c  60 00 00 62 -> 0xE7B2A0

|----------------------------------------------------------------------
| External references
|----------------------------------------------------------------------

        .extern FIM_$EXIT               | 0xE228BC: return from exception (RTE)
        .extern SVC_$TRAP7_TABLE        | 0xE7BC7E: handler table (svc_tables.c)
        .extern SVC_$TRAP4_INVALID_STUB | 0xE7B170: -> SVC_$TRAP8_INVALID
        .extern SVC_$TRAP4_ILLEGAL_USP_STUB | 0xE7B174: -> SVC_$ILLEGAL_USP_JMP
        .extern SVC_$BAD_USER_PTR       | 0xE7B2A0: argument pointer >= 0xCC0000

        .end
