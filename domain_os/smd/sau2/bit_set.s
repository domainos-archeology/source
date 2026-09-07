/*
 * smd/sau2/bit_set.s - SMD_$BIT_SET (hand-written assembly)
 *
 * Original address: 0x00E15D12, 12 bytes.
 *
 * A three-instruction test-and-set thunk: no link/unlk, the argument is read
 * straight off the stack at (0x4,SP), and the result is a Domain boolean in
 * the low byte of D0.  `bset` is an indivisible read-modify-write on the
 * 68000 bus, which is the whole point of the routine, so the C model in
 * smd/bit_set.c (built only for non-m68k hosts) is not equivalent under
 * concurrency.
 *
 * Entry:
 *   (0x4,SP)  uint8_t *byte_ptr
 *
 * Exit:
 *   D0.b  0xFF when bit 7 was clear before the call, 0x00 when it was set.
 *
 * Transcribed instruction for instruction:
 *   00e15d12    movea.l (0x4,SP),A0
 *   00e15d16    bset.b #0x7,(A0)
 *   00e15d1a    seq D0b
 *   00e15d1c    rts
 */

        .text
        .globl  SMD_$BIT_SET
        .globl  _SMD_$BIT_SET

SMD_$BIT_SET:
_SMD_$BIT_SET:
        movea.l 0x4(%sp),%a0            /* 00e15d12                     */
        bset.b  #0x7,(%a0)              /* 00e15d16: Z = old bit 7 == 0 */
        seq     %d0                     /* 00e15d1a: D0.b = 0xFF if Z   */
        rts                             /* 00e15d1c                     */
