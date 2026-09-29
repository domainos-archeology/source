|
| SVC_$TRAP2 - Domain/OS TRAP #2 System Call Dispatcher
|
| Byte gate (source-6psc; tools/asm_compare.py, `make check'): encodings
| identical to the image (modulo the documented widenings); address
| operands resolve to our objects.
|
| TRAP #2 handles syscalls (0-132) that take 2 arguments.
| User programs invoke system calls via TRAP #2 with:
|   - D0.w = syscall number (0-132)
|   - Two arguments on the user stack at (USP+0x04) and (USP+0x08)
|
| From: 0x00E7B094 (SAU2 map: "E7B094  SVC_$TRAP2").  This file covers
| 0x00E7B094..0x00E7B0D7 - SVC_$TRAP2, the two branch stubs that follow it,
| and the image's two-byte realignment pad; 0x44 bytes total.
|
| The two stubs are shared: SVC_$TRAP1 and SVC_$TRAP3 branch into them
| (8-bit, cross-object), which is why both are global.  Conversely TRAP2's
| own bad-pointer path branches back into SVC_$TRAP1_BAD_PTR_STUB.
|
| Note that SVC_$TRAP2_ILLEGAL_USP_STUB targets SVC_$ILLEGAL_USP_JMP
| (0x00E7B2C6) rather than SVC_$ILLEGAL_USP_UNLK (0x00E7B2C4): the
| fixed-argument dispatchers build no LINK frame, so they must skip the
| `unlk %a6' that SVC_$TRAP8 needs.
|
| Address space check:
|   0xCC0000 = boundary between user and kernel space
|   USP and both argument pointers must be < 0xCC0000
|
| Tables:
|   SVC_$TRAP2_TABLE at 0xE7B466 - handler addresses (133 entries, svc_tables.c)
|

        .include "svc/sau2/svc_macros.inc"

        .section ".text.SVC_$TRAP2","ax",@progbits
        .even

|----------------------------------------------------------------------
| SVC_$TRAP2 - 2-argument syscall dispatcher
|
| Input:
|   D0.w = syscall number (0x00-0x84, i.e. 0-132)
|   (USP+0x04) = arg1, (USP+0x08) = arg2
|
| Processing:
|   1. Validate syscall number (< 0x85)
|   2. Look up handler in SVC_$TRAP2_TABLE
|   3. Validate USP < 0xCC0000
|   4. Validate and push arg2 then arg1
|   5. Call handler, pop 8 bytes, return via RTE
|----------------------------------------------------------------------

        .global SVC_$TRAP2
        .global SVC_$TRAP2_INVALID_STUB
        .global SVC_$TRAP2_ILLEGAL_USP_STUB

SVC_$TRAP2:
        | 00e7b094  b0 7c 00 85: CMP.W #imm,D0 (see svc_macros.inc)
        cmp_w_imm 0x85, 0               | Check syscall number < 133
        bcc.b   SVC_$TRAP2_INVALID_STUB | 00e7b098  64 34 -> 0xE7B0CE

        lsl.w   #2,%d0                  | 00e7b09a  e5 48  D0 = syscall# * 4
        lea     (SVC_$TRAP2_TABLE:w,%pc),%a0 | 00e7b09c  41 fa 03 c8 -> 0xE7B466
        movea.l (0,%a0,%d0:w),%a0       | 00e7b0a0  20 70 00 00  A0 = handler

        move    %usp,%a1                | 00e7b0a4  4e 69
        move.l  #0xCC0000,%d1           | 00e7b0a6  22 3c 00 cc 00 00
        cmpa.l  %d1,%a1                 | 00e7b0ac  b3 c1  USP < 0xCC0000?
        bhi.b   SVC_$TRAP2_ILLEGAL_USP_STUB | 00e7b0ae  62 22 -> 0xE7B0D2

        move.l  (0x08,%a1),%d0          | 00e7b0b0  20 29 00 08  D0 = arg2
        cmp.l   %d0,%d1                 | 00e7b0b4  b2 80
        bls.b   SVC_$TRAP1_BAD_PTR_STUB | 00e7b0b6  63 d6 -> 0xE7B08E
        move.l  %d0,-(%sp)              | 00e7b0b8  2f 00  push arg2

        move.l  (0x04,%a1),%d0          | 00e7b0ba  20 29 00 04  D0 = arg1
        cmp.l   %d0,%d1                 | 00e7b0be  b2 80
        bls.b   SVC_$TRAP1_BAD_PTR_STUB | 00e7b0c0  63 cc -> 0xE7B08E
        move.l  %d0,-(%sp)              | 00e7b0c2  2f 00  push arg1

        jsr     (%a0)                   | 00e7b0c4  4e 90  call handler(arg1,arg2)

        addq.w  #8,%sp                  | 00e7b0c6  50 4f  pop 2 arguments
        jmp     FIM_$EXIT               | 00e7b0c8  4e f9 00 e2 28 bc

|----------------------------------------------------------------------
| Shared branch stubs (0x00E7B0CE, 0x00E7B0D2)
|
| Used by SVC_$TRAP1 (and, through TRAP1's bcc, by SVC_$TRAP0) and by
| SVC_$TRAP3 as well as by TRAP2 itself.
|----------------------------------------------------------------------

SVC_$TRAP2_INVALID_STUB:
        bra.w   SVC_$TRAP8_INVALID      | 00e7b0ce  60 00 01 be -> 0xE7B28E

SVC_$TRAP2_ILLEGAL_USP_STUB:
        bra.w   SVC_$ILLEGAL_USP_JMP    | 00e7b0d2  60 00 01 f2 -> 0xE7B2C6

        .short  0                       | 00e7b0d6  00 00: image pad, realigns
                                        | SVC_$TRAP3 to a longword boundary

|----------------------------------------------------------------------
| External references
|----------------------------------------------------------------------

        .extern FIM_$EXIT               | 0xE228BC: return from exception (RTE)
        .extern SVC_$TRAP2_TABLE        | 0xE7B466: handler table (svc_tables.c)
        .extern SVC_$TRAP1_BAD_PTR_STUB | 0xE7B08E: -> SVC_$BAD_USER_PTR
        .extern SVC_$TRAP8_INVALID      | 0xE7B28E: invalid syscall number
        .extern SVC_$ILLEGAL_USP_JMP    | 0xE7B2C6: jmp FIM_$ILLEGAL_USP (no unlk)

        .end
