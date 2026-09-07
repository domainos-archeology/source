|
| SVC_$TRAP5 - Domain/OS TRAP #5 System Call Dispatcher
|
| TRAP #5 handles syscalls (0-98) that take 5 arguments; it is the busiest
| of the fixed-argument dispatchers.  User programs invoke system calls via
| TRAP #5 with:
|   - D0.w = syscall number (0-98)
|   - Five arguments on the user stack at (USP+0x04) .. (USP+0x14)
|
| From: 0x00E7B17C (SAU2 map: "E7B17C  SVC_$TRAP5").  This file covers
| 0x00E7B17C..0x00E7B1D7 - SVC_$TRAP5 plus the image's two-byte realignment
| pad; 0x5C bytes total.
|
| TRAP5 owns no branch stubs of its own; all three error paths are 8-bit
| branches back into SVC_$TRAP4's stubs (trap4.s):
|
|   range check  -> SVC_$TRAP4_INVALID_STUB     (0x00E7B170)
|   bad USP      -> SVC_$TRAP4_ILLEGAL_USP_STUB (0x00E7B174)
|   bad user ptr -> SVC_$TRAP4_BAD_PTR_STUB     (0x00E7B178)
|
| (An earlier transcription of this file emitted those three stubs here, at
| the head of TRAP5.  They belong to TRAP4's region - bead source-avfg.)
|
| Address space check:
|   0xCC0000 = boundary between user and kernel space
|   USP and all five argument pointers must be < 0xCC0000
|
| Tables:
|   SVC_$TRAP5_TABLE at 0xE7BAF2 - handler addresses (99 entries, svc_tables.c)
|

        .include "svc/sau2/svc_macros.inc"

        .text
        .even

|----------------------------------------------------------------------
| SVC_$TRAP5 - 5-argument syscall dispatcher
|
| Input:
|   D0.w = syscall number (0x00-0x62)
|   (USP+0x04) = arg1 .. (USP+0x14) = arg5
|
| Processing:
|   1. Validate syscall number (< 0x63)
|   2. Look up handler in SVC_$TRAP5_TABLE
|   3. Validate USP < 0xCC0000
|   4. Validate and push arg5..arg1 (highest address first)
|   5. Call handler, pop 20 bytes, return via RTE
|----------------------------------------------------------------------

        .global SVC_$TRAP5

SVC_$TRAP5:
        | 00e7b17c  b0 7c 00 63: CMP.W #imm,D0 (see svc_macros.inc)
        cmp_w_imm 0x63, 0               | Check syscall number < 99
        bcc.b   SVC_$TRAP4_INVALID_STUB | 00e7b180  64 ee -> 0xE7B170

        lsl.w   #2,%d0                  | 00e7b182  e5 48  D0 = syscall# * 4
        lea     (SVC_$TRAP5_TABLE:w,%pc),%a0 | 00e7b184  41 fa 09 6c -> 0xE7BAF2
        movea.l (0,%a0,%d0:w),%a0       | 00e7b188  20 70 00 00  A0 = handler

        move    %usp,%a1                | 00e7b18c  4e 69
        move.l  #0xCC0000,%d1           | 00e7b18e  22 3c 00 cc 00 00
        cmpa.l  %d1,%a1                 | 00e7b194  b3 c1  USP < 0xCC0000?
        bhi.b   SVC_$TRAP4_ILLEGAL_USP_STUB | 00e7b196  62 dc -> 0xE7B174

        move.l  (0x14,%a1),%d0          | 00e7b198  20 29 00 14  D0 = arg5
        cmp.l   %d0,%d1                 | 00e7b19c  b2 80
        bls.b   SVC_$TRAP4_BAD_PTR_STUB | 00e7b19e  63 d8 -> 0xE7B178
        move.l  %d0,-(%sp)              | 00e7b1a0  2f 00  push arg5

        move.l  (0x10,%a1),%d0          | 00e7b1a2  20 29 00 10  D0 = arg4
        cmp.l   %d0,%d1                 | 00e7b1a6  b2 80
        bls.b   SVC_$TRAP4_BAD_PTR_STUB | 00e7b1a8  63 ce -> 0xE7B178
        move.l  %d0,-(%sp)              | 00e7b1aa  2f 00  push arg4

        move.l  (0x0C,%a1),%d0          | 00e7b1ac  20 29 00 0c  D0 = arg3
        cmp.l   %d0,%d1                 | 00e7b1b0  b2 80
        bls.b   SVC_$TRAP4_BAD_PTR_STUB | 00e7b1b2  63 c4 -> 0xE7B178
        move.l  %d0,-(%sp)              | 00e7b1b4  2f 00  push arg3

        move.l  (0x08,%a1),%d0          | 00e7b1b6  20 29 00 08  D0 = arg2
        cmp.l   %d0,%d1                 | 00e7b1ba  b2 80
        bls.b   SVC_$TRAP4_BAD_PTR_STUB | 00e7b1bc  63 ba -> 0xE7B178
        move.l  %d0,-(%sp)              | 00e7b1be  2f 00  push arg2

        move.l  (0x04,%a1),%d0          | 00e7b1c0  20 29 00 04  D0 = arg1
        cmp.l   %d0,%d1                 | 00e7b1c4  b2 80
        bls.b   SVC_$TRAP4_BAD_PTR_STUB | 00e7b1c6  63 b0 -> 0xE7B178
        move.l  %d0,-(%sp)              | 00e7b1c8  2f 00  push arg1

        jsr     (%a0)                   | 00e7b1ca  4e 90  call handler

        adda.w  #20,%sp                 | 00e7b1cc  de fc 00 14  pop 5 arguments
        jmp     FIM_$EXIT               | 00e7b1d0  4e f9 00 e2 28 bc

        .short  0                       | 00e7b1d6  00 00: image pad, realigns
                                        | SVC_$TRAP7 to a longword boundary

|----------------------------------------------------------------------
| External references
|----------------------------------------------------------------------

        .extern FIM_$EXIT               | 0xE228BC: return from exception (RTE)
        .extern SVC_$TRAP5_TABLE        | 0xE7BAF2: handler table (svc_tables.c)
        .extern SVC_$TRAP4_INVALID_STUB | 0xE7B170: -> SVC_$TRAP8_INVALID
        .extern SVC_$TRAP4_ILLEGAL_USP_STUB | 0xE7B174: -> SVC_$ILLEGAL_USP_JMP
        .extern SVC_$TRAP4_BAD_PTR_STUB | 0xE7B178: -> SVC_$BAD_USER_PTR

        .end
