|
| SVC_$TRAP1 - Domain/OS TRAP #1 System Call Dispatcher
|
| TRAP #1 handles syscalls (0-65) that take 1 argument.
| User programs invoke system calls via TRAP #1 with:
|   - D0.w = syscall number (0-65)
|   - One argument on the user stack at (USP+0x04)
|
| From: 0x00E7B05C (SAU2 map: "E7B05C  SVC_$TRAP1").  This file covers
| 0x00E7B05C..0x00E7B093 - SVC_$TRAP1, its one branch stub, and the two-byte
| zero pad the image uses to realign the next dispatcher, 0x38 bytes total.
|
| Error paths.  TRAP1 owns only one stub of its own; the other two out-of-line
| paths are reached through stubs that live in SVC_$TRAP2 and SVC_$TRAP4:
|
|   range check  -> SVC_$TRAP2_INVALID_STUB     (0x00E7B0CE, trap2.s)
|   bad USP      -> SVC_$TRAP2_ILLEGAL_USP_STUB (0x00E7B0D2, trap2.s)
|   bad user ptr -> SVC_$TRAP1_BAD_PTR_STUB     (0x00E7B08E, here) which is a
|                   bra.w to SVC_$TRAP4_BAD_PTR_STUB (0x00E7B178, trap4.s)
|
| These are 8-bit PC-relative branches into other object files; they resolve
| only because every trapN.o is emitted byte-for-byte the size of its image
| region and the linker keeps them in map order (trap0..trap8, contiguous).
|
| SVC_$TRAP1_RANGE_CHECK (0x00E7B060) is global because SVC_$TRAP0 jumps
| straight at this `bcc.b' to reuse it for its own range check - see trap0.s.
|
| Address space check:
|   0xCC0000 = boundary between user and kernel space
|   USP and the argument pointer must be < 0xCC0000
|
| Tables:
|   SVC_$TRAP1_TABLE at 0xE7B35E - handler addresses (66 entries, svc_tables.c)
|

        .include "svc/sau2/svc_macros.inc"

        .section ".text.SVC_$TRAP1","ax",@progbits
        .even

|----------------------------------------------------------------------
| SVC_$TRAP1 - 1-argument syscall dispatcher
|
| Input:
|   D0.w = syscall number (0x00-0x41)
|   (USP+0x04) = arg1
|
| Processing:
|   1. Validate syscall number (< 0x42)
|   2. Look up handler in SVC_$TRAP1_TABLE
|   3. Validate USP < 0xCC0000
|   4. Validate and push arg1
|   5. Call handler, pop the argument, return via RTE
|----------------------------------------------------------------------

        .global SVC_$TRAP1
        .global SVC_$TRAP1_RANGE_CHECK
        .global SVC_$TRAP1_BAD_PTR_STUB

SVC_$TRAP1:
        | 00e7b05c  b0 7c 00 42: CMP.W #imm,D0 (see svc_macros.inc)
        cmp_w_imm 0x42, 0               | Check syscall number < 66

SVC_$TRAP1_RANGE_CHECK:
        bcc.b   SVC_$TRAP2_INVALID_STUB | 00e7b060  64 6c -> 0xE7B0CE

        lsl.w   #2,%d0                  | 00e7b062  e5 48  D0 = syscall# * 4
        lea     (SVC_$TRAP1_TABLE:w,%pc),%a0 | 00e7b064  41 fa 02 f8 -> 0xE7B35E
        movea.l (0,%a0,%d0:w),%a0       | 00e7b068  20 70 00 00  A0 = handler

        move    %usp,%a1                | 00e7b06c  4e 69
        cmpa.l  #0xCC0000,%a1           | 00e7b06e  b3 fc 00 cc 00 00
        bhi.b   SVC_$TRAP2_ILLEGAL_USP_STUB | 00e7b074  62 5c -> 0xE7B0D2

        move.l  (0x04,%a1),%d0          | 00e7b076  20 29 00 04  D0 = arg1
        | 00e7b07a  b0 bc 00 cc 00 00: CMP.L #imm,D0 (see svc_macros.inc)
        cmp_l_imm 0xCC0000, 0           | Check arg1 < 0xCC0000
        bhi.b   SVC_$TRAP1_BAD_PTR_STUB | 00e7b080  62 0c -> 0xE7B08E
        move.l  %d0,-(%sp)              | 00e7b082  2f 00  push arg1

        jsr     (%a0)                   | 00e7b084  4e 90  call handler(arg1)

        addq.w  #4,%sp                  | 00e7b086  58 4f  pop 1 argument
        jmp     FIM_$EXIT               | 00e7b088  4e f9 00 e2 28 bc

|----------------------------------------------------------------------
| SVC_$TRAP1_BAD_PTR_STUB - 0x00E7B08E
|
| Out of 8-bit reach of SVC_$TRAP4_BAD_PTR_STUB, so TRAP1 spends a word
| bouncing through this bra.w.
|----------------------------------------------------------------------

SVC_$TRAP1_BAD_PTR_STUB:
        bra.w   SVC_$TRAP4_BAD_PTR_STUB | 00e7b08e  60 00 00 e8 -> 0xE7B178

        .short  0                       | 00e7b092  00 00: image pad, realigns
                                        | SVC_$TRAP2 to a longword boundary

|----------------------------------------------------------------------
| External references
|----------------------------------------------------------------------

        .extern FIM_$EXIT               | 0xE228BC: return from exception (RTE)
        .extern SVC_$TRAP1_TABLE        | 0xE7B35E: handler table (svc_tables.c)
        .extern SVC_$TRAP2_INVALID_STUB | 0xE7B0CE: -> SVC_$TRAP8_INVALID
        .extern SVC_$TRAP2_ILLEGAL_USP_STUB | 0xE7B0D2: -> SVC_$ILLEGAL_USP_JMP
        .extern SVC_$TRAP4_BAD_PTR_STUB | 0xE7B178: -> SVC_$BAD_USER_PTR

        .end
