/*
 * SIO2681_$TONE - Control tone output
 *
 * Controls the tone output through the 2681's output port. The tone
 * is used for speaker/bell functionality on Apollo workstations.
 *
 * Original address: 0x00e1d172
 */

#include "sio2681/sio2681_internal.h"

/*
 * SIO2681_$TONE - Enable or disable tone
 *
 * The 2681's output port bits can be set/reset to control external
 * hardware like a speaker. Bit 7 of the output port controls tone.
 *
 * Assembly analysis:
 *   - Gets chip structure from channel->regs (actually channel->chip via indirection)
 *   - Acquires spin lock
 *   - Modifies OPCR byte 6 based on enable flag (bit 7)
 *   - Writes to SOPBC (set output port bits) and ROPBC (reset output port bits)
 *   - Releases spin lock
 *
 * The assembly shows it uses the chip structure pointer stored in the
 * memory location referenced through channel->regs, which is the chip
 * structure's OPCR shadow register at offset 0x06.
 *
 * Parameters:
 *   channel    - Channel structure (used to find chip structure)
 *   enable_ptr - Pointer to enable flag (bit 7 = enable tone)
 *   param3     - Reserved (unused)
 *   param4     - Reserved (unused)
 */
void SIO2681_$TONE(sio2681_channel_t *channel, uint8_t *enable_ptr,
                   uint32_t param3, uint32_t param4)
{
    volatile uint8_t *base_regs;
    sio2681_chip_t *chip;
    uint8_t enable;
    uint8_t opcr_val;
    ml_$spin_token_t token;

    (void)param3;
    (void)param4;

    /*
     * Get chip structure - the assembly dereferences through the regs pointer
     * to get to the chip structure at offset +4.
     *
     * Assembly: movea.l (0x4,A0),A0  ; A0 = chip->regs, then chip->chip
     */
    base_regs = channel->regs;
    chip = channel->chip;

    token = ML_$SPIN_LOCK(&SIO2681_$DATA.spin_lock);

    enable = *enable_ptr;

    /*
     * Update OPCR shadow register bit 7 with inverted enable state.
     * Assembly shows: not.b D1b; andi.b #0x7f; andi.b #-0x80,D1b; or.b
     */
    opcr_val = (*(uint8_t *)&chip->config2) & 0x7F;  /* Clear bit 7 */
    opcr_val |= (~enable) & 0x80;                     /* Set bit 7 if enable is 0 */
    *(uint8_t *)&chip->config2 = opcr_val;

    /*
     * Write to SOPBC (Set Output Port Bits Command) and ROPBC (Reset Output Port Bits Command)
     * to update the actual output port state.
     */
    chip->regs[SIO2681_REG_SOPBC] = opcr_val;         /* Set bits that are 1 */
    chip->regs[SIO2681_REG_ROPBC] = opcr_val ^ 0xFF;  /* Reset bits that are 0 */

    ML_$SPIN_UNLOCK(&SIO2681_$DATA.spin_lock, token);
}
