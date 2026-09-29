/*
 * TIME_$WRT_VT_TIMER - Load the virtual timer counter
 *
 * Hand-written assembly in the TIME_ASM segment with a register calling
 * convention: the value arrives in D0w, A0 is preserved through A2 (which
 * is clobbered), and IN_VT_INT is cleared.  Its only caller is PROC1's
 * dispatcher at 0x00E20A64 (`bsr`/`jsr` from assembly), so it is kept as
 * assembly rather than given a C signature it does not have.
 *
 * Original address: 0x00e2af8a, 22 bytes:
 *   24 48                 movea.l A0,A2
 *   41 f9 00 ff ac 00     lea (0xffac00).l,A0
 *   01 88 00 09           movep.w D0w,(0x9,A0)      ; bytes 0xFFAC09 / 0xFFAC0B
 *   20 4a                 movea.l A2,A0
 *   51 f9 00 e2 af 6a     sf (0x00e2af6a).l         ; IN_VT_INT = 0
 *   4e 75                 rts
 */

        .section ".text.TIME_$WRT_VT_TIMER","ax",@progbits
        .balign 2

    .globl  TIME_$WRT_VT_TIMER
    .globl  _TIME_$WRT_VT_TIMER

TIME_$WRT_VT_TIMER:
_TIME_$WRT_VT_TIMER:
    movea.l %a0, %a2                /* save the caller's A0 */
    lea     (0xffac00).l, %a0       /* timer register block */
    movep.w %d0, 9(%a0)             /* VT counter, high byte at +9, low at +0xB */
    movea.l %a2, %a0                /* restore A0 */
    sf      (IN_VT_INT):l           /* IN_VT_INT = 0 (0x00E2AF6A) */
    rts
