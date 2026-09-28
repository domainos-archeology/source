/*
 * UID_$HASH - Hash a UID for table indexing (0x00E17360, 28 bytes)
 *
 * SAU2 map: its own 0x1C-byte segment `I E17360 UID_$HASH`.  The routine
 * has no frame and takes its arguments straight off the stack
 * (`movea.l (0x4,SP),A0` / `movea.l (0x8,SP),A1`), i.e. it was written in
 * assembly, but it follows the C calling convention (two pointer arguments,
 * result in D0), so it is kept as C for retargetability.
 *
 *   00e17368  movem.l (A0),{D0 D1}      ; D0 = uid.high, D1 = uid.low
 *   00e1736c  eor.l   D1,D0             ; D0 = high ^ low
 *   00e1736e  move.w  D0w,D1w           ; D1w = low 16 bits of that
 *   00e17370  clr.w   D0w
 *   00e17372  swap    D0                ; D0 = high 16 bits, zero-extended
 *   00e17374  eor.w   D1w,D0w           ; D0 = (hi16 ^ lo16), a 16-bit value
 *   00e17376  divu.w  (A1),D0           ; D0 = remainder:quotient
 *   00e17378  swap    D0                ; D0 = quotient:remainder
 *
 * So the LOW word of the result is the remainder - the bucket index every
 * caller keeps (`move.w D0w,D1w` in audit_$add_to_hash 0x00E712F8,
 * `& 0xFFFF` in file/priv_lock.c) - and the HIGH word is the quotient.
 *
 * Parameters:
 *   uid        - the UID to hash
 *   table_size - pointer to the word divisor (a Pascal VAR argument; the
 *                callers pass constant cells such as audit's 0x0025)
 */

#include "uid/uid_internal.h"

uint32_t UID_$HASH(uid_t *uid, uint16_t *table_size)
{
    uint32_t folded;
    uint16_t hash16;
    uint16_t divisor;
    uint16_t quotient;
    uint16_t remainder;

    folded  = uid->high ^ uid->low;                             /* 0x00E1736C */
    hash16  = (uint16_t)((folded >> 16) ^ (folded & 0xFFFFu));  /* 0x00E1736E-0x00E17374 */

    divisor   = *table_size;                                    /* 0x00E17376 divu.w (A1) */
    quotient  = (uint16_t)(hash16 / divisor);
    remainder = (uint16_t)(hash16 % divisor);

    /* 0x00E17378 swap: quotient in the high word, remainder in the low */
    return ((uint32_t)quotient << 16) | remainder;
}
