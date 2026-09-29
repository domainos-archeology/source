/*
 * MMU_$CACHE_INHIBIT_VA - Mark a virtual address cache-inhibited (no-op)
 *
 * Original address: 0x00E2429E, 2 bytes: a bare `rts'.  Callers (0x00E1A8EC,
 * 0x00E3ACB2, 0x00E3BD58) push one longword that is never read.  Nothing
 * between MMU_$MCR_CHANGE's end and CACHE_$CLEAR (0xE242D4) touches it.
 *
 * Image bytes (`gsk read 0xe2429e 2`):
 *   00e2429e  4e 75
 *
 * Hand-written assembly (no frame at all), so it is transcribed rather
 * than translated.
 */

        .section ".text.MMU_$CACHE_INHIBIT_VA","ax",@progbits
        .even

        .globl  MMU_$CACHE_INHIBIT_VA
        .globl  _MMU_$CACHE_INHIBIT_VA

MMU_$CACHE_INHIBIT_VA:
_MMU_$CACHE_INHIBIT_VA:
        rts                             /* 0xE2429E  4e 75 */
