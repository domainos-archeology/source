/*
 * AREA Internal Header
 *
 * Internal declarations for the AREA subsystem.
 * This header should only be included by the .c files under area/.
 */

#ifndef AREA_INTERNAL_H
#define AREA_INTERNAL_H

#include "area/area.h"
#include "math/math.h"
#include "as/as.h"
#include "ast/ast.h"
#include "cal/cal.h"
#include "ml/ml.h"
#include "mmu/mmu.h"
#include "network/network.h"
#include "proc1/proc1.h"
#include "rem_file/rem_file.h"
#include "wp/wp.h"

/*
 * Error message for internal crashes
 */
extern status_$t Area_Internal_Error;

/*
 * area_$seg_slot_t - one four-byte cell of an area's segment map.
 *
 * area_$entry_t.seg_bitmap[] is two of these (entry+0x18 and entry+0x1C) and
 * the overflow tables an area_$seg_table_t owns are arrays of them; both
 * AREA_$COPY cursors step by four bytes (0x00E0936A/0x00E0936E
 * `addq.l #0x4`) and the overflow offset is scaled by four as well
 * (0x00E0921C `lsl.w #0x2,D0w`).
 *
 * The field offsets are area_$get_aste's (0x00E09A6A), which is handed one
 * of these cells as its second argument:
 *   +0x00  the eight "segment allocated" bits - 0x00E09B88 `move.b (A4),D0b`
 *          followed by `btst.l D3,D0` with D3 = seg_index & 7, and the same
 *          byte is what AREA_$COPY tests at 0x00E09284.
 *   +0x01  a state byte - 0x00E09A96 `lea (0x1,A1),A2` then 0x00E09AA0
 *          `btst.b #0x6,(A2)` (the "in transition" wait), and 0x00E09B06
 *          `bset.b #0x6,(0x1,A0)`.
 *   +0x02  the 1-based ASTE index - 0x00E09AAE `move.w (0x2,A0),D0w`, which
 *          0x00E09AB2-0x00E09AC0 scales by 0x14 into the ASTE table at
 *          0xEC5400.
 *
 * Naming the byte gives the same address on either endianness; casting the
 * longword to `uint8_t *` would not.
 */
typedef struct area_$seg_slot_t {
    uint8_t  bits;              /* 0x00: eight "segment allocated" flags */
    uint8_t  state;             /* 0x01: bit 6 = ASTE in transition */
    uint16_t aste_index;        /* 0x02: 1-based index into ASTE_BASE */
} area_$seg_slot_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(area_$seg_slot_t) == 4, "area_$seg_slot_t size");
_Static_assert(offsetof(area_$seg_slot_t, bits)       == 0x00, "seg_slot.bits");
_Static_assert(offsetof(area_$seg_slot_t, state)      == 0x01, "seg_slot.state");
_Static_assert(offsetof(area_$seg_slot_t, aste_index) == 0x02, "seg_slot.aste_index");
#endif

/*
 * Extended segment table entry for areas with > 16 segments
 * Used by AREA_$COPY and segment threading operations
 */
typedef struct area_$seg_table_t {
    int16_t area_id;            /* 0x00: Area ID */
    uint8_t table_index;        /* 0x02: Table index (0-255) */
    uint8_t pad;                /* 0x03: Padding */
    struct area_$seg_table_t *next;  /* 0x04: Next in ASID list */
    area_$seg_slot_t *bitmap_ptr;    /* 0x08: Pointer to the overflow slots */
} area_$seg_table_t;

/*
 * AREA per-ASID extended segment table list
 * Array at AREA_GLOBALS_BASE + 0x68, indexed by ASID
 */
#define AREA_SEG_TABLE_LIST_BASE    (AREA_GLOBALS_BASE + 0x68)

/*
 * area_$alloc_resources - Extend the area table
 *
 * Wires and maps storage for up to `count` further area_$entry_t records,
 * threads them onto AREA_$FREE_LIST, and updates AREA_$N_AREAS /
 * AREA_$N_FREE.  The request is clamped to the module maximum held at
 * globals + 0x5D6.
 *
 * @param count         Number of entries requested (0x60 from
 *                      area_$internal_create)
 *
 * Returns: a Domain boolean - true (0xFF, i.e. < 0) if the table grew,
 *          false (0) if it was already at its maximum.
 *
 * Original address: 0x00E075CA
 */
boolean area_$alloc_resources(int16_t count);

/*
 * area_$remote_sync - Sync with remote partner
 *
 * Synchronizes area state with remote partner node.
 *
 * Original address: 0x00E087BA
 */
void area_$remote_sync(void);

/*
 * area_$free_segments - Free segment range
 *
 * Frees segments within the specified range.
 *
 * @param area_id       Area ID
 * @param start_seg     Starting segment
 * @param end_seg       Ending segment
 * @param clear_bitmap  If true, clear bitmap entries
 * @param status_p      Output: status code
 *
 * Original address: 0x00E085A6
 */
void area_$free_segments(int16_t area_id, uint32_t start_seg,
                          uint32_t end_seg, int8_t clear_bitmap,
                          status_$t *status_p);

/*
 * area_$lookup_seg_table - Look up extended segment table
 *
 * @param asid          Address space ID
 * @param area_id       Area ID
 * @param table_idx     Table index
 *
 * Returns: Pointer to segment table entry, or NULL
 *
 * Original address: 0x00E09D2E
 */
area_$seg_table_t *area_$lookup_seg_table(int16_t asid, int16_t area_id,
                                           int16_t table_idx);

/*
 * area_$free_seg_table - Free extended segment table entry
 *
 * @param entry         Entry to free
 * @param prev          Previous entry in list
 * @param asid          Address space ID
 *
 * Original address: 0x00E09E48
 */
void area_$free_seg_table(area_$seg_table_t *entry,
                           area_$seg_table_t *prev, int16_t asid);

/*
 * area_$get_aste - Get the ASTE named by one segment-map slot
 *
 * Six Pascal parameters; the prologue at 0x00E09A72-0x00E09A86 reads
 *   A6+0x08 word  area_id
 *   A6+0x0A long  slot        the area_$seg_slot_t cell for this segment
 *   A6+0x0E word  seg_idx
 *   A6+0x10 byte  wait        if true (0x00E09A90 `tst.b D4b` / `bmi`) spin
 *                             on the slot's in-transition bit
 *   A6+0x12 byte  create      if false and no ASTE is mapped, fail with
 *                             0x00030004 (0x00E09AFA)
 *   A6+0x14 long  status
 * and the result is left in A0 (0x00E09AE4 `movea.l A0,A2` at the caller).
 *
 * Original address: 0x00E09A6A
 */
struct aste_t *area_$get_aste(int16_t area_id, area_$seg_slot_t *slot,
                              int16_t seg_idx, int8_t wait, int8_t create,
                              status_$t *status_p);

/*
 * area_$find_entry_by_uid - Locate the ASTE that backs one area page
 *
 * Four Pascal parameters (prologue 0x00E093A4-0x00E093B0):
 *   A6+0x08 word  area_id
 *   A6+0x0A long  bste_ptr   a VAR word - for a reversed area 0x00E093E8
 *                            rewrites it as (-1 - *bste_ptr)
 *   A6+0x0E word  page
 *   A6+0x10 long  status_p
 * The ASTE is returned in A0.
 *
 * Original address: 0x00E0939C
 */
struct aste_t *area_$find_entry_by_uid(int16_t area_id, uint16_t *bste_ptr,
                                       int16_t page, status_$t *status_p);

#endif /* AREA_INTERNAL_H */
