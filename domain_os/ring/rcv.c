/*
 * Ring module receive functions
 *
 * This file contains the receive daemon functions for the ring module.
 * Each ring unit has its own receive daemon process that waits for
 * packets and dispatches them to the appropriate handlers.
 */

#include "ring/ring_internal.h"
#include "misc/crash_system.h"
#include "proc1/proc1.h"
#include "os/os_internal.h"

/*
 * RING_$RCV0 - Receive daemon for unit 0
 *
 * Main receive loop for ring unit 0. Runs as a separate process.
 * This is simply a wrapper that calls RING_$RCV_FROM_UNIT_PRIV with unit 0.
 *
 * Original address: 0x00E76642
 *
 * Assembly:
 *   link.w A6,0x0
 *   pea (A5)
 *   lea (0xe86400).l,A5        ; Load RING_$DATA base
 *   subq.l #0x2,SP
 *   clr.w -(SP)                 ; Push unit 0
 *   bsr.w RING_$RCV_FROM_UNIT_PRIV
 *   ... (never returns)
 */
void RING_$RCV0(void)
{
    RING_$RCV_FROM_UNIT_PRIV(0);
    /* Never returns */
}

/*
 * RING_$RCV1 - Receive daemon for unit 1
 *
 * Main receive loop for ring unit 1. Runs as a separate process.
 *
 * Original address: 0x00E7665E
 *
 * Assembly:
 *   link.w A6,0x0
 *   pea (A5)
 *   lea (0xe86400).l,A5
 *   subq.l #0x2,SP
 *   move.w #0x1,-(SP)           ; Push unit 1
 *   bsr.w RING_$RCV_FROM_UNIT_PRIV
 *   ... (never returns)
 */
void RING_$RCV1(void)
{
    RING_$RCV_FROM_UNIT_PRIV(1);
    /* Never returns */
}

/*
 * RING_$RCV_FROM_UNIT_PRIV - Privileged receive loop
 *
 * Internal receive loop implementation for a specific unit.
 * This is the main packet processing loop that:
 *   1. Waits for receive event
 *   2. Validates received packet
 *   3. Dispatches to appropriate handler
 *   4. Returns buffers and loops
 *
 * This function runs forever (infinite loop).
 *
 * Original address: 0x00E76048
 *
 * Assembly analysis (key points):
 *   - Calls IO_$GET_DCTE to get device info
 *   - Calls PROC1_$SET_LOCK(0xD) to set process lock
 *   - Stores hw_regs pointer from device_info + 0x34
 *   - Main loop:
 *     - Gets receive buffers via NETBUF_$GET_DAT/HDR
 *     - Calls ring_$setup_rx_dma to setup DMA
 *     - Configures hardware mode register
 *     - Advances ready_ec on first iteration
 *     - Waits on rx_wake_ec
 *     - Processes received packet
 *     - Returns buffers
 *
 * @param unit          Unit number (0 or 1)
 */
void RING_$RCV_FROM_UNIT_PRIV(uint16_t unit)
{
    ring_unit_t *unit_data;
    int16_t *hw_regs;
    status_$t status;
    int8_t first_time = (int8_t)-1;  /* true */
    int32_t wait_val;
    int8_t valid;

    /* Local buffer info */
    uint32_t rx_hdr_buf;
    uint32_t rx_data_buf;
    uint32_t hdr_info;
    uint32_t data_ptr;

    /* Receive header fields */
    int16_t hdr_len;
    int16_t data_len;

    /* Cleanup frame for FIM */
    uint8_t fim_cleanup[24];

    /* Packet info for dispatch */
    uint8_t pkt_param1[2];
    uint8_t pkt_param2[8];

    /*
     * Get device info via IO_$GET_DCTE.
     * This validates the unit and gets hardware pointers.
     */
    IO_$GET_DCTE(&ring_dcte_ctype_net, &unit, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /*
     * Set process lock to indicate we're a receive daemon.
     * Lock 0x0D is the process lock for ring receive.
     */
    PROC1_$SET_LOCK(0x0D);

    /*
     * Get unit data structure.
     */
    unit_data = &RING_$DATA.units[unit];

    /*
     * Get hardware register pointer from device info.
     */
    hw_regs = (int16_t *)unit_data->hw_regs;

    /*
     * Initialize wait value from current event count.
     */
    wait_val = unit_data->rx_wake_ec.value + 1;

    /*
     * Main receive loop - runs forever.
     */
    for (;;) {
        /*
         * Ensure we have receive buffers.
         */
        if (unit_data->rx_data_buf == NULL) {
            NETBUF_$GET_DAT((uint32_t *)&unit_data->rx_data_buf);
        }
        if (unit_data->rx_hdr_buf == NULL) {
            NETBUF_$GET_HDR((uint32_t *)&unit_data->rx_hdr_info,
                           &unit_data->rx_hdr_buf);
        }

        /*
         * Setup receive DMA with current buffers.
         */
        ring_$setup_rx_dma((uint32_t)(uintptr_t)unit_data->rx_hdr_buf,
                          (uint32_t)(uintptr_t)unit_data->rx_data_buf);

        /*
         * Configure hardware mode register.
         * Store receive buffer ID in hw_regs[4].
         */
        hw_regs[4] = (int16_t)(unit_data->tmask >> 8);

        /*
         * Clear busy flag in state.
         */
        unit_data->state_flags &= ~RING_UNIT_BUSY;

        /*
         * Configure hardware receive mode.
         */
        if (unit_data->tmask == 0) {
            hw_regs[6] = 0x1000;  /* Idle mode */
        }
        else {
            hw_regs[2] = 0x6000;  /* Active receive */
            hw_regs[6] = 0x2400;  /* Enable mode */
            /* Clear congestion flag in stats */
            RING_$STATS[unit].congestion_flag = 0;
        }

        /*
         * On first iteration, advance ready_ec to signal initialization complete.
         */
        if (first_time < 0) {
            EC_$ADVANCE(&unit_data->ready_ec);
            first_time = 0;  /* false */
        }

        /*
         * Wait for receive event.
         * This blocks until a packet arrives (interrupt advances rx_wake_ec).
         */
        EC_$WAIT((ec_$eventcount_t *[3]){&unit_data->rx_wake_ec, NULL, NULL},
                 &wait_val);
        wait_val++;

        /*
         * Increment wakeup counter.
         */
        RING_$WAKEUP_CNT++;

        /*
         * Save header info before processing.
         */
        hdr_info = unit_data->rx_hdr_info;

        /*
         * Check if unit became busy during wait.
         */
        if ((unit_data->state_flags & RING_UNIT_BUSY) != 0) {
            /*
             * Check hardware status for busy condition.
             */
            if ((hw_regs[2] & 0x2000) != 0) {
                RING_$BUSY_ON_RCV_INT++;
            }
        }
        else {
            /*
             * Normal receive - check hardware status.
             */
            if ((hw_regs[2] & 0x2000) != 0) {
                /*
                 * Hardware still busy - clear and retry.
                 */
                hw_regs[2] = 0;
                if ((hw_regs[2] & 0x2000) != 0) {
                    /*
                     * Still busy after clear - wait with timeout.
                     */
                    uint16_t delay_type = 0;  /* Relative delay */
                    clock_t timeout = {0, 0xABE};  /* ~2750 ticks */
                    TIME_$WAIT(&delay_type, &timeout, &status);
                    if (status != status_$ok) {
                        CRASH_SYSTEM(&status);
                    }
                    if ((hw_regs[2] & 0x2000) != 0) {
                        CRASH_SYSTEM(&No_available_socket_err);
                    }
                }

                /*
                 * Clear DMA channels and restart.
                 */
                ring_$clear_dma_channel(0, unit);
                ring_$clear_dma_channel(1, unit);
                continue;
            }
        }

        /*
         * Clear DMA channels.
         */
        ring_$clear_dma_channel(0, unit);
        ring_$clear_dma_channel(1, unit);

        /*
         * Validate received packet.
         */
        valid = ring_$validate_receive();

        if (valid < 0) {
            /*
             * Packet is valid - process it.
             */
            NETWORK_$ACTIVITY_FLAG = (int8_t)-1;  /* Set activity flag */

            /*
             * Get data pointer if there is data.
             */
            hdr_len = *(int16_t *)&hdr_info;
            if (hdr_len < 1) {
                data_ptr = 0;
            }
            else {
                data_ptr = (uint32_t)(uintptr_t)unit_data->rx_data_buf;
            }

            /*
             * Dispatch packet to handler.
             */
            ring_$receive_packet(unit, (void *)(uintptr_t)hdr_info,
                               (void *)(uintptr_t)data_ptr,
                               pkt_param1, pkt_param2);

            /*
             * Mark header buffer as consumed.
             */
            unit_data->rx_hdr_buf = NULL;

            /*
             * If data was used, mark data buffer as consumed.
             */
            if (data_ptr != 0) {
                unit_data->rx_data_buf = NULL;
            }
        }
        else {
            /*
             * Packet invalid or aborted.
             */
            RING_$ABORT_CNT++;
        }
    }
}

/*
 * ring_$validate_receive - Validate received packet
 *
 * Checks if a received packet is valid and should be processed.
 * This includes checking:
 *   - DMA completion status
 *   - Packet format validity
 *   - Checksum (if enabled)
 *
 * Original address: 0x00E75DE4
 *
 * @return Non-zero (negative) if packet is valid, 0 if invalid
 */
int8_t ring_$validate_receive(void)
{
    /*
     * TODO: Implement full validation logic.
     *
     * The original checks:
     *   - DMA status registers
     *   - Packet header magic/format
     *   - Optional checksum validation
     *
     * For now, return valid (assume DMA completed correctly).
     */
    return (int8_t)-1;  /* Valid */
}

/*
 * ring_$receive_packet - Dispatch received packet
 *
 * Routes a received packet to the appropriate handler based on
 * packet type and destination socket.
 *
 * Original address: 0x00E7649E
 *
 * @param unit          Unit number
 * @param hdr_info      Header info pointer
 * @param data_ptr      Data buffer pointer
 * @param param4        Additional parameter
 * @param param5        Additional parameter
 */
void ring_$receive_packet(uint16_t unit, void *hdr_info, void *data_ptr,
                          void *param4, void *param5)
{
    /*
     * TODO: Implement packet dispatch logic.
     *
     * The original function:
     *   1. Parses the packet header
     *   2. Looks up the destination socket
     *   3. Queues the packet for the socket
     *   4. Wakes any waiting processes
     */
    (void)unit;
    (void)hdr_info;
    (void)data_ptr;
    (void)param4;
    (void)param5;
}
