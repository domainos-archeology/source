/*
 * ring/ring_internal.h - Internal Ring Module Definitions
 *
 * Contains internal functions, data structures, and types used only within
 * the ring subsystem. External consumers should use ring/ring.h.
 */

#ifndef RING_INTERNAL_H
#define RING_INTERNAL_H

#include "ring/ring.h"
#include "time/time.h"
#include "io/io.h"
#include "network/network.h"
#include "route/route.h"
#include "netbuf/netbuf.h"
#include "sock/sock.h"
#include "pkt/pkt.h"
#include "fim/fim.h"
#include "net_io/net_io.h"
#include "parity/parity.h"

/*
 * ============================================================================
 * Hardware Addresses (DN300/DN3000 Token Ring Controller)
 * ============================================================================
 */

/*
 * DMA controller base address: a SAU2 hardware address, so it lives in
 * arch/m68k/sau2/hw.h (SAU2_RING_DMA_BASE, 0xFFA000).  The host build has no
 * such device; only the two byte-count registers below have host cells.
 */
#define RING_DMA_BASE           SAU2_RING_DMA_BASE

/* DMA channel offsets (0x40 bytes per channel) */
#define RING_DMA_CHAN0          0x00    /* Receive header */
#define RING_DMA_CHAN1          0x40    /* Receive data */
#define RING_DMA_CHAN2          0x80    /* Transmit */

/* DMA channel register offsets */
#define RING_DMA_STATUS         0x00    /* Status register */
#define RING_DMA_MODE           0x05    /* Mode register */
#define RING_DMA_CONTROL        0x07    /* Control register */
#define RING_DMA_BYTECOUNT      0x0A    /* Byte count (word) */
#define RING_DMA_ADDRESS        0x0C    /* Address (long) */
#define RING_DMA_EXTRA          0x29    /* Extra control */

/* DMA control values */
#define RING_DMA_CTL_START      0x80    /* Start DMA */
#define RING_DMA_CTL_CHAIN      0xC0    /* Chained DMA */
#define RING_DMA_CTL_CLEAR      0xFF    /* Clear channel */
#define RING_DMA_CTL_ABORT      0x10    /* Abort transfer */

/* DMA mode values */
#define RING_DMA_MODE_RX        0x92    /* Receive mode */
#define RING_DMA_MODE_TX        0x12    /* Transmit mode */

/* Receive buffer size */
#define RING_RX_BUF_SIZE        0x200   /* 512 bytes */

/*
 * DMA byte-count registers, read by ring_$validate_receive (0x00E75E24 and
 * 0x00E75E36) to work out how much of each buffer the controller filled.
 * Channel 0 carries the header, channel 1 the data; both are programmed for
 * RING_DMA_RX_WORDS bytes, so the transferred length is
 * RING_DMA_RX_WORDS - count*2.
 *
 * The host unit tests substitute plain memory cells for the memory mapped
 * registers; this is the only arch-specific hook the receive path needs.
 */
#define RING_DMA_RX_WORDS       0x400

#if defined(ARCH_HOST)
extern volatile uint16_t ring_$dma_chan0_count_cell;
extern volatile uint16_t ring_$dma_chan1_count_cell;
#define RING_DMA_CHAN0_COUNT    (&ring_$dma_chan0_count_cell)
#define RING_DMA_CHAN1_COUNT    (&ring_$dma_chan1_count_cell)
#else
#define RING_DMA_CHAN0_COUNT \
    ((volatile uint16_t *)(RING_DMA_BASE + RING_DMA_CHAN0 + RING_DMA_BYTECOUNT))
#define RING_DMA_CHAN1_COUNT \
    ((volatile uint16_t *)(RING_DMA_BASE + RING_DMA_CHAN1 + RING_DMA_BYTECOUNT))
#endif

/*
 * ============================================================================
 * Global Data Declarations
 *
 * Note: ring_global_t, ring_unit_t, and ring_channel_t are defined in ring.h
 * (public header) since they are part of the public API.
 * ============================================================================
 */

/* RING_$NETWORK_UID (0x00E1747C) is declared in ring/ring.h; RING_$INIT copies
 * it into RING_$CTL.network_uid (0xE86960 = RING_$CTL + 0x560). */

/* Device type for ring controller */
extern uint16_t ring_dcte_ctype_net;

/*
 * Error status constants.
 *
 * The receive path no longer uses "No_available_socket_err": the value it held
 * (0x0011000C) was a guess.  The status RING_$RCV_FROM_UNIT_PRIV actually
 * passes to CRASH_SYSTEM is the constant cell at 0x00E7628C (0x00110005), and
 * it lives with the code that uses it in ring/rcv.c.
 */
extern status_$t Network_hardware_error;

/*
 * The module data blocks - RING_$CTL (0xE86400), RING_$WIRED_DATA
 * (0xE261AC, holding RING_$XMIT_BIPHASE / RING_$XMIT_ESB / RING_$RCV_BIPHASE
 * / RING_$RCV_ESB, the software-diagnostic counters and RING_$DATA) and
 * RINGLOG_$CTL / RINGLOG_$DATA - are declared in ring/ring.h and
 * ring/ringlog.h.
 */

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * ring_$setup_tx_dma - Setup transmit DMA
 *
 * Configures the transmit DMA channel for packet transmission.
 *
 * @param hdr_pa        Header physical address
 * @param hdr_len       Header length
 * @param data_pa       Data physical address
 * @param data_len      Data length
 *
 * Original address: 0x00E7584E
 */
void ring_$setup_tx_dma(uint32_t hdr_pa, int16_t hdr_len,
                        uint32_t data_pa, int16_t data_len);

/*
 * ring_$setup_rx_dma - Setup receive DMA
 *
 * Configures the receive DMA channels for packet reception.
 *
 * @param hdr_buf       Header buffer address
 * @param data_buf      Data buffer address
 *
 * Original address: 0x00E758C4
 */
void ring_$setup_rx_dma(uint32_t hdr_buf, uint32_t data_buf);

/*
 * ring_$clear_dma_channel - Clear a DMA channel
 *
 * Clears the specified DMA channel, optionally aborting any pending
 * transfer. Checks for hardware errors.
 *
 * @param channel       Channel number (0, 1, or 2)
 * @param unit          Unit number
 *
 * Original address: 0x00E757E0
 */
void ring_$clear_dma_channel(int16_t channel, uint16_t unit);

/*
 * ring_$process_rx_packet - Process received packet
 *
 * Called from the receive interrupt handler to process a complete
 * received packet.
 *
 * @param unit_data     Pointer to unit data structure
 *
 * @return Event count to advance, or NULL
 *
 * Original address: 0x00E75400
 */
ec_$eventcount_t *ring_$process_rx_packet(ring_unit_t *unit_data);

/*
 * ring_$receive_packet - Internal receive packet handler
 *
 * Processes a received packet and dispatches it to the appropriate
 * socket.
 *
 * All four record parameters are Pascal var parameters - the caller passes the
 * ADDRESSES of its own locals (0x00E76250-0x00E7625C) and the routine reads
 * them back through those pointers.
 *
 * @param unit          Unit number
 * @param hdr_p         Address of the received header pointer
 * @param data_pa_p     Address of the data buffer DMA address (0 if no data)
 * @param hdr_len_p     Address of the received header byte count
 * @param data_len_p    Address of the received data byte count
 *
 * @return A word result slot the caller reserves but never reads; the routine
 *         itself never stores into it.
 *
 * Original address: 0x00E7649E
 */
int16_t ring_$receive_packet(uint16_t unit, ring_$pkt_hdr_t **hdr_p,
                             uint32_t *data_pa_p, int16_t *hdr_len_p,
                             int16_t *data_len_p);

/*
 * ring_$do_start - Internal start implementation
 *
 * @param unit          Unit number
 * @param unit_data     Unit data structure
 * @param status_ret    Status output
 *
 * Original address: 0x00E7671C
 */
void ring_$do_start(uint16_t unit, ring_unit_t *unit_data, status_$t *status_ret);

/*
 * ring_$set_hw_mask - Set hardware transmit mask
 *
 * @param unit          Unit number
 * @param mask          Mask value
 *
 * Original address: 0x00E767CC
 */
void ring_$set_hw_mask(uint16_t unit, uint16_t mask);

/*
 * ring_$open_internal - Internal channel open
 *
 * @param is_os         OS-level open flag
 * @param name          Channel name
 * @param args          Open arguments
 * @param status_ret    Status output
 *
 * Original address: 0x00E76B7C
 */
void ring_$open_internal(int8_t is_os, void *name, void *args, status_$t *status_ret);

/*
 * ring_$copy_data - Copy data for service calls
 *
 * @param iovecs        I/O vector array
 * @param iovec_cnt     Number of I/O vectors
 * @param offset_ptr    Current offset (updated)
 * @param count_ptr     Remaining count (updated)
 * @param dest_ptr      Destination pointer (updated)
 * @param len           Length to copy
 *
 * Original address: 0x00E76F1E
 */
void ring_$copy_data(void *iovecs, int16_t iovec_cnt, uint16_t *offset_ptr,
                     uint16_t *count_ptr, void **dest_ptr, uint16_t len);

/*
 * ring_$find_pkt_type - Find packet type in table
 *
 * @param pkt_type      Packet type to find
 * @param table         Packet type table
 * @param table_size    Table size
 *
 * @return Index if found, 0 if not found
 *
 * Original address: 0x00E7630C
 */
int16_t ring_$find_pkt_type(uint32_t pkt_type, ring_pkt_type_t *table,
                            uint16_t table_size);

/*
 * ring_$copy_to_user - Copy data to user buffer
 *
 * @param src_ptr       Source pointer (updated)
 * @param src_len       Source length
 * @param dest          Destination buffer
 * @param dest_param    Destination parameter
 * @param offset_ptr    Offset pointer (updated)
 * @param count_ptr     Count pointer (updated)
 *
 * Original address: 0x00E77382
 */
void ring_$copy_to_user(void **src_ptr, int16_t src_len, void *dest,
                        uint16_t dest_param, uint16_t *offset_ptr,
                        uint16_t *count_ptr);

/*
 * ring_$validate_receive (0x00E75DE4) is a nested Pascal procedure of
 * RING_$RCV_FROM_UNIT_PRIV - it reaches into the caller's frame through the
 * static link ("movea.l (A6),A2" at 0x00E75DEC).  It is therefore emitted as a
 * static function inside ring/rcv.c, not as a subsystem-wide entry point.
 */

/*
 * ring_$disable_interrupts - Disable for receive loop
 *
 * Original address: 0x00E7667C
 */
void ring_$disable_interrupts(void);

/*
 * HDR_CHKSUM - Calculate header checksum
 *
 * @param hdr           Header pointer
 * @param data          Data pointer
 *
 * @return Checksum value
 *
 * Original address: 0x00E762CA
 */
uint8_t HDR_CHKSUM(const void *hdr, const uint16_t *len_p);

/*
 * ============================================================================
 * External Functions Used by Ring
 *
 * Note: These declarations are provided for reference. The actual
 * declarations should come from the proper header files:
 *   - io/io.h for IO_$GET_DCTE
 *   - mmu/mmu.h for MMU_$MCR_CHANGE
 *   - proc1/proc1.h for PROC1_$SET_LOCK
 *   - misc/crash_system.h for CRASH_SYSTEM
 * ============================================================================
 */

/* NET_IO_$CREATE_PORT is declared in net_io/net_io.h (included above) */

/* PARITY_$CHK_IO is declared in parity/parity.h (included above) */

/* NETBUF_$GET_HDR is declared in netbuf/netbuf.h (included above) */

#endif /* RING_INTERNAL_H */
