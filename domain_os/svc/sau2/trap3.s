|
| SVC_$TRAP3 - Domain/OS TRAP #3 System Call Dispatcher
|
| TRAP #3 handles syscalls (0-154) that take 3 arguments.
| User programs invoke system calls via TRAP #3 with:
|   - D0.w = syscall number (0-154)
|   - Three arguments on the user stack at (USP+0x04) .. (USP+0x0C)
|
| From: 0x00E7B0D8 (SAU2 map: "E7B0D8  SVC_$TRAP3").  This file covers
| 0x00E7B0D8..0x00E7B11F - SVC_$TRAP3 plus the image's two-byte realignment
| pad; 0x48 bytes total.
|
| TRAP3 owns no branch stubs of its own: all three error paths are 8-bit
| branches into the stubs held by its neighbours,
|
|   range check  -> SVC_$TRAP2_INVALID_STUB     (0x00E7B0CE, trap2.s)
|   bad USP      -> SVC_$TRAP2_ILLEGAL_USP_STUB (0x00E7B0D2, trap2.s)
|   bad user ptr -> SVC_$TRAP4_BAD_PTR_STUB     (0x00E7B178, trap4.s)
|
| The forward branch to 0x00E7B178 is +0x7C, four bytes short of the 8-bit
| limit; it only resolves because trap3.o and trap4.o are emitted at exactly
| their image sizes and the linker keeps them adjacent.
|
| Address space check:
|   0xCC0000 = boundary between user and kernel space
|   USP and all three argument pointers must be < 0xCC0000
|
| Tables:
|   SVC_$TRAP3_TABLE at 0xE7B67A - handler addresses (155 entries, svc_tables.c)
|

        .include "svc/sau2/svc_macros.inc"

        .text
        .even

|----------------------------------------------------------------------
| SVC_$TRAP3 - 3-argument syscall dispatcher
|
| Input:
|   D0.w = syscall number (0x00-0x9A)
|   (USP+0x04) = arg1, (USP+0x08) = arg2, (USP+0x0C) = arg3
|
| Processing:
|   1. Validate syscall number (< 0x9B)
|   2. Look up handler in SVC_$TRAP3_TABLE
|   3. Validate USP < 0xCC0000
|   4. Validate and push arg3, arg2, arg1 (highest address first)
|   5. Call handler, pop 12 bytes, return via RTE
|----------------------------------------------------------------------

        .global SVC_$TRAP3

SVC_$TRAP3:
        | 00e7b0d8  b0 7c 00 9b: CMP.W #imm,D0 (see svc_macros.inc)
        cmp_w_imm 0x9B, 0               | Check syscall number < 155
        bcc.b   SVC_$TRAP2_INVALID_STUB | 00e7b0dc  64 f0 -> 0xE7B0CE

        lsl.w   #2,%d0                  | 00e7b0de  e5 48  D0 = syscall# * 4
        lea     (SVC_$TRAP3_TABLE:w,%pc),%a0 | 00e7b0e0  41 fa 05 98 -> 0xE7B67A
        movea.l (0,%a0,%d0:w),%a0       | 00e7b0e4  20 70 00 00  A0 = handler

        move    %usp,%a1                | 00e7b0e8  4e 69
        move.l  #0xCC0000,%d1           | 00e7b0ea  22 3c 00 cc 00 00
        cmpa.l  %d1,%a1                 | 00e7b0f0  b3 c1  USP < 0xCC0000?
        bhi.b   SVC_$TRAP2_ILLEGAL_USP_STUB | 00e7b0f2  62 de -> 0xE7B0D2

        move.l  (0x0C,%a1),%d0          | 00e7b0f4  20 29 00 0c  D0 = arg3
        cmp.l   %d0,%d1                 | 00e7b0f8  b2 80
        bls.b   SVC_$TRAP4_BAD_PTR_STUB | 00e7b0fa  63 7c -> 0xE7B178
        move.l  %d0,-(%sp)              | 00e7b0fc  2f 00  push arg3

        move.l  (0x08,%a1),%d0          | 00e7b0fe  20 29 00 08  D0 = arg2
        cmp.l   %d0,%d1                 | 00e7b102  b2 80
        bls.b   SVC_$TRAP4_BAD_PTR_STUB | 00e7b104  63 72 -> 0xE7B178
        move.l  %d0,-(%sp)              | 00e7b106  2f 00  push arg2

        move.l  (0x04,%a1),%d0          | 00e7b108  20 29 00 04  D0 = arg1
        cmp.l   %d0,%d1                 | 00e7b10c  b2 80
        bls.b   SVC_$TRAP4_BAD_PTR_STUB | 00e7b10e  63 68 -> 0xE7B178
        move.l  %d0,-(%sp)              | 00e7b110  2f 00  push arg1

        jsr     (%a0)                   | 00e7b112  4e 90  call handler

        adda.w  #12,%sp                 | 00e7b114  de fc 00 0c  pop 3 arguments
        jmp     FIM_$EXIT               | 00e7b118  4e f9 00 e2 28 bc

        .short  0                       | 00e7b11e  00 00: image pad, realigns
                                        | SVC_$TRAP4 to a longword boundary

|----------------------------------------------------------------------
| External references
|----------------------------------------------------------------------

        .extern FIM_$EXIT               | 0xE228BC: return from exception (RTE)
        .extern SVC_$TRAP3_TABLE        | 0xE7B67A: handler table (svc_tables.c)
        .extern SVC_$TRAP2_INVALID_STUB | 0xE7B0CE: -> SVC_$TRAP8_INVALID
        .extern SVC_$TRAP2_ILLEGAL_USP_STUB | 0xE7B0D2: -> SVC_$ILLEGAL_USP_JMP
        .extern SVC_$TRAP4_BAD_PTR_STUB | 0xE7B178: -> SVC_$BAD_USER_PTR

        .end
