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
 *   - Base address: 0xFFB000 - 0x20 + chip_num * 0x20 (lea (-0x20,A1,D0w) at 0xE333FC; chip numbers are 1-based, so chip 1 is 0xFFB000)
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
    sio2681_ptrs_entry_t *ptrs;
    status_$t status;

    chip_num = *chip_num_ptr;

    /*
     * Calculate base address for this chip's registers.
     * Each chip uses 32 bytes of address space, and chip numbers are
     * 1-BASED: TERM_$INIT passes the constant word 1 from 0x00E33220 as both
     * int_vec_ptr and chip_num_ptr (0x00E331C4 `pea (0x5a,PC)` /
     * 0x00E331C8 `move.l (SP),-(SP)`).
     *
     * Assembly (0x00E333F4-0x00E333FC):
     *   movea.l #0xffb000,A1
     *   lsl.w   #0x5,D0w
     *   lea     (-0x20,A1,D0w*0x1),A1
     * i.e. A1 = 0xFFB000 - 0x20 + chip_num * 0x20, so chip 1 sits at
     * 0xFFB000 and chip 2 at 0xFFB020.
     */
    base_addr = (volatile uint8_t *)ARCH_VA_TO_PTR(SIO2681_BASE_ADDR - 0x20 +
                                                   ((uint16_t)chip_num << 5));

    /* Initialize chip structure */
    chip_struct->regs = base_addr;
    chip_struct->config1 = config[0];
    chip_struct->config2 = config[2];
    chip_struct->imr_shadow = 0xa2;  /* Initial IMR value: not our interrupt mask */

    /* Disable all interrupts initially */
    base_addr[SIO2681_REG_IMR] = 0;

    /*
     * 0xE333F4..0xE33428: the one per-chip record.  D0 = chip_num * 0x10 is
     * an index off 0x00E2DF80 and every write is at a NEGATIVE displacement
     * from it, so entry n is at 0x00E2DF80 + 0x10*(n-1): the table is
     * 1-based, like the register base above.
     */
    ptrs = &SIO2681_$PTRS[chip_num - 1];

    /* 0xE33428 "move.l A3,(-0x8,A4,D0w*0x1)" */
    ptrs->chip = chip_struct;
    /* 0xE3342C "move.l (0x10,A6),(-0x10,A4,D0w*0x1)" */
    ptrs->chan_a = chan_a_struct;

    /*
     * Initialize Channel A structure, 0xE33432..0xE3345C, in the order the
     * original writes the fields.
     */
    chan_a_struct->regs = base_addr;            /* 0xE33436 move.l A1,(A4)   */
    chan_a_struct->sio_desc = *chan_a_callback; /* 0xE3343C -> (0xc,A4)      */
    chan_a_struct->tx_int_mask = 0x0002;        /* 0xE33440 move.w #2,(0x18) */
    chan_a_struct->int_bit = 0;                 /* 0xE33446 clr.w (0x12,A4)  */
    chan_a_struct->peer = chan_b_struct;        /* 0xE3344A -> (0x8,A4)      */
    chan_a_struct->chip = chip_struct;          /* 0xE33450 -> (0x4,A4)      */
    chan_a_struct->flags = 0;                   /* 0xE33454 clr.w (0x10,A4)  */
    chan_a_struct->reserved_14 = 0;             /* 0xE33458 clr.l (0x14,A4)  */
    chan_a_struct->baud_support = 0;            /* 0xE3345C clr.w (0x1a,A4)  */

    /* Issue reset/enable command to channel A */
    base_addr[SIO2681_REG_CRA] = SIO2681_CR_RX_ENABLE | SIO2681_CR_TX_ENABLE;  /* 0x05 */

    /* 0xE3346E "move.l (0x1c,A6),(-0xc,A3,D0w*0x1)" */
    ptrs->chan_b = chan_b_struct;

    /*
     * Initialize Channel B structure, 0xE33474..0xE334A4.  Its registers are
     * at base + 0x10 (0xE33474 "lea (0x10,A1),A3"), and the 0x18/0x12 pair is
     * the mirror image of channel A's.
     */
    chan_b_struct->regs = base_addr + 0x10;     /* 0xE3347C move.l A3,(A4)   */
    chan_b_struct->sio_desc = *chan_b_callback; /* 0xE33482 -> (0xc,A4)      */
    chan_b_struct->tx_int_mask = 0;             /* 0xE33486 clr.w (0x18,A4)  */
    chan_b_struct->int_bit = 4;                 /* 0xE3348A move.w #4,(0x12) */
    chan_b_struct->peer = chan_a_struct;        /* 0xE33490 -> (0x8,A4)      */
    chan_b_struct->chip = chip_struct;          /* 0xE33496 -> (0x4,A4)      */
    chan_b_struct->flags = 0;                   /* 0xE3349C clr.w (0x10,A4)  */
    chan_b_struct->reserved_14 = 0;             /* 0xE334A0 clr.l (0x14,A4)  */
    chan_b_struct->baud_support = 0;            /* 0xE334A4 clr.w (0x1a,A4)  */

    /* Issue reset/enable command to channel B */
    (base_addr + 0x10)[SIO2681_REG_CRA] = SIO2681_CR_RX_ENABLE | SIO2681_CR_TX_ENABLE;

    /*
     * Configure line parameters for both channels.
     * Mask 0x3FFF means set all parameters.
     */
    SIO2681_$SET_LINE(chan_a_struct, chan_a_params, 0x3FFF, &status);
    SIO2681_$SET_LINE(chan_b_struct, chan_b_params, 0x3FFF, &status);

    /*
     * 0xE334DE..0xE334F0: install this chip's interrupt stub.
     *
     *   move.w (A2),D0w                        ; chip_num
     *   lsl.w #0x2,D0w                         ; chip_num * 4
     *   move.w (A0),D1w                        ; the vector number
     *   movea.l #0x64,A1
     *   lsl.w #0x2,D1w
     *   move.l (-0x4,A5,D0w*0x1),(-0x4,A1,D1w*0x1)
     *
     * with A5 = 0x00E351EC.  The source index is 1-based on chip_num, and the
     * destination is 0x64 - 4 + vec*4 == the m68k exception vector table
     * entry `vec`, i.e. the vector number is 1-based against 0x64, the level-1
     * autovector (vector 25).
     */
    {
        int16_t vec_num = *int_vec_ptr;
        m68k_ptr_t *vector_table = (m68k_ptr_t *)ARCH_VA_TO_PTR(0x64);

        vector_table[vec_num - 1] =
            ARCH_PTR_TO_VA((const void *)SIO2681_$INT_VECTORS[chip_num - 1]);
    }

    /* Enable interrupts (write final IMR value) */
    base_addr[SIO2681_REG_IMR] = chip_struct->imr_shadow;
}
