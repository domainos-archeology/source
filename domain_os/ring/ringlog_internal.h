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
 * Ring Log Entry Structure (stride 0x2E = 46 bytes)
 *
 * RINGLOG_$LOGIT reaches an entry as RINGLOG_$DATA + 0x2E * index
 * (0x00E1A2E4), so entry 0 overlaps the index word and the struct below is
 * laid over a byte pool rather than being an array element - its C sizeof is
 * 0x30 because the 13-word packet copy at 0x00E1A358 runs from +0x16 to
 * +0x2E inclusive, two bytes into the next entry.
 * ============================================================================
 */
typedef struct __attribute__((packed)) ringlog_$entry_t {
    /*
     * 0x00: never written by this entry.  For entry 0 this IS the buffer's
     * index word; for entry n > 0 it is where entry n-1's last packet word
     * landed.
     */
    uint16_t    shared_head;            /* 0x00 */

    /*
     * 0x02 / 0x03: two bytes taken out of the packet record.
     *   receive (0x00E1A3A0/0x00E1A3A6): +0x03 <- pkt[0x39], +0x02 <- pkt[0x45]
     *   send    (0x00E1A3E6/0x00E1A3F4): +0x02 <- pkt[0x1B],
     *                                    +0x03 <- pkt[0x1F + 2*pkt[0x19]]
     */
    uint8_t     sock_byte_02;           /* 0x02 */
    uint8_t     sock_byte_03;           /* 0x03 */

    /*
     * 0x04..0x0B: three 20-bit node ids and the flag nibble, packed into
     * eight bytes.  Each is written with a masked longword read-modify-write
     * at an odd word offset, so the three views overlap:
     *
     *   0x00E1A390  andi.l #0xfff,(0x4,A2)      keep bits 11..0  of long@0x04
     *   0x00E1A39C  or.l   D5,(0x4,A2)          D5 = value << 12
     *        -> node_a occupies 0x04.b7 .. 0x06.b4  (20 bits)
     *
     *   0x00E1A378  andi.l #-0xfffff01,(0x6,A2) keep 0xF00000FF of long@0x06
     *   0x00E1A382  or.l   D5,(0x6,A2)          D5 = value << 8
     *        -> node_b occupies 0x06.b3 .. 0x08.b0  (20 bits)
     *
     *   0x00E1A320  andi.l #-0xfffff1,(0x8,A2)  keep 0xFF00000F of long@0x08
     *   0x00E1A32E  or.l   D1,(0x8,A2)          D1 = pkt[0x00] << 4
     *        -> node_c occupies 0x09      .. 0x0B.b4 (20 bits)
     *
     * and the flags are what 0xFF00000F leaves of byte 0x0B: bits 3..0.
     * Only the low 20 bits of each source value survive the shift, so the
     * accessors below mask to 20 bits.
     *
     * The pool is spelled as bytes because the three longword views cannot be
     * separate C members, and because RINGLOG_$CNTL hands the whole buffer to
     * user space verbatim: the packing must keep the m68k byte order on any
     * host.  Use the ringlog_$get_packed / ringlog_$put_packed helpers.
     */
    uint8_t     packed[8];              /* 0x04..0x0B */

    /*
     * 0x0C / 0x10: two longwords copied straight out of the packet on the
     * receive path (0x00E1A3B2 <- pkt+0x3A, 0x00E1A3AC <- pkt+0x2E) and
     * cleared on the send path (0x00E1A3BA / 0x00E1A3BE).
     */
    uint32_t    field_0c;               /* 0x0C */
    uint32_t    field_10;               /* 0x10 */

    /* 0x14: pkt+0x16 (0x00E1A332) */
    uint16_t    pkt_type;               /* 0x14 */

    /*
     * 0x16: thirteen words copied out of the packet starting at
     * 2 * ((pkt[0x18] + 0x1E) >> 1) (0x00E1A338-0x00E1A362).  The last of the
     * thirteen lands at entry + 0x2E, i.e. in the next entry's shared_head.
     */
    uint16_t    pkt_words[13];          /* 0x16..0x2F */
} ringlog_$entry_t;

_Static_assert(offsetof(ringlog_$entry_t, sock_byte_02) == 0x02, "ringlog entry 0x02");
_Static_assert(offsetof(ringlog_$entry_t, sock_byte_03) == 0x03, "ringlog entry 0x03");
_Static_assert(offsetof(ringlog_$entry_t, packed)       == 0x04, "ringlog entry 0x04");
_Static_assert(offsetof(ringlog_$entry_t, field_0c)     == 0x0C, "ringlog entry 0x0C");
_Static_assert(offsetof(ringlog_$entry_t, field_10)     == 0x10, "ringlog entry 0x10");
_Static_assert(offsetof(ringlog_$entry_t, pkt_type)     == 0x14, "ringlog entry 0x14");
_Static_assert(offsetof(ringlog_$entry_t, pkt_words)    == 0x16, "ringlog entry 0x16");
_Static_assert(sizeof(ringlog_$entry_t) == 0x30,
               "ringlog_$entry_t spans 0x30 bytes at a 0x2E stride");

/* Byte offsets, within ringlog_$entry_t.packed[], of the three longword views */
#define RINGLOG_PACKED_OFF_04   0       /* the longword at entry + 0x04 */
#define RINGLOG_PACKED_OFF_06   2       /* the longword at entry + 0x06 */
#define RINGLOG_PACKED_OFF_08   4       /* the longword at entry + 0x08 */

/*
 * Big-endian longword accessors for ringlog_$entry_t.packed.  The image does
 * these as plain move.l/andi.l/or.l on an m68k; spelling them out in bytes
 * keeps the stored bit pattern identical on a little-endian host.
 */
static inline uint32_t ringlog_$get_packed(const ringlog_$entry_t *entry, int off)
{
    const uint8_t *p = &entry->packed[off];

    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static inline void ringlog_$put_packed(ringlog_$entry_t *entry, int off, uint32_t v)
{
    uint8_t *p = &entry->packed[off];

    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/*
 * ============================================================================
 * RINGLOG_$DATA - the ring log buffer (0x00EA3E38, 0x11FC bytes)
 *
 * Map "D53 EA3E38 RINGLOG_$DATA loaded at 1B2152, size = 11FC", in the
 * trailing data region after FILE_$LOT_DATA; a MODULE_DATA block linked in
 * the map's order.  RINGLOG_$LOGIT and RINGLOG_$CNTL address it through its
 * literal base ("movea.l #0xea3e38,A0" at 0x00E1A2E4 and 0x00E7229A).
 *
 * A byte pool: the entry stride 0x2E is an explicit constant in the code and
 * entry 0 overlaps the index word, so there is no C array of entries.
 * Pointer-free.
 * ============================================================================
 */
typedef struct ringlog_$data_t {
    uint8_t     bytes[RINGLOG_DATA_SIZE];
} ringlog_$data_t;

_Static_assert(sizeof(ringlog_$data_t) == 0x11FC,
               "RINGLOG_$DATA is 0x11FC bytes (SAU2 map D53 EA3E38)");

MODULE_DATA_DECLARE(ringlog_$data_t, RINGLOG_$DATA, 0x00EA3E38);

/*
 * The next-entry index: the word at RINGLOG_$DATA + 0 (0x00E1A2B6,
 * 0x00E7228E).  Reached through the byte pool so it stays the same object as
 * entry 0's shared_head.
 */
static inline int16_t ringlog_$get_index(void)
{
    const uint8_t *p = RINGLOG_$DATA.bytes;

    return (int16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static inline void ringlog_$set_index(int16_t v)
{
    uint8_t *p = RINGLOG_$DATA.bytes;

    p[0] = (uint8_t)((uint16_t)v >> 8);
    p[1] = (uint8_t)v;
}

/* Entry n, at RINGLOG_$DATA + 0x2E * n */
static inline ringlog_$entry_t *ringlog_$entry(int16_t index)
{
    return (ringlog_$entry_t *)&RINGLOG_$DATA.bytes[(int32_t)index * RINGLOG_ENTRY_SIZE];
}

/*
 * Native-order accessors for the packet record RINGLOG_$LOGIT is handed.
 * That record is built by other kernel code, so its fields are ordinary
 * machine words at byte displacements; only the LOG entry is a packed
 * big-endian object.  memcpy keeps the odd-word longwords (pkt + 0x2E,
 * pkt + 0x3A) legal on hosts that require alignment.
 */
static inline uint32_t ringlog_$pkt_long(const uint8_t *pkt, uint16_t off)
{
    uint32_t v;

    __builtin_memcpy(&v, pkt + off, sizeof v);
    return v;
}

static inline uint16_t ringlog_$pkt_uword(const uint8_t *pkt, uint16_t off)
{
    uint16_t v;

    __builtin_memcpy(&v, pkt + off, sizeof v);
    return v;
}

static inline int16_t ringlog_$pkt_word(const uint8_t *pkt, uint16_t off)
{
    return (int16_t)ringlog_$pkt_uword(pkt, off);
}

/*
 * The wire area MST_$WIRE_AREA pins for the log (RINGLOG_$CNTL 0x00E722BE):
 * RINGLOG_$DATA up to RINGLOG_BUFFER_SIZE (0x11FA) bytes in; the end is the
 * local at (-0x8,A6), the image's literal 0xEA3E38 + 0x11FA.
 */

#endif /* RINGLOG_INTERNAL_H */
