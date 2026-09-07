/*
 * CHKSUM - the disk block checksum
 *
 * A single tiny routine that sums the 1024-byte block at a virtual address
 * as 512 unsigned 16-bit words, discarding carries.
 */

#ifndef CHKSUM_H
#define CHKSUM_H

#include "base/base.h"

/*
 * Bytes CHKSUM_$GET_CHKSUM reads, and the number of 16-bit words that is.
 *
 * Both come from `move.w #0xff,D1w` + `dbf` (256 iterations) with two
 * `add.w (A0)+,D0w` per iteration: 512 words, 1024 bytes.  The count is
 * baked into the routine; there is no length argument.
 */
#define CHKSUM_PAGE_WORDS       512
#define CHKSUM_PAGE_SIZE        (CHKSUM_PAGE_WORDS * 2)

/*
 * CHKSUM_$GET_CHKSUM - 16-bit sum of the 1024 bytes at `va`
 *
 * Original address: 0x00E0A314, 20 bytes.  It is hand-written assembly:
 * there is no `link`, it reads its argument straight off the stack at
 * (0x4,SP) and returns the sum in D0.
 *
 *   00e0a314  movea.l (0x4,SP),A0
 *   00e0a318  moveq #0x0,D0
 *   00e0a31a  move.w #0xff,D1w
 *   00e0a31e  add.w (A0)+,D0w      \
 *   00e0a320  add.w (A0)+,D0w       > 256 iterations, two words each
 *   00e0a322  dbf D1w,0x00e0a31e   /
 *   00e0a326  rts
 *
 * The words are the machine's 16-bit words, i.e. BIG-endian pairs, and the
 * sum is taken modulo 2^16 (`add.w`; D0's high half is never read).
 *
 * m68k:  chksum/sau2/get_chksum.s (byte-identical to the image).
 * Other: chksum/get_chksum.c, a portable model that reads the same
 *        big-endian word sequence.
 */
uint16_t CHKSUM_$GET_CHKSUM(const void *va);

#endif /* CHKSUM_H */
