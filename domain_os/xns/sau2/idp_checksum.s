/*
 * XNS_IDP_$CHECKSUM / XNS_IDP_$HOP_AND_SUM - the XNS IDP checksum engine
 *
 * SAU2 map domain_os.10.2.map:
 *
 *      D    E2B850  XNS_IDP_ASM        size = 4C
 *           E2B850  XNS_IDP_$CHECKSUM
 *           E2B872  XNS_IDP_$HOP_AND_SUM
 *
 * Hand-written assembly, not compiled Pascal: no link/unlk frame, the
 * arguments are read straight off the stack at (4,SP)/(8,SP) and (4,SP)/(6,SP),
 * and the immediate compares use the CMP.W #imm,Dn / AND.W #imm,Dn encodings
 * (0xB07C / 0xC27C) that gas will not emit, so they are spelled with .short.
 *
 * XNS_IDP_$CHECKSUM(data, word_count) -> D0.w
 *   One's-complement sum with end-around carry, rotated left one bit after
 *   every word; word_count words (`subq.w #1` + `dbf`).  0xFFFF becomes 0.
 *
 * XNS_IDP_$HOP_AND_SUM(current_sum, hop_offset) -> D0.w
 *   Adds 0x100 rotated left by ((hop_offset - 3) >> 1) & 0xF to current_sum
 *   with end-around carry; 0xFFFF becomes 0.
 *
 * The segment is 0x4C bytes: 0x22 + 0x28 of code plus a zero pad word at
 * 0x00E2B89A.
 *
 * Verified byte-identical to the image (objcopy -O binary, 76 bytes).
 *
 * Original address: 0x00E2B850
 */

        .section ".text.XNS_IDP_$CHECKSUM","ax",@progbits
        .even

        .globl  XNS_IDP_$CHECKSUM
        .globl  _XNS_IDP_$CHECKSUM
        .globl  XNS_IDP_$HOP_AND_SUM
        .globl  _XNS_IDP_$HOP_AND_SUM

XNS_IDP_$CHECKSUM:
_XNS_IDP_$CHECKSUM:
        moveq   #0, %d0                 /* 00E2B850  sum = 0 */
        movea.l 4(%sp), %a0             /* 00E2B852  data */
        move.w  8(%sp), %d1             /* 00E2B856  word_count */
        subq.w  #1, %d1                 /* 00E2B85A  dbf count */
1:
        add.w   (%a0)+, %d0             /* 00E2B85C  sum += *data++ */
        bcc.s   2f                      /* 00E2B85E */
        addq.w  #1, %d0                 /* 00E2B860  end-around carry */
2:
        rol.w   #1, %d0                 /* 00E2B862 */
        dbf     %d1, 1b                 /* 00E2B864 */
        .short  0xb07c, 0xffff          /* 00E2B868  cmp.w #-1,%d0 */
        bne.s   3f                      /* 00E2B86C */
        moveq   #0, %d0                 /* 00E2B86E  0xFFFF -> 0 */
3:
        rts                             /* 00E2B870 */

        .section ".text.XNS_IDP_$HOP_AND_SUM","ax",@progbits
        .balign 2
XNS_IDP_$HOP_AND_SUM:
_XNS_IDP_$HOP_AND_SUM:
        move.w  6(%sp), %d1             /* 00E2B872  hop_offset */
        subq.w  #3, %d1                 /* 00E2B876 */
        asr.w   #1, %d1                 /* 00E2B878 */
        .short  0xc27c, 0x000f          /* 00E2B87A  and.w #0xf,%d1 */
        move.w  #0x100, %d0             /* 00E2B87E */
        tst.w   %d1                     /* 00E2B882 */
        beq.s   4f                      /* 00E2B884 */
        rol.w   %d1, %d0                /* 00E2B886 */
4:
        add.w   4(%sp), %d0             /* 00E2B888  + current_sum */
        bcc.s   5f                      /* 00E2B88C */
        addq.w  #1, %d0                 /* 00E2B88E  end-around carry */
5:
        .short  0xb07c, 0xffff          /* 00E2B890  cmp.w #-1,%d0 */
        bne.s   6f                      /* 00E2B894 */
        moveq   #0, %d0                 /* 00E2B896  0xFFFF -> 0 */
6:
        rts                             /* 00E2B898 */

        .short  0                       /* 00E2B89A  segment pad to 0x4C */
