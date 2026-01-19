/*
 * ring/ringlog_internal.h - Internal Ring Log Definitions
 *
 * Contains internal data structures and types used only within
 * the ring logging subsystem.
 */

#ifndef RINGLOG_INTERNAL_H
#define RINGLOG_INTERNAL_H

#include "ring/ringlog.h"
#include "ml/ml.h"
#include "mst/mst.h"
#include "wp/wp.h"

/*
 * ============================================================================
 * Data Structure Addresses (m68k)
 * ============================================================================
 */

/* Ring log control data base address */
#define RINGLOG_CTL_BASE        0xE2C32C

/* Ring log buffer base address */
#define RINGLOG_BUF_BASE        0xEA3E38

/*
 * ============================================================================
 * Ring Log Entry Structure (0x2E = 46 bytes)
 *
 * Each entry records information about a single packet event.
 * The structure packs multiple fields into bit fields for efficiency.
 * ============================================================================
 */
typedef struct __attribute__((packed)) ringlog_entry_t {
    /*
     * Bytes 0x00-0x01: Reserved/padding
     */
    uint8_t     _reserved0[2];          /* 0x00 */

    /*
     * Byte 0x02: Source/dest socket or packet info byte 1
     * For receives: byte from packet offset 0x45
     * For sends: byte from packet offset 0x1b
     */
    uint8_t     sock_byte1;             /* 0x02 */

    /*
     * Byte 0x03: Source/dest socket or packet info byte 2
     * For receives: byte from packet offset 0x39
     * For sends: byte from packet[0x19]*2 + 0x1f
     */
    uint8_t     sock_byte2;             /* 0x03 */

    /*
     * Bytes 0x04-0x07: Packed 24-bit network ID (shifted << 12)
     * Low 12 bits are preserved from previous value
     * For receives: from packet offset 0x40 (24-bit)
     * For sends: from packet offset 0x08 (24-bit)
     */
    uint32_t    remote_network_id;      /* 0x04 */

    /*
     * Bytes 0x08-0x0B: Packed network ID and flags
     * - Bits 31-4: Network ID from packet[0] << 4
     * - Bits 3-0: preserved from previous (likely entry index or flags)
     *
     * Byte 0x0B contains flags:
     * - Bit 3 (0x08): Inbound flag (from header_info[0] bit 7)
     * - Bit 2 (0x04): Valid entry flag (always set when logged)
     * - Bit 1 (0x02): Send flag (1 = send, 0 = receive)
     */
    uint32_t    local_network_id_flags; /* 0x08 */

    /*
     * Bytes 0x0C-0x0F: Additional packet info
     * For receives: from packet offset 0x3a (4 bytes)
     * For sends: zeroed
     */
    uint32_t    field_0c;               /* 0x0C */

    /*
     * Bytes 0x10-0x13: Additional packet info
     * For receives: from packet offset 0x2e (4 bytes)
     * For sends: zeroed
     */
    uint32_t    field_10;               /* 0x10 */

    /*
     * Bytes 0x14-0x15: Packet type
     * From packet offset 0x16
     */
    uint16_t    packet_type;            /* 0x14 */

    /*
     * Bytes 0x16-0x2D: Packet data (24 bytes = 12 words)
     * Copied from packet starting at calculated offset based on
     * packet header length field
     */
    uint8_t     packet_data[24];        /* 0x16 */

} ringlog_entry_t;

/*
 * Verify structure size at compile time
 */
_Static_assert(sizeof(ringlog_entry_t) == RINGLOG_ENTRY_SIZE,
               "ringlog_entry_t size mismatch");

/*
 * ============================================================================
 * Ring Log Buffer Structure
 *
 * Located at 0xEA3E38 on original platform.
 * Contains the current index followed by the entry array.
 * ============================================================================
 */
typedef struct ringlog_buffer_t {
    int16_t             current_index;              /* 0x00: Next entry to write (0-99) */
    ringlog_entry_t     entries[RINGLOG_MAX_ENTRIES]; /* 0x02: Entry array */
} ringlog_buffer_t;

/*
 * ============================================================================
 * Ring Log Control Structure
 *
 * Contains configuration and state for the logging subsystem.
 * Located at base 0xE2C32C on original platform.
 * ============================================================================
 */
typedef struct ringlog_ctl_t {
    /*
     * Wired page addresses for the ring buffer.
     * Up to some number of pages can be wired to keep buffer in physical memory.
     * Index 0 is unused; entries 1..wire_count contain wired addresses.
     */
    uint32_t    wired_pages[10];        /* 0x00: Wired page addresses (indices 1-9 used) */

    /*
     * Spinlock for buffer access.
     * Protects current_index and entry writes.
     */
    uint32_t    spinlock;               /* 0x28: Spinlock (at 0xE2C354) */

    /*
     * Network ID filter.
     * If non-zero, only packets matching this network ID are logged.
     */
    uint32_t    filter_id;              /* 0x2C: RINGLOG_$ID filter (at 0xE2C358) */

    /*
     * Number of wired pages.
     * Pages wired_pages[1] through wired_pages[wire_count] are wired.
     */
    int16_t     wire_count;             /* 0x30: Number of wired pages (at 0xE2C35C) */

    /*
     * Socket type filters.
     * When >= 0, packets to/from that socket type are NOT logged.
     * When < 0, filtering for that socket type is disabled.
     */
    int8_t      mbx_sock_filter;        /* 0x32: MBX socket filter (at 0xE2C35E) */
    int8_t      _pad1;
    int8_t      who_sock_filter;        /* 0x34: WHO socket filter (at 0xE2C360) */
    int8_t      _pad2;
    int8_t      nil_sock_filter;        /* 0x36: NIL socket filter (at 0xE2C362) */
    int8_t      _pad3;

    /*
     * Logging active flag.
     * -1 (0xFF) = logging is active
     * 0 = logging is stopped
     */
    int8_t      logging_active;         /* 0x38: RING_$LOGGING_NOW (at 0xE2C364) */
    int8_t      _pad4;

    /*
     * First entry flag.
     * Set to -1 when buffer wraps or is cleared; reset after first entry.
     * Used to detect if buffer has wrapped.
     */
    int8_t      first_entry_flag;       /* 0x3A: (at 0xE2C366) */

} ringlog_ctl_t;

/*
 * ============================================================================
 * Global Data Declarations
 * ============================================================================
 */

/*
 * Ring log control structure.
 * On m68k, located at 0xE2C32C.
 */
extern ringlog_ctl_t RINGLOG_$CTL;

/*
 * Ring log buffer.
 * On m68k, located at 0xEA3E38.
 */
extern ringlog_buffer_t RINGLOG_$BUF;

/*
 * Convenience aliases for common fields
 */
#define RINGLOG_$ID             (RINGLOG_$CTL.filter_id)
#define RINGLOG_$NIL_SOCK       (RINGLOG_$CTL.nil_sock_filter)
#define RINGLOG_$WHO_SOCK       (RINGLOG_$CTL.who_sock_filter)
#define RINGLOG_$MBX_SOCK       (RINGLOG_$CTL.mbx_sock_filter)
#define RING_$LOGGING_NOW       (RINGLOG_$CTL.logging_active)

/*
 * ============================================================================
 * Wire area parameters (passed to MST_$WIRE_AREA)
 *
 * These are used to wire the ring buffer memory to prevent paging.
 * ============================================================================
 */

/* Buffer end address for wiring */
#define RINGLOG_WIRE_END        (RINGLOG_BUF_BASE + RINGLOG_BUFFER_SIZE)

#endif /* RINGLOG_INTERNAL_H */
