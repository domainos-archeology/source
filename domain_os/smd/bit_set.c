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
 * Resolved (bead source-ssma): the original relies on `bset` being an
 * indivisible read-modify-write bus cycle, which is the only reason the
 * routine exists, so the model must be an atomic test-and-set too.  It now
 * uses __atomic_fetch_or with sequential consistency, which both gcc and
 * clang lower to a single atomic RMW (lock bts / lock or, ldaxrb/stlxrb) on
 * every host this tree is built on.  The value returned is the *previous*
 * byte, so the "was bit 7 already set" test is still made on the old value,
 * exactly as `bset` + `seq` does.
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
 */
int8_t SMD_$BIT_SET(uint8_t *byte_ptr)
{
    uint8_t old_value;

    /*
     * 0x00E15D16 "bset.b #0x7,(A0)": one indivisible read-modify-write that
     * sets bit 7 and leaves Z reflecting the bit's previous state.
     */
    old_value = (uint8_t)__atomic_fetch_or(byte_ptr, (uint8_t)0x80u,
                                           __ATOMIC_SEQ_CST);

    /* 0x00E15D1A "seq D0b": Z is set when the old bit was 0 */
    return (old_value & 0x80u) == 0 ? (int8_t)0xFF : (int8_t)0;
}

#endif /* !ARCH_M68K */
