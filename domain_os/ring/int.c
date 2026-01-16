/*
 * RING_$INT - Token ring interrupt handler
 *
 * Handles interrupts from the token ring hardware. This is called when
 * a packet has been received or a transmit operation has completed.
 *
 * The handler:
 *   1. Determines interrupt source (transmit or receive)
 *   2. Clears the interrupt condition
 *   3. Advances the appropriate event count to wake waiting processes
 *
 * Original address: 0x00E75748
 *
 * Assembly analysis:
 *   - Gets unit number from device_info + 6
 *   - Gets hardware register pointer from device_info + 0x34 (psVar2)
 *   - Checks psVar2[0] for receive interrupt (negative = pending)
 *   - Checks psVar2[1] for transmit interrupt (negative = pending)
 *   - On receive: clears status, advances tx_ec (+0x10)
 *   - On transmit: clears status, increments RCV_INT_CNT, calls MCR_CHANGE,
 *     processes packet, advances ec from FUN_00e75400 or ready_ec
 */

#include "ring/ring_internal.h"
#include "mmu/mmu.h"

/*
 * RING_$INT - Interrupt handler
 *
 * @param device_info   Device information structure
 *                      Contains unit number at +6, hw regs at +0x34
 *
 * @return 0xFF (interrupt handled)
 */
int8_t RING_$INT(void *device_info)
{
    int16_t unit_num;
    int16_t *hw_regs;
    ring_unit_t *unit_data;
    ec_$eventcount_t *ec;

    /*
     * Get unit number from device info.
     */
    unit_num = *(int16_t *)((uint8_t *)device_info + 6);

    /*
     * Get hardware register pointer from device info.
     * This points to the device status/control registers.
     */
    hw_regs = *(int16_t **)((uint8_t *)device_info + 0x34);

    /*
     * Calculate unit data pointer.
     */
    unit_data = &RING_$DATA.units[unit_num];

    /*
     * Check for receive interrupt.
     * If hw_regs[0] is negative (bit 15 set), receive interrupt pending.
     */
    if (hw_regs[0] < 0) {
        /*
         * Clear receive interrupt by writing 0 to status register.
         */
        hw_regs[0] = 0;

        /*
         * Advance the transmit event count to wake any waiters.
         * The tx_ec is at offset +0x10 in the unit structure.
         */
        ec = &unit_data->tx_ec;
    }
    else {
        /*
         * Check for transmit complete interrupt.
         * If hw_regs[1] is negative (bit 15 set), transmit interrupt pending.
         */
        if (hw_regs[1] >= 0) {
            /*
             * No interrupt pending - return immediately.
             */
            return (int8_t)-1;  /* 0xFF */
        }

        /*
         * Clear transmit interrupt by writing 0 to status register.
         */
        hw_regs[1] = 0;

        /*
         * Increment receive interrupt counter.
         */
        RING_$RCV_INT_CNT++;

        /*
         * Call MMU_$MCR_CHANGE to set memory control register mode 5.
         * This likely maps the DMA buffers for CPU access.
         */
        MMU_$MCR_CHANGE(5);

        /*
         * Process the received packet.
         * FUN_00e75400 (ring_$process_rx_packet) returns the event count
         * to advance, or NULL if the packet should be discarded.
         */
        ec = ring_$process_rx_packet(unit_data);

        if (ec == NULL) {
            /*
             * Packet discarded - increment statistics counter.
             * The counter is at a computed offset based on unit number.
             *
             * Assembly:
             *   move.w (0x6,A2),D0w    ; Get unit number
             *   lsl.w #0x2,D0w         ; * 4
             *   move.w D0w,D1w
             *   neg.w D0w
             *   lsl.w #0x4,D1w         ; * 16 = * 64 total, then - 4 = * 60 = 0x3C
             *   add.w D1w,D0w
             *   addq.l #0x1,(0x1c,A0,D0w*0x1)  ; Increment stat at +0x1C
             *
             * This calculates: unit_num * (-4 + 64) = unit_num * 60 = unit_num * 0x3C
             * Then adds 0x1C to get the discard counter offset.
             */
            RING_$STATS[unit_num]._reserved0++;  /* Actually a counter */

            return (int8_t)-1;  /* 0xFF */
        }
    }

    /*
     * Advance the event count without calling the dispatcher.
     * This wakes any processes waiting on this event count,
     * but doesn't immediately switch to them (since we're in
     * interrupt context).
     */
    EC_$ADVANCE_WITHOUT_DISPATCH(ec);

    return (int8_t)-1;  /* 0xFF */
}

/*
 * ring_$process_rx_packet - Process a received packet
 *
 * This function is called from the interrupt handler to process
 * a received packet. It validates the packet and determines
 * which event count to advance.
 *
 * Original address: 0x00E75400
 *
 * @param unit_data     Unit data structure
 *
 * @return Event count to advance, or NULL to discard packet
 */
ec_$eventcount_t *ring_$process_rx_packet(ring_unit_t *unit_data)
{
    /*
     * TODO: Implement full packet processing logic.
     *
     * The original function:
     *   1. Validates the received packet
     *   2. Checks packet type against registered handlers
     *   3. Determines if packet should be queued or discarded
     *   4. Returns the appropriate event count
     *
     * For now, return the ready_ec to signal packet arrival.
     */

    return &unit_data->ready_ec;
}
