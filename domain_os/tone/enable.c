/*
 * TONE_$ENABLE - Enable or disable the tone output
 *
 * This function controls the speaker output via the SIO2681 DUART's
 * output port. It delegates to SIO2681_$TONE which manipulates the
 * output port bit to turn the speaker on or off.
 *
 * Original address: 0x00e1ace8
 *
 * Assembly analysis:
 *   - Saves A5, sets A5 to 0xE2C9F0 (data segment base)
 *   - Computes A5+0x1268 = 0xE2DC58 (pointer to tone channel)
 *   - Stores this pointer in local variable
 *   - Calls SIO2681_$TONE(&channel_ptr, enable, -, -)
 *   - Restores A5 and returns
 *
 * Data layout:
 *   0xE2C9F0: Data segment base (A5)
 *   0xE2DC58: TONE_$CHANNEL pointer (A5 + 0x1268)
 */

#include "tone/tone_internal.h"

/*
 * Pointer to the SIO2681 channel used for tone generation.
 * This is stored at address 0xE2DC58 (= 0xE2C9F0 + 0x1268).
 * It is initialized during system startup by the keyboard/console driver.
 *
 * Original address: 0xE2DC58
 */
sio2681_channel_t *TONE_$CHANNEL = NULL;

/*
 * TONE_$ENABLE - Enable or disable tone
 *
 * Parameters:
 *   enable - Pointer to enable flag (bit 7 = enable tone)
 *            0xFF enables tone, 0x00 disables it
 *
 * The original code passes a pointer-to-pointer to SIO2681_$TONE
 * due to how the Pascal-derived calling convention works with
 * pass-by-reference semantics.
 */
void TONE_$ENABLE(uint8_t *enable)
{
    sio2681_channel_t **channel_ptr;
    uint32_t unused1;
    uint32_t unused2;

    /*
     * Get pointer to the tone channel pointer.
     * The original assembly computes this as A5+0x1268 and stores
     * the address in a local variable, then passes a pointer to
     * that local variable to SIO2681_$TONE.
     *
     * Since TONE_$CHANNEL is our global at 0xE2DC58, we just
     * take its address.
     */
    channel_ptr = &TONE_$CHANNEL;

    /*
     * Call SIO2681_$TONE to control the speaker output.
     * The third and fourth parameters are unused stack space
     * in the original code.
     */
    SIO2681_$TONE(*channel_ptr, enable, unused1, unused2);
}
