/*
 * smd/bit_set.c - portable model of SMD_$BIT_SET
 *
 * Original address: 0x00E15D12.
 *
 * The real routine is a three-instruction hand-written thunk and lives in
 * smd/sau2/bit_set.s; this file supplies a C stand-in for hosts that are not
 * the SAU2, and is compiled out entirely on m68k so the two do not collide
 * at link time (bead source-c3ap).
 *
 * The model is NOT equivalent under concurrency: the original relies on
 * `bset` being an indivisible read-modify-write bus cycle, which is the only
 * reason the routine exists.  A real port needs the host's atomic
 * test-and-set here.
 */

#include "smd/smd_internal.h"

#if !defined(ARCH_M68K)

/*
 * SMD_$BIT_SET - Test and set bit 7 of a byte
 *
 * Parameters:
 *   byte_ptr - the byte to test and set
 *
 * Returns:
 *   0xFF (negative) if bit 7 was clear before the call, 0 if it was set.
 *   The original writes only the low byte of D0 ("seq D0b"), hence int8_t.
 *
 * TODO(source-ssma): use a host atomic test-and-set instead of the plain
 * read-modify-write below.
 */
int8_t SMD_$BIT_SET(uint8_t *byte_ptr)
{
    uint8_t old_value;

    old_value = *byte_ptr;      /* 0x00E15D16 bset.b #0x7,(A0) reads ... */
    *byte_ptr = (uint8_t)(old_value | 0x80u); /* ... then writes bit 7 back */

    /* 0x00E15D1A seq D0b: Z is set when the old bit was 0 */
    return (old_value & 0x80u) == 0 ? (int8_t)0xFF : (int8_t)0;
}

#endif /* !ARCH_M68K */
