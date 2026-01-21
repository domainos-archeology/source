/*
 * smd/bit_set.c - SMD_$BIT_SET implementation
 *
 * Atomic test-and-set of bit 7 in a byte.
 * Returns whether the bit was previously clear.
 *
 * Original address: 0x00E15D12
 *
 * Assembly:
 *   00e15d12    movea.l (0x4,SP),A0      ; Get byte pointer
 *   00e15d16    bset.b #0x7,(A0)         ; Test and set bit 7
 *   00e15d1a    seq D0b                  ; Set if equal (bit was 0)
 *   00e15d1c    rts
 *
 * The bset instruction atomically tests bit 7, sets the Z flag based on
 * the original value, then sets bit 7. seq sets D0 to 0xFF if Z was set
 * (bit was 0), or 0x00 if Z was clear (bit was 1).
 */

#include "smd/smd_internal.h"

/*
 * SMD_$BIT_SET - Atomic test and set bit 7
 *
 * Tests bit 7 of the byte at the given address and sets it.
 * This is an atomic read-modify-write operation on m68k.
 *
 * Parameters:
 *   byte_ptr - Pointer to byte to test and set
 *
 * Returns:
 *   0xFF (-1) if bit was previously clear (now set)
 *   0x00 if bit was previously set
 *
 * Note: On m68k this is atomic due to the bset instruction.
 * On other architectures, this may need to use platform-specific
 * atomic operations or locking.
 */
int8_t SMD_$BIT_SET(uint8_t *byte_ptr)
{
    uint8_t old_value;

    old_value = *byte_ptr;
    *byte_ptr = old_value | 0x80;

    /* Return 0xFF if bit was clear, 0x00 if bit was set */
    return -((old_value & 0x80) == 0);
}
