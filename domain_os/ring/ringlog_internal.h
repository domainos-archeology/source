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

/* Remaining documented offsets (bead source-pewa). */
_Static_assert(__builtin_offsetof(ringlog_entry_t, _reserved0) == 0x00, "ringlog_entry_t._reserved0");
_Static_assert(__builtin_offsetof(ringlog_entry_t, sock_byte1) == 0x02, "ringlog_entry_t.sock_byte1");
_Static_assert(__builtin_offsetof(ringlog_entry_t, sock_byte2) == 0x03, "ringlog_entry_t.sock_byte2");
_Static_assert(__builtin_offsetof(ringlog_entry_t, remote_network_id) == 0x04, "ringlog_entry_t.remote_network_id");
_Static_assert(__builtin_offsetof(ringlog_entry_t, local_network_id_flags) == 0x08, "ringlog_entry_t.local_network_id_flags");
_Static_assert(__builtin_offsetof(ringlog_entry_t, field_0c) == 0x0C, "ringlog_entry_t.field_0c");
_Static_assert(__builtin_offsetof(ringlog_entry_t, field_10) == 0x10, "ringlog_entry_t.field_10");
_Static_assert(__builtin_offsetof(ringlog_entry_t, packet_type) == 0x14, "ringlog_entry_t.packet_type");
_Static_assert(__builtin_offsetof(ringlog_entry_t, packet_data) == 0x16, "ringlog_entry_t.packet_data");

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

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(ringlog_buffer_t, current_index) == 0x00, "ringlog_buffer_t.current_index");
_Static_assert(__builtin_offsetof(ringlog_buffer_t, entries) == 0x02, "ringlog_buffer_t.entries");

/* ringlog_ctl_t and RINGLOG_$CTL are declared in ring/ringlog.h so that
 * ROUTE_$PROCESS can test RING_$LOGGING_NOW (0x00E2C364) without reaching
 * into this internal header. */

/*
 * ============================================================================
 * Global Data Declarations
 * ============================================================================
 */

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
/* RING_$LOGGING_NOW is defined in ring/ringlog.h. */

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
