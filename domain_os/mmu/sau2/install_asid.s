/*
 * MMU_$INSTALL_ASID - Switch the MMU to an address space
 *
 * Byte gate (source-6psc; tools/asm_compare.py, `make check'): encodings
 * identical to the image (modulo the documented widenings); address
 * operands resolve to our objects.
 *
 * Original address: 0x00E24204, 38 bytes (0xE24204..0xE24229).
 *
 * Image bytes (`gsk read 0xe24204 38`):
 *   00e24204  32 2f 00 04 33 c1 00 e2  06 0a 13 c1 00 e2 3d 2c
 *   00e24214  33 fa fb 16 00 ff b4 00  13 f9 00 e2 18 d5 00 ff
 *   00e24224  b4 02 60 00 00 ac
 *
 * Argument: (4,SP) asid, word.
 *   PROC1_$AS_ID (0xE2060A) := asid; the HIGH byte of MMU_$PID_PRIV
 *   (0xE23D2C) := its low byte; CSR := MMU_$PID_PRIV; the FPU owner
 *   register (0xFFB402) := the low byte of FP_$OWNER (0xE218D5, a single
 *   byte store); tail-call CACHE_$CLEAR.
 *
 * Encodings identical to the image; address operands resolve to our
 * objects (tools/asm_compare.py), except these widenings, which the
 * compare recognises and checks:
 *   0xE24214  move.w (-0x4ea,PC),CSR  -> move.w MMU_$PID_PRIV,CSR   (+2)
 *   0xE24226  bra.w CACHE_$CLEAR      -> jmp CACHE_$CLEAR           (+2)
 */

        .section ".text.MMU_$INSTALL_ASID","ax",@progbits
        .even

        /* MMU_ASM data cell: a field of the MMU_$GLOBALS block (mmu/mmu.h,
         * source-o56c). */
        .extern MMU_$GLOBALS
        .set    MMU_$PID_PRIV,  MMU_$GLOBALS + 0x0  /* map 0xE23D2C, a field of the MMU_$GLOBALS block */
        /* PROC1_$AS_ID (uint16_t, proc1/proc1_data.c, map 0xE2060A) and
         * FP_$OWNER (the 2-byte owner cell in fp/sau2/savep.s, map 0xE218D4;
         * the byte store reads its low byte) are used by name (source-6psc). */
        .extern PROC1_$AS_ID
        .extern FP_$OWNER
        .set    FP_$OWNER_LO,   FP_$OWNER + 1   /* 0x00E218D5 */
        /* SAU2 hardware registers, legitimately absolute. */
        .equ    MMU_CSR,        0x00FFB400  /* SAU2 MMU CSR (hardware, SAU2_MMU_CSR in arch/m68k/sau2/hw.h) */
        .equ    FPU_OWNER_REG,  0x00FFB402  /* SAU2 FPU owner register (hardware, SAU2_MMU_FPU_OWNER_REG in arch/m68k/sau2/hw.h) */

        .globl  MMU_$INSTALL_ASID
        .globl  _MMU_$INSTALL_ASID
        .extern CACHE_$CLEAR

MMU_$INSTALL_ASID:
_MMU_$INSTALL_ASID:
        move.w  (4,%sp),%d1             /* 0xE24204  32 2f 00 04       */
        move.w  %d1,(PROC1_$AS_ID).l    /* 0xE24208  33 c1 00 e2 06 0a */
        move.b  %d1,MMU_$PID_PRIV       /* 0xE2420E  13 c1 00 e2 3d 2c */
        move.w  MMU_$PID_PRIV,MMU_CSR   /* 0xE24214  33 fa fb 16 00 ff b4 00 in the image */
        move.b  (FP_$OWNER_LO).l,FPU_OWNER_REG /* 0xE2421C  13 f9 00 e2 18 d5 00 ff b4 02 */
        jmp     CACHE_$CLEAR            /* 0xE24226  60 00 00 ac (bra.w) in the image */

        .size   MMU_$INSTALL_ASID, .-MMU_$INSTALL_ASID
