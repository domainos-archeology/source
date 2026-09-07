/*
 * RING - Token Ring Network Module
 *
 * This module implements the token ring network interface for Domain/OS.
 * It provides the low-level driver for Apollo's token ring hardware,
 * supporting packet transmission/reception, interrupt handling, and
 * integration with the network I/O subsystem.
 *
 * The ring subsystem manages:
 * - Up to 2 token ring units (RING_MAX_UNITS)
 * - Per-unit event counts for synchronization
 * - DMA channels for transmit and receive
 * - Socket-based channel multiplexing (up to 10 channels per unit)
 * - Statistics collection for each unit
 *
 * Hardware: Apollo DN300/DN3000 token ring controller at 0xFFA000
 * DMA channels:
 *   - Channel 0 (0xFFA000): Receive header
 *   - Channel 1 (0xFFA040): Receive data
 *   - Channel 2 (0xFFA080): Transmit
 */

#ifndef RING_H
#define RING_H

#include "base/base.h"
#include "ec/ec.h"
#include "ml/ml.h"
#include "network/network.h"    /* status_$network_* */
#include "route/route.h"        /* status_$internet_* */

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Maximum number of ring units */
#define RING_MAX_UNITS          2

/* Maximum number of channels per unit */
#define RING_MAX_CHANNELS       10

/* Per-unit structure size */
#define RING_UNIT_SIZE          0x244

/* Per-unit statistics structure size */
#define RING_STATS_SIZE         0x3C

/* Maximum data length for packets */
#define RING_MAX_DATA_LEN       0x400   /* 1024 bytes */

/* Network header size */
#define RING_HDR_SIZE           0x1C    /* 28 bytes */

/*
 * ============================================================================
 * Status Codes (module 0x31 = RING)
 * ============================================================================
 */
/* Module 0x31, "OS / Ring", from the SR10.4 status database. */
#define status_$ring_not_implemented                0x00310001
#define status_$ring_invalid_unit_num               0x00310002
#define status_$ring_illegal_header_length          0x00310003
#define status_$ring_invalid_data_length            0x00310004
#define status_$ring_transmit_failed                0x00310005
#define status_$ring_no_packet_to_receive           0x00310006
#define status_$ring_pkt_type_in_use                0x00310007
#define status_$ring_no_channels                    0x00310008
#define status_$ring_invalid_svc_packet_type        0x00310009
#define status_$ring_channel_not_open               0x0031000A
#define status_$ring_device_offline                 0x0031000B
#define status_$ring_device_already_online          0x0031000C
#define status_$ring_internal_driver_error          0x0031000D
#define status_$ring_controller_hardware_error      0x0031000E
#define status_$ring_pkt_type_not_in_use            0x0031000F
#define status_$ring_driver_version_mismatch        0x00310010
#define status_$ring_invalid_stats_block            0x00310011
#define status_$ring_illegal_dest_address           0x00310012

#define status_$io_controller_not_in_system         0x00100002

/*
 * status_$internet_unknown_network_port: route/route.h
 * status_$network_transmit_failed, status_$network_data_length_too_large,
 * status_$network_memory_parity_error_during_transmit: network/network.h
 */

/*
 * ============================================================================
 * Transmit Status Flags
 * ============================================================================
 */

/* Transmit result flags (returned in status byte) */
#define RING_TX_FLAG_SUCCESS        0x80    /* Transmission successful */
#define RING_TX_FLAG_COLLISION      0x40    /* Collision detected */
#define RING_TX_FLAG_NO_RESPONSE    0x20    /* No response from destination */
#define RING_TX_FLAG_ABORT          0x08    /* Transmission aborted */
#define RING_TX_FLAG_RETRY          0x04    /* Retry required */
#define RING_TX_FLAG_ERROR          0x02    /* General error */
#define RING_TX_FLAG_TIMEOUT        0x01    /* Timeout occurred */

/* Extended status flags (second byte) */
#define RING_TX_EXT_PARITY          0x80    /* Parity error */
#define RING_TX_EXT_PROTOCOL        0x40    /* Protocol error */
#define RING_TX_EXT_BIPHASE         0x20    /* Biphase error */
#define RING_TX_EXT_NOT_IN_SYSTEM   0x10    /* Controller not in system */
#define RING_TX_EXT_CONGESTION      0x08    /* Network congestion */
#define RING_TX_EXT_ESB             0x04    /* ESB error */

/*
 * ============================================================================
 * Unit Flags
 * ============================================================================
 */

/* Unit state flags (at offset +0x31 in unit structure) */
#define RING_UNIT_STARTED       0x01    /* Unit has been started */
#define RING_UNIT_RUNNING       0x02    /* Unit is actively running */
#define RING_UNIT_BUSY          0x04    /* Unit is busy with I/O */

/* Maximum number of packet-type table entries per unit (0x00E76C8A) */
#define RING_MAX_PKT_TYPES      0x20

/* Socket id stored in a channel that was opened by the OS (0x00E76D20) */
#define RING_OS_SOCKET_ID       0x00E1

/*
 * ============================================================================
 * Token ring controller register block
 *
 * The DCTE holds the pointer at +0x34; RING_$INIT caches it in the unit
 * structure at +0x1C.  The registers are addressed as two words, one byte and
 * one word (byte offsets 0, 2, 4, 6) - see ring_$clear_dma_channel
 * (0x00E75812), RING_$INT (0x00E7576C/0x00E7577A) and
 * RING_$RCV_FROM_UNIT_PRIV (0x00E760F4/0x00E7610A/0x00E76114).
 *
 * All accesses are memory mapped I/O and must not be reordered or elided,
 * hence the volatile qualifiers.
 * ============================================================================
 */
typedef struct ring_hw_regs_t {
    volatile uint16_t   xmit_csr;   /* 0x00: transmit status/control */
    volatile uint16_t   rcv_csr;    /* 0x02: receive status/control */
    volatile uint8_t    tmask;      /* 0x04: transmit mask (low byte of unit->tmask) */
    volatile uint8_t    _pad05;     /* 0x05 */
    volatile uint16_t   mode;       /* 0x06: receiver mode */
} ring_hw_regs_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(ring_hw_regs_t, xmit_csr) == 0x00, "ring_hw_regs_t.xmit_csr");
_Static_assert(__builtin_offsetof(ring_hw_regs_t, rcv_csr) == 0x02, "ring_hw_regs_t.rcv_csr");
_Static_assert(__builtin_offsetof(ring_hw_regs_t, tmask) == 0x04, "ring_hw_regs_t.tmask");
_Static_assert(__builtin_offsetof(ring_hw_regs_t, _pad05) == 0x05, "ring_hw_regs_t._pad05");
_Static_assert(__builtin_offsetof(ring_hw_regs_t, mode) == 0x06, "ring_hw_regs_t.mode");

/*
 * Receive status register access
 *
 * On the target these are plain volatile MMIO reads and writes of
 * ring_hw_regs_t.rcv_csr.  On a host build they go through functions the
 * unit tests supply, so that a test can model a controller whose receive
 * status still reads busy after the register has been written zero - which is
 * the condition RING_$RCV_FROM_UNIT_PRIV's recovery arm exists for
 * (0x00E7618A-0x00E761E2) and which plain memory cannot reproduce.
 *
 * The other three registers are write-only on this path, so they stay direct.
 */
#if defined(ARCH_M68K)
#define RING_$RCV_CSR_READ(regs)        ((regs)->rcv_csr)
#define RING_$RCV_CSR_WRITE(regs, v)    ((regs)->rcv_csr = (uint16_t)(v))
#else
uint16_t ring_$rcv_csr_read(ring_hw_regs_t *regs);
void     ring_$rcv_csr_write(ring_hw_regs_t *regs, uint16_t value);
#define RING_$RCV_CSR_READ(regs)        ring_$rcv_csr_read(regs)
#define RING_$RCV_CSR_WRITE(regs, v)    ring_$rcv_csr_write((regs), (uint16_t)(v))
#endif

/* Bits of ring_hw_regs_t.rcv_csr tested by the driver */
#define RING_RCV_CSR_BUSY       0x2000  /* btst #13 - receiver still active */

/* Values written to ring_hw_regs_t */
#define RING_RCV_CSR_ARM        0x6000  /* 0x00E7610A */
#define RING_MODE_ENABLE        0x2400  /* 0x00E76114 - tmask nonzero */
#define RING_MODE_IDLE          0x1000  /* 0x00E76134 - tmask zero */

/*
 * ============================================================================
 * Received packet header (the buffer handed out by NETBUF_$GET_HDR)
 *
 * Only the fields touched by the receive path are named.  Offsets are taken
 * from ring_$validate_receive (0x00E75E48-0x00E75E6C), ring_$receive_packet
 * (0x00E764BE-0x00E76542) and ring_$process_rx_packet (0x00E7541C onward).
 * ============================================================================
 */
typedef struct ring_$pkt_hdr_t {
    uint32_t    msg_type;       /* 0x00: message type (1 and 3 are diagnostics) */
    uint8_t     flags;          /* 0x04: bit0 route, bit1/bit4 swdiag, bit7 local */
    uint8_t     _r05[2];        /* 0x05 */
    uint8_t     flags7;         /* 0x07: bit3 tested by ring_$receive_packet */
    uint32_t    src_id;         /* 0x08: source node id */
    uint8_t     _r0c;           /* 0x0C */
    uint8_t     chksum;         /* 0x0D: header checksum */
    uint8_t     pkt_class;      /* 0x0E */
    uint8_t     _r0f;           /* 0x0F */
    uint16_t    hdr_len;        /* 0x10: header byte count */
    uint16_t    _r12;           /* 0x12 */
    uint16_t    data_len;       /* 0x14: data byte count */
    uint16_t    _r16;           /* 0x16 */
    uint32_t    route_info;     /* 0x18: passed to the socket/route layer */
} ring_$pkt_hdr_t;

/*
 * ============================================================================
 * Channel Entry Structure (8 bytes per entry)
 *
 * The Pascal array is 1-based: entry i lives at unit + 0x5A + 8*i, so entry 1
 * is at unit + 0x62 (RING_$INIT 0x00E2FB56, RING_$SVC_CLOSE 0x00E76E72,
 * ring_$open_internal 0x00E76D72).  The C array below starts at 0x62 and is
 * therefore indexed with [i - 1].
 * ============================================================================
 */
typedef struct ring_channel_t {
    boolean     flags;          /* 0x00: -1 (0xFF) = channel open */
    int8_t      _pad01;         /* 0x01 */
    int16_t     asid;           /* 0x02: PROC1_$AS_ID of the opener (0x00E76D76) */
    int16_t     socket_id;      /* 0x04: socket, or RING_OS_SOCKET_ID (0x00E76D7E) */
    int16_t     open_version;   /* 0x06: the driver interface version the
                                 *       channel was opened with - the first
                                 *       word of ring_$open_options_t
                                 *       ("move.w (A1),(0x60,A0)" at
                                 *       0x00E76D84).  ring_$open_internal
                                 *       rejects anything above 1 with
                                 *       0x00310010, "driver version
                                 *       mismatch" (0x00E76BAE). */
} ring_channel_t;

/* Remaining documented offsets (bead source-pewa). */
_Static_assert(__builtin_offsetof(ring_channel_t, flags) == 0x00, "ring_channel_t.flags");
_Static_assert(__builtin_offsetof(ring_channel_t, _pad01) == 0x01, "ring_channel_t._pad01");
_Static_assert(__builtin_offsetof(ring_channel_t, asid) == 0x02, "ring_channel_t.asid");
_Static_assert(__builtin_offsetof(ring_channel_t, socket_id) == 0x04, "ring_channel_t.socket_id");

/*
 * ============================================================================
 * Packet type table entry (12 bytes per entry)
 *
 * Also a 1-based Pascal array: entry i is at unit + 0xA8 + 12*i, so entry 1 is
 * at unit + 0xB4, which is the base ring_$open_internal (0x00E76CD0) and
 * ring_$receive_packet (0x00E764D2) hand to ring_$find_pkt_type.
 * ============================================================================
 */
typedef struct ring_pkt_type_t {
    uint32_t    low;            /* 0x00: inclusive low bound of the type range */
    uint32_t    high;           /* 0x04: inclusive high bound of the type range */
    int16_t     channel;        /* 0x08: owning channel (1-based) */
    int16_t     _pad0a;         /* 0x0A */
} ring_pkt_type_t;

/* Remaining documented offsets (bead source-pewa). */
_Static_assert(__builtin_offsetof(ring_pkt_type_t, low) == 0x00, "ring_pkt_type_t.low");
_Static_assert(__builtin_offsetof(ring_pkt_type_t, high) == 0x04, "ring_pkt_type_t.high");
_Static_assert(__builtin_offsetof(ring_pkt_type_t, channel) == 0x08, "ring_pkt_type_t.channel");
_Static_assert(__builtin_offsetof(ring_pkt_type_t, _pad0a) == 0x0A, "ring_pkt_type_t._pad0a");

/*
 * ============================================================================
 * Per-Unit Data Structure (0x244 bytes)
 *
 * Located at RING_DATA_BASE + (unit * RING_UNIT_SIZE)
 * ============================================================================
 */
typedef struct ring_unit_t {
    void               *route_port;         /* 0x000: ROUTE_$PORT_ARRAY entry (0x00E2FBBA) */
    ec_$eventcount_t    rx_wake_ec;         /* 0x004: receive wake eventcount */
    ec_$eventcount_t    tx_ec;              /* 0x010: transmit-done eventcount */
    ring_hw_regs_t     *hw_regs;            /* 0x01C: cached DCTE+0x34 register pointer */
    void               *device_info;        /* 0x020: DCTE (0x00E2FB14) */
    ec_$eventcount_t    ready_ec;           /* 0x024: receive daemon ready eventcount */
    uint8_t             _r030;              /* 0x030 */
    uint8_t             state_flags;        /* 0x031: RING_UNIT_* bits */
    uint16_t            tmask;              /* 0x032: transmit mask */
    ml_$exclusion_t     tx_exclusion;       /* 0x034: transmit exclusion (18 bytes) */
    uint8_t             _r046[2];           /* 0x046 */
    int16_t             open_count;         /* 0x048: outstanding opens (0x00E76BEC) */
    uint8_t             _r04a[2];           /* 0x04A */
    ml_$exclusion_t     rx_exclusion;       /* 0x04C: channel table exclusion (18 bytes) */
    uint8_t             _r05e[2];           /* 0x05E */
    boolean             initialized;        /* 0x060: -1 once RING_$INIT succeeded */
    uint8_t             _r061;              /* 0x061 */
    ring_channel_t      channels[RING_MAX_CHANNELS];    /* 0x062..0x0B1 (1-based: [i-1]) */
    uint8_t             _r0b2[2];           /* 0x0B2 */
    ring_pkt_type_t     pkt_types[RING_MAX_PKT_TYPES];  /* 0x0B4..0x233 (1-based: [i-1]) */
    uint16_t            pkt_type_cnt;       /* 0x234: entries in use */
    uint16_t            _r236;              /* 0x236 */
    uint32_t            rx_hdr_pa;          /* 0x238: header buffer DMA address */
    ring_$pkt_hdr_t    *rx_hdr;             /* 0x23C: header buffer virtual address */
    uint32_t            rx_data_pa;         /* 0x240: data buffer DMA address */
} ring_unit_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(ring_unit_t, route_port) == 0x00, "ring_unit_t.route_port");
_Static_assert(__builtin_offsetof(ring_unit_t, _r030) == 0x30, "ring_unit_t._r030");
_Static_assert(__builtin_offsetof(ring_unit_t, _r046) == 0x46, "ring_unit_t._r046");
_Static_assert(__builtin_offsetof(ring_unit_t, _r04a) == 0x4A, "ring_unit_t._r04a");
_Static_assert(__builtin_offsetof(ring_unit_t, _r05e) == 0x5E, "ring_unit_t._r05e");
_Static_assert(__builtin_offsetof(ring_unit_t, _r061) == 0x61, "ring_unit_t._r061");
_Static_assert(__builtin_offsetof(ring_unit_t, _r0b2) == 0xB2, "ring_unit_t._r0b2");
_Static_assert(__builtin_offsetof(ring_unit_t, _r236) == 0x236, "ring_unit_t._r236");
#endif

#if defined(ARCH_M68K)
_Static_assert(offsetof(ring_unit_t, rx_wake_ec)   == 0x004, "ring_unit_t.rx_wake_ec");
_Static_assert(offsetof(ring_unit_t, tx_ec)        == 0x010, "ring_unit_t.tx_ec");
_Static_assert(offsetof(ring_unit_t, hw_regs)      == 0x01C, "ring_unit_t.hw_regs");
_Static_assert(offsetof(ring_unit_t, device_info)  == 0x020, "ring_unit_t.device_info");
_Static_assert(offsetof(ring_unit_t, ready_ec)     == 0x024, "ring_unit_t.ready_ec");
_Static_assert(offsetof(ring_unit_t, state_flags)  == 0x031, "ring_unit_t.state_flags");
_Static_assert(offsetof(ring_unit_t, tmask)        == 0x032, "ring_unit_t.tmask");
_Static_assert(offsetof(ring_unit_t, tx_exclusion) == 0x034, "ring_unit_t.tx_exclusion");
_Static_assert(offsetof(ring_unit_t, open_count)   == 0x048, "ring_unit_t.open_count");
_Static_assert(offsetof(ring_unit_t, rx_exclusion) == 0x04C, "ring_unit_t.rx_exclusion");
_Static_assert(offsetof(ring_unit_t, initialized)  == 0x060, "ring_unit_t.initialized");
_Static_assert(offsetof(ring_unit_t, channels)     == 0x062, "ring_unit_t.channels");
_Static_assert(offsetof(ring_unit_t, pkt_types)    == 0x0B4, "ring_unit_t.pkt_types");
_Static_assert(offsetof(ring_unit_t, pkt_type_cnt) == 0x234, "ring_unit_t.pkt_type_cnt");
_Static_assert(offsetof(ring_unit_t, rx_hdr_pa)    == 0x238, "ring_unit_t.rx_hdr_pa");
_Static_assert(offsetof(ring_unit_t, rx_hdr)       == 0x23C, "ring_unit_t.rx_hdr");
_Static_assert(offsetof(ring_unit_t, rx_data_pa)   == 0x240, "ring_unit_t.rx_data_pa");
_Static_assert(sizeof(ring_unit_t)                 == RING_UNIT_SIZE, "sizeof ring_unit_t");
_Static_assert(sizeof(ring_channel_t)              == 8, "sizeof ring_channel_t");
_Static_assert(sizeof(ring_pkt_type_t)             == 12, "sizeof ring_pkt_type_t");
#endif /* ARCH_M68K */

/*
 * ============================================================================
 * Global Ring Data Structure
 *
 * Located at RING_DATA_BASE (0xE86400).  This is the A5 module base every
 * ring routine loads with "lea (0xe86400).l,A5".
 * ============================================================================
 */
typedef struct ring_global_t {
    ring_unit_t     units[RING_MAX_UNITS];  /* 0x000: per-unit data */
    uint8_t         scrub[0x10];            /* 0x488: RING_$SCRUB */
    uint32_t        wire_list[0x20];        /* 0x498: wired page list (0x00E766A4) */
    uint16_t        _r518;                  /* 0x518: passed to NET_IO_$CREATE_PORT */
    uint16_t        max_data_len;           /* 0x51A: max data length (0x00E75974) */
    uint8_t         _r51c[0x44];            /* 0x51C */
    uid_t           network_uid;            /* 0x560: network UID (0x00E2FB0C) */
    clock_t         force_start_timeout;    /* 0x568: RING_$FORCE_START (6 bytes) */
    uint8_t         _r56e[0x0A];            /* 0x56E */
    clock_t         xmit_timeout1;          /* 0x578 */
    uint8_t         _r57e[0x02];            /* 0x57E */
    clock_t         xmit_timeout2;          /* 0x580 */
    uint8_t         _r586[0x0A];            /* 0x586 */
    clock_t         poll_timeout;           /* 0x590 */
    uint8_t         _r596[0x02];            /* 0x596 */
    clock_t         wait_timeout;           /* 0x598 */
    uint8_t         _r59e[0x02];            /* 0x59E */
    int16_t         port_array[RING_MAX_UNITS]; /* 0x5A0: NET_IO port per unit */
    uint32_t        set_tmask_chg_cnt;      /* 0x5A4: RING_$SET_TMASK_CHG_CNT */
    uint32_t        unit_tmask_chg_cnt;     /* 0x5A8: RING_$UNIT_TMASK_CHG_CNT */
    uint32_t        tmask_chg_and_busy_cnt; /* 0x5AC: RING_$TMASK_CHG_AND_BUSY_CNT */
    uint32_t        rcv_int_cnt;            /* 0x5B0: RING_$RCV_INT_CNT */
    uint16_t        wire_cnt;               /* 0x5B4: entries used in wire_list */
    uint16_t        unexpected_xmit_stat;   /* 0x5B6: RING_$UNEXPECTED_XMIT_STAT */
    uint16_t        bad_data_cnt;           /* 0x5B8: RING_$BAD_DATA_CNT */
    uint16_t        wakeup_cnt;             /* 0x5BA: RING_$WAKEUP_CNT */
    uint16_t        abort_cnt;              /* 0x5BC: RING_$ABORT_CNT */
    uint16_t        busy_on_rcv_int;        /* 0x5BE: RING_$BUSY_ON_RCV_INT */
    uint32_t        send_null_cnt;          /* 0x5C0: RING_$SEND_NULL_CNT */
    uint16_t        xmit_waited;            /* 0x5C4: RING_$XMIT_WAITED */
    uint16_t        _r5c6;                  /* 0x5C6 */
    void          (*rcv_proc[RING_MAX_UNITS])(void); /* 0x5C8: RING_$RCV0 / RING_$RCV1 */
} ring_global_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(ring_global_t, units) == 0x00, "ring_global_t.units");
_Static_assert(__builtin_offsetof(ring_global_t, _r518) == 0x518, "ring_global_t._r518");
_Static_assert(__builtin_offsetof(ring_global_t, _r51c) == 0x51C, "ring_global_t._r51c");
_Static_assert(__builtin_offsetof(ring_global_t, _r56e) == 0x56E, "ring_global_t._r56e");
_Static_assert(__builtin_offsetof(ring_global_t, _r57e) == 0x57E, "ring_global_t._r57e");
_Static_assert(__builtin_offsetof(ring_global_t, _r586) == 0x586, "ring_global_t._r586");
_Static_assert(__builtin_offsetof(ring_global_t, _r596) == 0x596, "ring_global_t._r596");
_Static_assert(__builtin_offsetof(ring_global_t, _r59e) == 0x59E, "ring_global_t._r59e");
_Static_assert(__builtin_offsetof(ring_global_t, tmask_chg_and_busy_cnt) == 0x5AC, "ring_global_t.tmask_chg_and_busy_cnt");
_Static_assert(__builtin_offsetof(ring_global_t, _r5c6) == 0x5C6, "ring_global_t._r5c6");
#endif

#if defined(ARCH_M68K)
_Static_assert(offsetof(ring_global_t, scrub)               == 0x488, "ring_global_t.scrub");
_Static_assert(offsetof(ring_global_t, wire_list)           == 0x498, "ring_global_t.wire_list");
_Static_assert(offsetof(ring_global_t, max_data_len)        == 0x51A, "ring_global_t.max_data_len");
_Static_assert(offsetof(ring_global_t, network_uid)         == 0x560, "ring_global_t.network_uid");
_Static_assert(offsetof(ring_global_t, force_start_timeout) == 0x568, "ring_global_t.force_start_timeout");
_Static_assert(offsetof(ring_global_t, xmit_timeout1)       == 0x578, "ring_global_t.xmit_timeout1");
_Static_assert(offsetof(ring_global_t, xmit_timeout2)       == 0x580, "ring_global_t.xmit_timeout2");
_Static_assert(offsetof(ring_global_t, poll_timeout)        == 0x590, "ring_global_t.poll_timeout");
_Static_assert(offsetof(ring_global_t, wait_timeout)        == 0x598, "ring_global_t.wait_timeout");
_Static_assert(offsetof(ring_global_t, port_array)          == 0x5A0, "ring_global_t.port_array");
_Static_assert(offsetof(ring_global_t, set_tmask_chg_cnt)   == 0x5A4, "ring_global_t.set_tmask_chg_cnt");
_Static_assert(offsetof(ring_global_t, unit_tmask_chg_cnt)  == 0x5A8, "ring_global_t.unit_tmask_chg_cnt");
_Static_assert(offsetof(ring_global_t, rcv_int_cnt)         == 0x5B0, "ring_global_t.rcv_int_cnt");
_Static_assert(offsetof(ring_global_t, wire_cnt)            == 0x5B4, "ring_global_t.wire_cnt");
_Static_assert(offsetof(ring_global_t, unexpected_xmit_stat)== 0x5B6, "ring_global_t.unexpected_xmit_stat");
_Static_assert(offsetof(ring_global_t, bad_data_cnt)        == 0x5B8, "ring_global_t.bad_data_cnt");
_Static_assert(offsetof(ring_global_t, wakeup_cnt)          == 0x5BA, "ring_global_t.wakeup_cnt");
_Static_assert(offsetof(ring_global_t, abort_cnt)           == 0x5BC, "ring_global_t.abort_cnt");
_Static_assert(offsetof(ring_global_t, busy_on_rcv_int)     == 0x5BE, "ring_global_t.busy_on_rcv_int");
_Static_assert(offsetof(ring_global_t, send_null_cnt)       == 0x5C0, "ring_global_t.send_null_cnt");
_Static_assert(offsetof(ring_global_t, xmit_waited)         == 0x5C4, "ring_global_t.xmit_waited");
_Static_assert(offsetof(ring_global_t, rcv_proc)            == 0x5C8, "ring_global_t.rcv_proc");
#endif /* ARCH_M68K */

/*
 * ============================================================================
 * Public Data
 * ============================================================================
 */

/* Ring global data structure */
extern ring_global_t RING_$DATA;

/*
 * Software-diagnostic counters.  These live just below the per-unit statistics
 * array, at 0x00E261AC..0x00E261DF; they are NOT part of RING_$DATA.
 */
extern uint32_t RING_$SWDIAG_NODEID;    /* 0x00E261AC */
extern uint32_t RING_$SWDIAG_GOODRCV_CNT; /* 0x00E261B0 */
extern uint32_t RING_$SWDIAG_RCVCNT;    /* 0x00E261B4 */
extern uint16_t RING_$RCV_BIPHASE;      /* 0x00E261B8 */
extern uint16_t RING_$RCV_ESB;          /* 0x00E261BA */
extern uint16_t RING_$XMIT_BIPHASE;     /* 0x00E261BC */
extern uint16_t RING_$XMIT_ESB;         /* 0x00E261BE */
extern uint16_t RING_$PAGING_OVERFLOW;  /* 0x00E261C0 */

/*
 * RING_$SWDIAG_DATA (0x00E261C2) - software diagnostic error counters,
 * bumped alongside the per-unit statistics by ring_$validate_receive when
 * the packet came from the software diagnostic (flags bit1 and bit4 set).
 *
 * From +0x06 on this is the SAME ten-word receive-error block as
 * ring_$stats_t+0x20, at a uniform displacement of -0x1A: every mirror
 * ring_$validate_receive touches is exactly its stats counter minus 0x1A
 * (0x20/0x06, 0x22/0x08, 0x24/0x0A, 0x28/0x0E, 0x2A/0x10, 0x2E/0x14,
 * 0x30/0x16).  The two mirrors the receive path never writes, +0x0C and
 * +0x12, are the two whose stats counterparts it also never writes normally
 * (rcvbus CRASH_SYSTEMs first, rcvovr is not a CSR condition), which is what
 * pins the correspondence.  See ring_$stats_t for where the names come from.
 */
typedef struct ring_$swdiag_t {
    uint16_t    _r00;                   /* 0x00 (0x00E261C2) */
    uint16_t    _r02;                   /* 0x02 */
    uint16_t    _r04;                   /* 0x04 */
    uint16_t    rcveor;                 /* 0x06: rcv_csr bit 5  (0x00E75FD6) */
    uint16_t    rcvcrc;                 /* 0x08: rcv_csr bit 8  (0x00E76004) */
    uint16_t    rcvtim;                 /* 0x0A: rcv_csr bit 9  (0x00E75FA2) */
    uint16_t    rcvbus;                 /* 0x0C: never written - bit 6 crashes */
    uint16_t    rcvmodem;               /* 0x0E: rcv_csr bit 3  (0x00E7602E) */
    uint16_t    rcvpkt;                 /* 0x10: rcv_csr bit 10/11 (0x00E75F86) */
    uint16_t    rcvovr;                 /* 0x12: never written by the receive path */
    uint16_t    rcvapar;                /* 0x14: rcv_csr bit 0  (0x00E75FEC) */
    uint16_t    rcvxerr;                /* 0x16: rcv_csr bit 7  (0x00E76016) */
} ring_$swdiag_t;

extern ring_$swdiag_t RING_$SWDIAG_DATA;

/* Network UID for ring interface */
extern uid_t RING_$NETWORK_UID;

/*
 * ============================================================================
 * Statistics Structure
 * ============================================================================
 */

/*
 * Per-unit statistics (0x3C bytes)
 * Located at 0xE261E0 + (unit * 0x3C), indexed with a 0-based unit number.
 *
 * RING_$GET_STATS (0x00E76950) copies the whole block out verbatim - fifteen
 * longwords, "moveq #0xe,D1 / move.l (A3)+,(A4)+ / dbf" at 0x00E7699C - and
 * reports its size as 0x3C, so the kernel itself never names the fields.
 *
 * The receive error counters at 0x20..0x32 are decoded from the receive
 * status register by ring_$validate_receive (0x00E75F60-0x00E76032), which
 * tests the bits in this order and stops at the first match:
 *
 *   bit 10 or 11  -> +0x2A, swdiag +0x10   (also bumps RING_$RCV_BIPHASE /
 *                                           RING_$RCV_ESB at 0x00E261B8 /
 *                                           0x00E261BA)
 *   bit 9         -> +0x24, swdiag +0x0A
 *   bit 6         -> CRASH_SYSTEM first (0x00E75FB4), then +0x26; this is the
 *                    only one with no software-diagnostic mirror
 *   bit 5         -> +0x20, swdiag +0x06
 *   bit 0         -> +0x2E, swdiag +0x14
 *   bit 8         -> +0x22, swdiag +0x08
 *   bit 7         -> +0x30, swdiag +0x16
 *   bit 3         -> +0x28, swdiag +0x0E
 *
 * The swdiag mirror is bumped only when the packet came from the software
 * diagnostic ("tst.b D0b / bpl" before each one).
 *
 * The counter names below are Apollo's own (bead source-1a5o).  The kernel
 * never names them - RING_$GET_STATS just copies the block out - so they were
 * recovered from a user-space consumer, /etc/netmain in the SR10.4
 * distribution, whose "Error counts for <node>" display formats the ASKNODE
 * ring statistics record field by field:
 *
 *   rcvxerr  rcvhcsum  xmit bph  rcv bph   xmit esb
 *   rcvbus   rcvmodem  rcvpkt    rcvovr    rcvapar
 *   xmit_tim rcvcnt    rcveor    rcvcrc    rcvtim
 *   ...
 *
 * That names exactly eleven receive fields - one long (rcvcnt) and ten words -
 * which is exactly what 0x1C..0x33 holds.  Three of them are pinned directly
 * by the image and the rest follow from the order:
 *
 *   +0x1C rcvcnt   - the only long, bumped once per accepted packet
 *                    ("addq.l #1,(0x1c,A3)" at 0x00E75EEC)
 *   +0x26 rcvbus   - the bit-6 arm logs status 0x00110013, "receive bus
 *                    error", and CRASH_SYSTEMs (0x00E75FB4-0x00E75FBE)
 *   +0x32 rcvhcsum - the header-checksum arm logs 0x00110010, "bad checksum"
 *                    (0x00E75ED8-0x00E75EE2)
 *
 * Reading netmain's rows in the order (rcvcnt, rcveor, rcvcrc, rcvtim),
 * (rcvbus, rcvmodem, rcvpkt, rcvovr, rcvapar), (rcvxerr, rcvhcsum) lands
 * rcvbus on +0x26 and rcvhcsum on +0x32 - both anchors - and leaves +0x2C
 * (rcvovr, a DMA overrun) as the one word the CSR decode never touches, which
 * is consistent with the swdiag mirror also skipping its +0x12.
 *
 * The TRANSMIT half at 0x00..0x1B is named from the same binary (bead
 * source-11rf).  Two independent orderings in /etc/netmain agree, and one
 * counter is pinned outright by the image:
 *
 *  1. The "Error counts for <node>" display block lists, in row order,
 *       xmit_call  xmitcnt  xmit_nack  xmit_wack  xmit_orun
 *       xmit_apar  xmit_bus xmit_nortn xmit_modem xmit_error
 *       xmit_tim   rcvcnt   rcveor     rcvcrc     rcvtim
 *     -- so xmit_tim is the word immediately before the already-proven
 *     rcvcnt at +0x1C, which fixes the whole run backwards from +0x1A.
 *  2. netmain's counter-selection menu carries explicit selector keys, and
 *     they run strictly descending over exactly these labels:
 *       9 No acknowledge, 8 Wait acknowledge, 7 Xmit over run,
 *       6 Xmit ack parity, 5 Xmit bus error, 4 Xmit no return,
 *       3 Xmit modem error, 2 Xmit packet error, 1 Xmit time out
 *     which is the same nine words in the same order.
 *  3. The anchor: netmain's help text for "Transmit modem error" reads
 *     "Counts the number of times the transmitter could not synchronize
 *     properly with the network, resulting in an Xmit ESB or biphase error".
 *     RING_$SENDP bumps +0x16 on exactly that condition -- the arm gated by
 *     `andi.w #0xc00,D0w` (the two ESB/biphase status bits) at 0x00E75C5E,
 *     which also bumps the standalone RING_$XMIT_BIPHASE / RING_$XMIT_ESB
 *     words (0x00E75C70 / 0x00E75C82) before `addq.w #0x1,(0x16,A2)` at
 *     0x00E75C88.  Both orderings independently place xmit_modem at +0x16.
 *
 * Every transmit counter below therefore carries the RING_$SENDP instruction
 * that bumps it, with A2 = RING_$STATS[unit] (0x00E7594C-0x00E75954).
 *
 * The three fields netmain displays that are NOT in this record --
 * "xmit bph", "rcv bph" and "xmit esb" -- are the standalone words
 * RING_$XMIT_BIPHASE (0x00E261BC), RING_$RCV_BIPHASE (0x00E261B8) and
 * RING_$XMIT_ESB (0x00E261BE); ASKNODE assembles them into the reply.  That
 * is why netmain shows 25 counters where the record holds 22.
 */
typedef struct ring_$stats_t {
    uint16_t    _reserved0;         /* 0x00: never read or written by the
                                     *       kernel; netmain does not display it */
    uint32_t    xmit_call;          /* 0x02: RING_$SENDP calls; bumped once on
                                     *       entry, 0x00E759CE `addq.l #0x1,(0x2,A2)` */
    uint32_t    xmitcnt;            /* 0x06: successful sends; 0x00E75C42 (status
                                     *       word == 0x14) and 0x00E75D66 (the
                                     *       "accepted" and short-packet arms) */
    uint16_t    xmit_nack;          /* 0x0A: "No acknowledge" - 0x00E75D72, the
                                     *       arm reached when status bit 4 is
                                     *       clear and the length is 0 or > 4 */
    uint16_t    xmit_wack;          /* 0x0C: "Wait acknowledge" - status bit 1,
                                     *       0x00E75D3E */
    uint16_t    xmit_orun;          /* 0x0E: "Xmit over run" - status bit 0,
                                     *       0x00E75CDC */
    uint16_t    xmit_apar;          /* 0x10: "Xmit ack parity" - status bit 15,
                                     *       0x00E75D02 */
    uint16_t    xmit_bus;           /* 0x12: "Xmit bus error" - status bit 6,
                                     *       0x00E75CCA (the other arm of the
                                     *       same test reports 0x00110016,
                                     *       "memory parity error during
                                     *       transmit", instead of counting) */
    uint16_t    xmit_nortn;         /* 0x14: "Xmit no return" - status bits 5|9
                                     *       (`andi.w #0x220`), 0x00E75CF0 */
    uint16_t    xmit_modem;         /* 0x16: "Xmit modem error" - status bits
                                     *       10|11 (`andi.w #0xc00`), 0x00E75C88;
                                     *       the anchor for this whole half */
    uint16_t    xmit_error;         /* 0x18: "Xmit packet error" - status bits
                                     *       3 and 4 both set, 0x00E75D22 */
    uint16_t    xmit_tim;           /* 0x1A: "Xmit time out" - bumped on the
                                     *       retransmit re-arm, 0x00E75BE0 */
    uint32_t    rcvcnt;             /* 0x1C: packets accepted (0x00E75EEC) */
    uint16_t    rcveor;             /* 0x20: rcv_csr bit 5  (0x00E75FCE) */
    uint16_t    rcvcrc;             /* 0x22: rcv_csr bit 8  (0x00E75FFC) */
    uint16_t    rcvtim;             /* 0x24: rcv_csr bit 9  (0x00E75F98) */
    uint16_t    rcvbus;             /* 0x26: rcv_csr bit 6, status 0x00110013
                                     *       "receive bus error" (0x00E75FBE) */
    uint16_t    rcvmodem;           /* 0x28: rcv_csr bit 3  (0x00E76026) */
    uint16_t    rcvpkt;             /* 0x2A: rcv_csr bit 10 or 11; the split is
                                     *       counted separately in RING_$RCV_ESB /
                                     *       RING_$RCV_BIPHASE (0x00E75F7C) */
    uint16_t    rcvovr;             /* 0x2C: DMA overrun; not a CSR condition,
                                     *       so the receive decode never bumps it */
    uint16_t    rcvapar;            /* 0x2E: rcv_csr bit 0  (0x00E75FE4) */
    uint16_t    rcvxerr;            /* 0x30: rcv_csr bit 7  (0x00E7600E) */
    uint16_t    rcvhcsum;           /* 0x32: header checksum mismatch, status
                                     *       0x00110010 "bad checksum" (0x00E75EE2) */
    int8_t      last_success;       /* 0x34: Last transmission succeeded */
    int8_t      _reserved2;         /* 0x35 */
    int8_t      congestion_flag;    /* 0x36: Network congestion */
    int8_t      _reserved3;         /* 0x37 */
    int8_t      biphase_flag;       /* 0x38: Biphase error flag */
    int8_t      _reserved4;         /* 0x39 */
    int8_t      retry_pending;      /* 0x3A: Retry is pending */
    int8_t      _reserved5;         /* 0x3B */
} ring_$stats_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(ring_$stats_t, xmit_call)          == 0x02, "ring_$stats_t.xmit_call");
_Static_assert(offsetof(ring_$stats_t, xmitcnt)            == 0x06, "ring_$stats_t.xmitcnt");
_Static_assert(offsetof(ring_$stats_t, xmit_nack)          == 0x0A, "ring_$stats_t.xmit_nack");
_Static_assert(offsetof(ring_$stats_t, xmit_wack)          == 0x0C, "ring_$stats_t.xmit_wack");
_Static_assert(offsetof(ring_$stats_t, xmit_orun)          == 0x0E, "ring_$stats_t.xmit_orun");
_Static_assert(offsetof(ring_$stats_t, xmit_apar)          == 0x10, "ring_$stats_t.xmit_apar");
_Static_assert(offsetof(ring_$stats_t, xmit_bus)           == 0x12, "ring_$stats_t.xmit_bus");
_Static_assert(offsetof(ring_$stats_t, xmit_nortn)         == 0x14, "ring_$stats_t.xmit_nortn");
_Static_assert(offsetof(ring_$stats_t, xmit_modem)         == 0x16, "ring_$stats_t.xmit_modem");
_Static_assert(offsetof(ring_$stats_t, xmit_error)         == 0x18, "ring_$stats_t.xmit_error");
_Static_assert(offsetof(ring_$stats_t, xmit_tim)           == 0x1A, "ring_$stats_t.xmit_tim");
_Static_assert(offsetof(ring_$stats_t, rcvcnt)             == 0x1C, "ring_$stats_t.rcvcnt");
_Static_assert(offsetof(ring_$stats_t, rcveor)             == 0x20, "ring_$stats_t.rcveor");
_Static_assert(offsetof(ring_$stats_t, rcvbus)             == 0x26, "ring_$stats_t.rcvbus");
_Static_assert(offsetof(ring_$stats_t, rcvovr)             == 0x2C, "ring_$stats_t.rcvovr");
_Static_assert(offsetof(ring_$stats_t, rcvpkt)             == 0x2A, "ring_$stats_t.rcvpkt");
_Static_assert(offsetof(ring_$stats_t, rcvhcsum)           == 0x32, "ring_$stats_t.rcvhcsum");
_Static_assert(offsetof(ring_$stats_t, congestion_flag)    == 0x36, "ring_$stats_t.congestion_flag");
_Static_assert(sizeof(ring_$stats_t)                       == RING_STATS_SIZE, "sizeof ring_$stats_t");
_Static_assert(offsetof(ring_$swdiag_t, rcveor)            == 0x06, "ring_$swdiag_t.rcveor");
_Static_assert(offsetof(ring_$swdiag_t, rcvpkt)            == 0x10, "ring_$swdiag_t.rcvpkt");
_Static_assert(offsetof(ring_$swdiag_t, rcvxerr)           == 0x16, "ring_$swdiag_t.rcvxerr");
#endif /* ARCH_M68K */

/*
 * ============================================================================
 * Public Functions
 * ============================================================================
 */

/*
 * RING_$INIT - Initialize a ring unit
 *
 * Initializes the ring unit data structures, event counts, and exclusion
 * locks. Creates a network I/O port for the unit.
 *
 * @param device_info   Device information structure (from DCTE)
 *
 * @return status_$ok on success, error code on failure
 *
 * Original address: 0x00E2FAE0
 */
status_$t RING_$INIT(void *device_info);

/*
 * RING_$GET_ID - Get the network ID for a ring unit
 *
 * Retrieves the 24-bit network ID from the device's hardware address.
 * The ID is constructed from bytes at offsets 0x12, 0x14, and 0x16
 * in the device initialization data.
 *
 * @param param   Pointer to unit info
 *
 * @return Network ID (24-bit value)
 *
 * Original address: 0x00E2FA28
 */
uint32_t RING_$GET_ID(void *param);

/*
 * RING_$INT - Interrupt handler for ring unit
 *
 * Handles receive interrupts from the token ring hardware.
 * Advances event counts to wake waiting processes.
 *
 * @param device_info   Device information structure
 *
 * @return 0xFF (interrupt handled)
 *
 * Original address: 0x00E75748
 */
int8_t RING_$INT(void *device_info);

/*
 * RING_$START - Start a ring unit
 *
 * Activates a ring unit for transmission and reception.
 * Must be called after RING_$INIT.
 *
 * @param unit_ptr      Pointer to unit number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E76830
 */
void RING_$START(uint16_t *unit_ptr, status_$t *status_ret);

/*
 * RING_$STOP - Stop a ring unit
 *
 * Deactivates a ring unit. The unit can be restarted with RING_$START.
 *
 * @param unit_ptr      Pointer to unit number
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E769C4
 */
void RING_$STOP(uint16_t *unit_ptr, status_$t *status_ret);

/*
 * RING_$SENDP - Send a packet on the ring
 *
 * Transmits a packet over the token ring network. Handles retries,
 * timeout management, and error reporting.
 *
 * @param unit_ptr      Pointer to unit number
 * @param hdr_pa        Header physical address
 * @param hdr_va        Header virtual address
 * @param data_info     Data buffer info (PA in high 32 bits, VA in low)
 * @param data_len      Data length
 * @param send_flags    Send flags output
 * @param result_flags  Result flags output
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E75916
 */
void RING_$SENDP(uint16_t *unit_ptr, uint32_t hdr_pa, void *hdr_va,
                 uint64_t data_info, uint16_t data_len,
                 uint16_t *send_flags, uint16_t *result_flags,
                 status_$t *status_ret);

/*
 * RING_$GET_STATS - Get statistics for a ring unit
 *
 * Copies the per-unit statistics to the provided buffer.
 *
 * @param unit_ptr      Pointer to unit number
 * @param stats_buf     Output buffer for statistics (0x3C bytes)
 * @param unused        Unused parameter
 * @param len_out       Output: bytes copied
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E76950
 */
void RING_$GET_STATS(uint16_t *unit_ptr, void *stats_buf, uint16_t unused,
                     uint16_t *len_out, status_$t *status_ret);

/*
 * RING_$IOCTL - I/O control for ring unit
 *
 * Performs I/O control operations on a ring unit.
 * Currently supports setting the transmit mask.
 *
 * @param unit_ptr      Pointer to unit number
 * @param cmd           Command (0 = set tmask)
 * @param param         Command parameter
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E76B2C
 */
void RING_$IOCTL(uint16_t *unit_ptr, int16_t *cmd, void *param,
                 status_$t *status_ret);

/*
 * RING_$SET_TMASK - Set transmit mask
 *
 * Sets the transmit mask register for a ring unit.
 *
 * @param unit          Unit number
 * @param mask          Transmit mask value
 *
 * Original address: 0x00E768E8
 */
void RING_$SET_TMASK(uint16_t unit, uint16_t mask);

/*
 * RING_$KICK_DRIVER - Kick the ring driver
 *
 * Forces the driver to re-check for pending work.
 *
 * Original address: 0x00E768A8
 */
void RING_$KICK_DRIVER(void);

/*
 * ============================================================================
 * Service Functions (called via NET_IO dispatch)
 * ============================================================================
 */

/*
 * RING_$SVC_OPEN - Open a ring channel (service call)
 *
 * @param name          Channel name
 * @param args          Open arguments
 * @param unused        Unused
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E76DF2
 */
void RING_$SVC_OPEN(void *name, void *args, void *unused, status_$t *status_ret);

/*
 * RING_$SVC_CLOSE - Close a ring channel (service call)
 *
 * @param unit_ptr      Pointer to unit number
 * @param args          Close arguments
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E76E22
 */
void RING_$SVC_CLOSE(uint16_t *unit_ptr, void *args, status_$t *status_ret);

/*
 * RING_$SVC_READ - Read from a ring channel (service call)
 *
 * @param unit_ptr      Pointer to unit number
 * @param result        Result buffer
 * @param param3        Additional parameter
 * @param param4        Additional parameter
 * @param param5        Additional parameter
 * @param len_out       Output: bytes read
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E77402
 */
void RING_$SVC_READ(uint16_t *unit_ptr, void *result, void *param3,
                    void *param4, uint16_t param5, int16_t *len_out,
                    status_$t *status_ret);

/*
 * RING_$SVC_WRITE - Write to a ring channel (service call)
 *
 * @param unit_ptr      Pointer to unit number
 * @param hdr           Packet header
 * @param param3        Additional parameter
 * @param data          Data buffer
 * @param data_len      Data length
 * @param result        Result output
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E76F9E
 */
void RING_$SVC_WRITE(uint16_t *unit_ptr, void *hdr, void *param3,
                     void *data, int16_t data_len, uint16_t *result,
                     status_$t *status_ret);

/*
 * RING_$SVC_IOCTL - IOCTL for ring channel (service call)
 *
 * @param unit_ptr      Pointer to unit number
 * @param cmd_args      Command and arguments
 * @param param3        Additional parameter
 * @param param4        Additional parameter
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E776B8
 */
void RING_$SVC_IOCTL(uint16_t *unit_ptr, void *cmd_args, void *param3,
                     void *param4, status_$t *status_ret);

/*
 * ============================================================================
 * OS-level Functions (called by network manager)
 * ============================================================================
 */

/*
 * RING_$OPEN_OS - Open ring for OS use
 *
 * @param param1        Parameter 1
 * @param args          Arguments
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E77BA0
 */
void RING_$OPEN_OS(uint16_t param1, void *args, status_$t *status_ret);

/*
 * RING_$CLOSE_OS - Close ring OS use
 *
 * @param param1        Parameter 1
 * @param args          Arguments
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E77C24
 */
void RING_$CLOSE_OS(uint16_t param1, void *args, status_$t *status_ret);

/*
 * RING_$SEND_OS - Send via ring for OS
 *
 * @param param1        Parameter 1
 * @param param2        Parameter 2
 * @param param3        Parameter 3
 * @param param4        Parameter 4
 * @param param5        Parameter 5
 * @param param6        Parameter 6
 * @param param7        Parameter 7
 * @param status_ret    Output: status code
 *
 * Original address: 0x00E77C60
 */
void RING_$SEND_OS(void *param1, void *param2, void *param3, void *param4,
                   void *param5, void *param6, void *param7,
                   status_$t *status_ret);

/*
 * ============================================================================
 * Receive Functions
 * ============================================================================
 */

/*
 * RING_$RCV0 - Receive daemon for unit 0
 *
 * Main receive loop for ring unit 0. Runs as a separate process.
 *
 * Original address: 0x00E76642
 */
void RING_$RCV0(void);

/*
 * RING_$RCV1 - Receive daemon for unit 1
 *
 * Main receive loop for ring unit 1. Runs as a separate process.
 *
 * Original address: 0x00E7665E
 */
void RING_$RCV1(void);

/*
 * RING_$RCV_FROM_UNIT_PRIV - Privileged receive loop
 *
 * Internal receive loop implementation for a specific unit.
 *
 * @param unit          Unit number
 *
 * Original address: 0x00E76048
 */
void RING_$RCV_FROM_UNIT_PRIV(uint16_t unit);

/*
 * RING_$POLL_STICKY_BPHERR - Poll for sticky biphase errors
 *
 * Checks for persistent biphase errors on the ring.
 *
 * @param param1        Parameter 1
 * @param param2        Parameter 2
 *
 * Original address: 0x00E76290
 */
void RING_$POLL_STICKY_BPHERR(void *param1, void *param2);

/*
 * RING_$PROC2_CLEANUP - Process cleanup handler
 *
 * Called when a process using ring sockets terminates.
 *
 * @param param1        Parameter 1
 *
 * Original address: 0x00E76A42
 */
void RING_$PROC2_CLEANUP(void *param1);

/*
 * Ring receive overflow counters (moved here from app/app_internal.h --
 * bead source-3uo).
 */
#if defined(ARCH_M68K)
#define RING_$FILE_OVERFLOW     (*(uint16_t *)0xE24596)
#define RING_$OVERFLOW_OVERFLOW (*(uint16_t *)0xE24594)
#else
extern uint16_t RING_$FILE_OVERFLOW;
extern uint16_t RING_$OVERFLOW_OVERFLOW;
#endif

#endif /* RING_H */
