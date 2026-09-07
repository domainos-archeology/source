/*
 * Ring module internal helper functions
 *
 * This file contains internal helper functions used by the ring module
 * for DMA setup, hardware control, and other low-level operations.
 */

#include "ring/ring_internal.h"
#include "misc/crash_system.h"

/*
 * ============================================================================
 * Hardware Register Access
 *
 * The token ring controller uses memory-mapped I/O at 0xFFA000.
 * DMA channels are at 0x40-byte intervals.
 * ============================================================================
 */

/* DMA controller base address */
#define DMA_BASE    ((volatile uint8_t *)RING_DMA_BASE)

/* Get pointer to DMA channel registers */
#define DMA_CHAN(n) ((volatile uint8_t *)(RING_DMA_BASE + ((n) << 6)))

/*
 * ring_$setup_tx_dma - Setup transmit DMA channel
 *
 * Configures DMA channel 2 for packet transmission. Handles both
 * header-only and header+data transmissions.
 *
 * Original address: 0x00E7584E
 *
 * Assembly analysis:
 *   - Sets byte count at +0x8A (word): (hdr_len + 1) / 2
 *   - Sets address at +0x8C (long): hdr_pa
 *   - Sets mode at +0x85: 0x12 (transmit mode)
 *   - If data_len == 0:
 *     - Sets +0xA9 = 0 (no chain)
 *     - Sets +0x87 = 0x80 (start single)
 *   - Else:
 *     - Sets +0x9A (word): (data_len + 1) / 2
 *     - Sets +0x9C (long): data_pa
 *     - Sets +0xA9 = 2 (chain enabled)
 *     - Sets +0xB9 = 1 (chain count)
 *     - Sets +0x87 = 0xC0 (start chained)
 *
 * @param hdr_pa        Header physical address
 * @param hdr_len       Header length in bytes
 * @param data_pa       Data physical address
 * @param data_len      Data length in bytes
 */
void ring_$setup_tx_dma(uint32_t hdr_pa, int16_t hdr_len,
                        uint32_t data_pa, int16_t data_len)
{
    volatile uint8_t *dma = DMA_BASE;
    int16_t hdr_words, data_words;

    /*
     * Calculate word count for header (round up).
     * Assembly: addq.l #1,D2; bpl skip; addq.l #1,D2; skip: asr.l #1,D2
     */
    hdr_words = (hdr_len + 1) / 2;

    /*
     * Set header DMA parameters.
     * Byte count register at +0x8A (channel 2 base + 0x0A).
     * Address register at +0x8C (channel 2 base + 0x0C).
     */
    *(volatile int16_t *)(dma + 0x8A) = hdr_words;
    *(volatile uint32_t *)(dma + 0x8C) = hdr_pa;

    /*
     * Set transmit mode.
     */
    dma[0x85] = RING_DMA_MODE_TX;  /* 0x12 */

    if (data_len == 0) {
        /*
         * Header-only transmission (no data segment).
         */
        dma[0xA9] = 0;                    /* No chain */
        dma[0x87] = RING_DMA_CTL_START;   /* 0x80 - start single */
    }
    else {
        /*
         * Header + data transmission (chained DMA).
         */
        data_words = (data_len + 1) / 2;

        /*
         * Set data DMA parameters.
         * These are at offset +0x10 from the header registers.
         */
        *(volatile int16_t *)(dma + 0x9A) = data_words;
        *(volatile uint32_t *)(dma + 0x9C) = data_pa;

        dma[0xA9] = 2;                    /* Chain enabled */
        dma[0xB9] = 1;                    /* Chain count */
        dma[0x87] = RING_DMA_CTL_CHAIN;   /* 0xC0 - start chained */
    }
}

/*
 * ring_$setup_rx_dma - Setup receive DMA channels
 *
 * Configures DMA channels 0 and 1 for packet reception.
 * Channel 0 receives the header, channel 1 receives the data.
 *
 * Original address: 0x00E758C4
 *
 * Assembly analysis:
 *   - Channel 0 (header): +0x0A = 0x200, +0x0C = hdr_buf, +0x29 = 0
 *   - Channel 1 (data):   +0x4A = 0x200, +0x4C = data_buf, +0x69 = 0
 *   - Both channels: mode = 0x92, control = 0x80
 *
 * @param hdr_buf       Header buffer physical address
 * @param data_buf      Data buffer physical address
 */
void ring_$setup_rx_dma(uint32_t hdr_buf, uint32_t data_buf)
{
    volatile uint8_t *dma = DMA_BASE;

    /*
     * Setup channel 0 (header receive).
     */
    *(volatile int16_t *)(dma + 0x0A) = RING_RX_BUF_SIZE;  /* 0x200 bytes */
    *(volatile uint32_t *)(dma + 0x0C) = hdr_buf;
    dma[0x29] = 0;

    /*
     * Setup channel 1 (data receive).
     */
    *(volatile int16_t *)(dma + 0x4A) = RING_RX_BUF_SIZE;  /* 0x200 bytes */
    *(volatile uint32_t *)(dma + 0x4C) = data_buf;
    dma[0x69] = 0;

    /*
     * Set receive mode and start both channels.
     */
    dma[0x05] = RING_DMA_MODE_RX;         /* 0x92 */
    dma[0x45] = RING_DMA_MODE_RX;         /* 0x92 */
    dma[0x07] = RING_DMA_CTL_START;       /* 0x80 */
    dma[0x47] = RING_DMA_CTL_START;       /* 0x80 */
}

/*
 * ring_$clear_dma_channel - Clear/abort a DMA channel
 *
 * Clears the specified DMA channel, optionally aborting any pending
 * transfer. Checks for hardware errors.
 *
 * Original address: 0x00E757E0
 *
 * Assembly analysis:
 *   - Gets channel status from base + (channel << 6)
 *   - If bit 3 set or (channel == 2 and hw bit 0 clear): write 0x10 to abort
 *   - If bit 4 set: crash with Network_hardware_error
 *   - Clear channel by writing 0xFF to status
 *
 * @param channel       Channel number (0, 1, or 2)
 * @param unit          Unit number
 */
void ring_$clear_dma_channel(int16_t channel, uint16_t unit)
{
    volatile uint8_t *chan_base;
    uint8_t status;
    ring_unit_t *unit_data;
    int16_t *hw_regs;

    /*
     * Get channel base address.
     */
    chan_base = DMA_CHAN(channel);

    /*
     * Get unit data for hardware register access.
     */
    unit_data = &RING_$CTL.units[unit];

    /*
     * Get hardware status register pointer from device info.
     */
    hw_regs = *(int16_t **)((uint8_t *)unit_data->device_info + 0x34);

    /*
     * Read channel status.
     */
    status = chan_base[0];

    /*
     * Check if abort is needed.
     * Bit 3 set indicates DMA error requiring abort.
     * For channel 2 (transmit), also check hardware status bit 0.
     */
    if ((status & 0x08) != 0) {
        /* DMA error - abort */
        chan_base[0x07] = RING_DMA_CTL_ABORT;  /* 0x10 */
    }
    else if (channel == 2) {
        /* Check transmit hardware status */
        if ((hw_regs[1] & 0x01) == 0) {
            chan_base[0x07] = RING_DMA_CTL_ABORT;  /* 0x10 */
        }
    }
    else {
        /*
         * Check for fatal hardware error (bit 4).
         */
        if ((status & 0x10) != 0) {
            CRASH_SYSTEM(&Network_hardware_error);
        }
    }

    /*
     * Clear the channel.
     */
    chan_base[0] = RING_DMA_CTL_CLEAR;  /* 0xFF */
}

/*
 * ring_$set_hw_mask - Set hardware transmit mask
 *
 * Sets the transmit mask register for a ring unit.
 * The mask controls which packet types are transmitted.
 *
 * Original address: 0x00E767CC
 *
 * @param unit          Unit number
 * @param mask          Mask value
 */
void ring_$set_hw_mask(uint16_t unit, uint16_t mask)
{
    ring_unit_t *unit_data;

    unit_data = &RING_$CTL.units[unit];

    /*
     * Store the mask in the unit structure.
     */
    unit_data->tmask = mask;

    /*
     * TODO(source-6co): the hardware write is missing.  ring_$set_hw_mask
     * (0x00E767CC) stores the mask into the unit record and then writes the
     * LOW byte of it to the controller's tmask register - RING_$RCV's copy of
     * the same idiom is `move.b` to hw_regs+4.  Re-emit against 0x00E767CC
     * with a volatile byte view of the register block.
     */
}

/*
 * ring_$do_start - Perform full unit start sequence
 *
 * Called when a unit is being started for the first time.
 * Sets up the receive process and enables the hardware.
 *
 * Original address: 0x00E7671C
 *
 * @param unit          Unit number
 * @param unit_data     Unit data structure
 * @param status_ret    Status output
 */
void ring_$do_start(uint16_t unit, ring_unit_t *unit_data, status_$t *status_ret)
{
    /*
     * Mark unit as started.
     */
    unit_data->state_flags |= RING_UNIT_STARTED;

    /*
     * TODO(source-6co): only the state flag is set.  ring_$do_start at
     * 0x00E7671C is 0xB0 bytes and the rest of it - the receive-process
     * creation, the hardware init and the initial DMA buffer setup - has not
     * been transcribed.  RING_$START (0x00E76830) is its only caller.
     */

    *status_ret = status_$ok;
}

/*
 * ring_$disable_interrupts - Disable interrupts for receive loop
 *
 * Called during startup and other critical sections.
 *
 * Original address: 0x00E7667C
 */
void ring_$disable_interrupts(void)
{
    /*
     * TODO(source-6co): the body is empty, and the NAME is wrong: 0x00E7667C
     * does not touch the SR at all.  It is 140 bytes that take the mutex at
     * 0x00E2C898 (`jsr 0x00E20DF8` at 0x00E7668E), test the counter at
     * RING globals +0x5B4, and on zero call 0x00E44BA4 with five arguments -
     * three of them `pea (d16,PC)` constant cells (0x00E766A0 `pea (0x66,PC)`,
     * 0x00E766A8 `pea (0x62,PC)`, 0x00E766AC `pea (0x6a,PC)`) - then walk the
     * 4-byte-stride table at RING globals +0x498.  Reverse it before renaming.
     */
}

/*
 * HDR_CHKSUM - Calculate the ring header checksum
 *
 * sum = 0; for each byte from hdr[12] up to hdr[*len_p - 1]:
 *     sum = (sum << 1) + byte      (16-bit)
 * and the result is the low byte of sum.  A header shorter than 13 bytes
 * checksums to zero, because `subi.w #0xd,D2w` / `bmi` skips the loop before
 * the accumulator is ever touched.
 *
 * The dbf count is *len_p - 13, i.e. *len_p - 12 iterations, and the index
 * register starts at 13 while the load is `move.b (-0x1,A0,D2w*0x1),D3b` --
 * so the first byte read is hdr[12], not hdr[13].
 *
 * Original address: 0x00E762CA
 *
 * @param hdr    Header base
 * @param len_p  Points at a WORD holding the header byte count
 *               (0x00E762D6 `movea.l (0xc,A6),A1` / 0x00E762DC `move.w (A1),D0w`)
 *
 * @return the checksum, in the low byte
 */
uint8_t HDR_CHKSUM(const void *hdr, const uint16_t *len_p)
{
    const uint8_t *bytes = (const uint8_t *)hdr;
    uint16_t sum;          /* D1 */
    int16_t  count;        /* D0, the dbf counter */
    uint16_t index;        /* D2 */

    sum = 0;                                        /* 0x00E762DA `clr.w D1w` */

    /* 0x00E762DE-0x00E762E4. */
    count = (int16_t)(*len_p) - 0x0D;
    if (count < 0) {
        return (uint8_t)(sum & 0x00FF);
    }

    index = 0x0D;                                   /* 0x00E762E8 `moveq #0xd,D2` */
    do {
        uint16_t b = bytes[index - 1];              /* 0x00E762F0 */

        sum = (uint16_t)(sum + sum);                /* 0x00E762EE `add.w D1w,D1w` */
        index = (uint16_t)(index + 1);              /* 0x00E762F4 */
        sum = (uint16_t)(sum + b);                  /* 0x00E762F6 */
        count--;
    } while (count >= 0);                           /* 0x00E762F8 `dbf D0w` */

    /* 0x00E762FE `andi.w #0xff,D0w`. */
    return (uint8_t)(sum & 0x00FF);
}
