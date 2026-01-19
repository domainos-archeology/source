/*
 * SIO2681_$INIT - Initialize a SIO2681 DUART chip
 *
 * This function initializes both channels of a Signetics 2681 DUART chip.
 * It sets up the hardware registers, links channel structures to SIO
 * descriptors, and configures initial line parameters.
 *
 * Original address: 0x00e333dc
 */

#include "sio2681/sio2681_internal.h"

/*
 * SIO2681_$INIT - Initialize DUART chip and both channels
 *
 * Assembly analysis:
 *   - Calculates base address: 0xFFB000 - (chip_num << 5) = 0xFFAFE0 for chip 0
 *   - Channel A registers at base + 0x00
 *   - Channel B registers at base + 0x10
 *   - Sets up channel structures with hardware pointers
 *   - Initializes IMR to 0 (interrupts disabled)
 *   - Calls SET_LINE to configure both channels
 *   - Sets up interrupt vector
 *   - Enables receiver/transmitter with command 0x05
 *
 * Parameters (from stack frame):
 *   A6+0x08: int_vec_ptr    - Interrupt vector number pointer
 *   A6+0x0C: chip_num_ptr   - Chip number pointer
 *   A6+0x10: chan_a_struct  - Channel A structure
 *   A6+0x14: chan_a_callback - Channel A SIO descriptor pointer
 *   A6+0x18: chan_a_params  - Channel A parameters
 *   A6+0x1C: chan_b_struct  - Channel B structure
 *   A6+0x20: chan_b_callback - Channel B SIO descriptor pointer
 *   A6+0x24: chan_b_params  - Channel B parameters
 *   A6+0x28: chip_struct    - Chip structure
 *   A6+0x2C: config         - Configuration data
 */
void SIO2681_$INIT(int16_t *int_vec_ptr, int16_t *chip_num_ptr,
                   sio2681_channel_t *chan_a_struct, sio_desc_t **chan_a_callback,
                   sio_params_t *chan_a_params,
                   sio2681_channel_t *chan_b_struct, sio_desc_t **chan_b_callback,
                   sio_params_t *chan_b_params,
                   sio2681_chip_t *chip_struct, uint16_t *config)
{
    volatile uint8_t *base_addr;
    int16_t chip_num;
    int16_t table_offset;
    status_$t status;

    chip_num = *chip_num_ptr;

    /*
     * Calculate base address for this chip's registers.
     * Each chip uses 32 bytes of address space.
     * Base is 0xFFB000, chips are at decreasing addresses.
     *
     * Assembly: lsl.w #5,D0w ; lea (-0x20,A1,D0w*0x1),A1
     * where A1 starts at 0xFFB000
     */
    base_addr = (volatile uint8_t *)(SIO2681_BASE_ADDR - ((uint16_t)chip_num << 5));

    /* Initialize chip structure */
    chip_struct->regs = base_addr;
    chip_struct->config1 = config[0];
    chip_struct->config2 = config[2];
    chip_struct->imr_shadow = 0xa2;  /* Initial IMR value: not our interrupt mask */

    /* Disable all interrupts initially */
    base_addr[SIO2681_REG_IMR] = 0;

    /*
     * Calculate table offset for channel pointers.
     * Table indexed by chip_num << 4 (16 bytes per chip entry).
     */
    table_offset = (int16_t)chip_num << 4;

    /* Register chip and channel A in global tables */
    SIO2681_$CHIPS[chip_num] = chip_struct;
    SIO2681_$CHANNELS[(chip_num << 1)] = chan_a_struct;

    /*
     * Initialize Channel A structure
     */
    chan_a_struct->regs = base_addr;  /* Channel A at base */
    chan_a_struct->sio_desc = *chan_a_callback;
    chan_a_struct->flags = 0x0002;    /* Flags: not channel B indicator used elsewhere */
    chan_a_struct->int_bit = 0;       /* TxRDY is bit 0 for channel A */
    chan_a_struct->peer = chan_b_struct;
    chan_a_struct->chip = chip_struct;
    chan_a_struct->tx_int_mask = 0;
    chan_a_struct->reserved_14 = 0;
    chan_a_struct->baud_support = 0;

    /* Issue reset/enable command to channel A */
    base_addr[SIO2681_REG_CRA] = SIO2681_CR_RX_ENABLE | SIO2681_CR_TX_ENABLE;  /* 0x05 */

    /* Register channel B in global table */
    SIO2681_$CHANNELS[(chip_num << 1) + 1] = chan_b_struct;

    /*
     * Initialize Channel B structure
     * Channel B registers are at base + 0x10
     */
    chan_b_struct->regs = base_addr + 0x10;
    chan_b_struct->sio_desc = *chan_b_callback;
    chan_b_struct->flags = 0x0000;    /* No special flags for B */
    chan_b_struct->int_bit = 4;       /* TxRDY is bit 4 for channel B */
    chan_b_struct->peer = chan_a_struct;
    chan_b_struct->chip = chip_struct;
    chan_b_struct->tx_int_mask = 0;
    chan_b_struct->reserved_14 = 0;
    chan_b_struct->baud_support = 0;

    /* Issue reset/enable command to channel B */
    (base_addr + 0x10)[SIO2681_REG_CRA] = SIO2681_CR_RX_ENABLE | SIO2681_CR_TX_ENABLE;

    /*
     * Configure line parameters for both channels.
     * Mask 0x3FFF means set all parameters.
     */
    SIO2681_$SET_LINE(chan_a_struct, chan_a_params, 0x3FFF, &status);
    SIO2681_$SET_LINE(chan_b_struct, chan_b_params, 0x3FFF, &status);

    /*
     * Set up interrupt vector.
     * The vector table is indexed by int_vec_ptr (from chip_num).
     * Interrupt vectors are at 0x60 + (vec_num * 4).
     *
     * Assembly shows:
     *   move.w (A0),D1w  ; int_vec from param
     *   lsl.w #2,D1w     ; *4 for vector table offset
     *   move.l (-0x4,A5,D0w*0x1),(-0x4,A1,D1w*0x1)
     *
     * This copies from SIO2681_$INT_VECTORS[chip_num] to interrupt vector table.
     */
    {
        int16_t vec_num = *int_vec_ptr;
        m68k_ptr_t *int_vec_table = (m68k_ptr_t *)0x60;
        int_vec_table[vec_num] = (m68k_ptr_t)SIO2681_$INT_VECTORS[chip_num];
    }

    /* Enable interrupts (write final IMR value) */
    base_addr[SIO2681_REG_IMR] = chip_struct->imr_shadow;
}
