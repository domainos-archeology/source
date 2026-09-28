/*
 * MMU_$INSTALL_ASID - Switch the MMU to an address space
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
 * Deviations from the image bytes, forced by separate assembly:
 *   0xE24214  move.w (-0x4ea,PC),CSR  -> move.w MMU_$PID_PRIV,CSR   (+2)
 *   0xE24226  bra.w CACHE_$CLEAR      -> jmp CACHE_$CLEAR           (+2)
 */

        .text
        .even

        .equ    MMU_$PID_PRIV,  0x00E23D2C
        .equ    PROC1_$AS_ID,   0x00E2060A
        .equ    FP_$OWNER_LO,   0x00E218D5
        .equ    MMU_CSR,        0x00FFB400
        .equ    FPU_OWNER_REG,  0x00FFB402

        .globl  MMU_$INSTALL_ASID
        .globl  _MMU_$INSTALL_ASID
        .extern CACHE_$CLEAR

MMU_$INSTALL_ASID:
_MMU_$INSTALL_ASID:
        move.w  (4,%sp),%d1             /* 0xE24204  32 2f 00 04       */
        move.w  %d1,PROC1_$AS_ID        /* 0xE24208  33 c1 00 e2 06 0a */
        move.b  %d1,MMU_$PID_PRIV       /* 0xE2420E  13 c1 00 e2 3d 2c */
        move.w  MMU_$PID_PRIV,MMU_CSR   /* 0xE24214  33 fa fb 16 00 ff b4 00 in the image */
        move.b  FP_$OWNER_LO,FPU_OWNER_REG /* 0xE2421C  13 f9 00 e2 18 d5 00 ff b4 02 */
        jmp     CACHE_$CLEAR            /* 0xE24226  60 00 00 ac (bra.w) in the image */

        .size   MMU_$INSTALL_ASID, .-MMU_$INSTALL_ASID
