/*
 * chksum/sau2/get_chksum.s - CHKSUM_$GET_CHKSUM (m68k / SAU2)
 *
 * Transcribed instruction for instruction from the image at 0x00E0A314
 * (20 bytes).  This is hand-written assembly, not compiled Pascal: there
 * is no `link`/`unlk`, no register save, and the argument is read
 * straight off the stack at (0x4,SP).
 *
 *   00e0a314  20 6f 00 04    movea.l (0x4,SP),A0
 *   00e0a318  70 00          moveq   #0x0,D0
 *   00e0a31a  32 3c 00 ff    move.w  #0xff,D1w
 *   00e0a31e  d0 58          add.w   (A0)+,D0w
 *   00e0a320  d0 58          add.w   (A0)+,D0w
 *   00e0a322  51 c9 ff fa    dbf     D1w,0x00e0a31e
 *   00e0a326  4e 75          rts
 *
 * `moveq #0xff,D1` + `dbf` is 256 iterations of two words each, i.e. 512
 * big-endian 16-bit words = 1024 bytes.  Carries out of bit 15 are
 * discarded (`add.w`, and D0's high half is never read back), so the
 * result is the sum of the page's words modulo 2^16, returned in D0 the
 * way a Pascal function result is.
 *
 * The only caller is disk_$chksum_page (0x00E0A2CC).
 */

        .section ".text.CHKSUM_$GET_CHKSUM","ax",@progbits
        .even

/* 256 dbf iterations, two words per iteration. */
        .set    CHKSUM_LOOP_COUNT, 0xFF

/*
 * uint16_t CHKSUM_$GET_CHKSUM(const void *va)
 *
 * In:  (0x4,SP) = address of a 1024-byte page
 * Out: D0.w     = sum of its 512 words, modulo 2^16
 * Clobbers: D0, D1, A0
 */
        .global CHKSUM_$GET_CHKSUM
CHKSUM_$GET_CHKSUM:
        movea.l (0x4,%sp), %a0          /* 00e0a314  page address */
        moveq   #0, %d0                 /* 00e0a318  running sum */
        move.w  #CHKSUM_LOOP_COUNT, %d1 /* 00e0a31a  256 iterations */
.Lloop:
        add.w   (%a0)+, %d0             /* 00e0a31e */
        add.w   (%a0)+, %d0             /* 00e0a320 */
        dbf     %d1, .Lloop             /* 00e0a322 */
        rts                             /* 00e0a326 */

        .end
