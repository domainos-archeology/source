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
 * area_$seg_slot_t and area_$seg_table_t are defined in area/area.h: the
 * seg-table pool at AREA_$GLOBALS+0x150 is 64 area_$seg_table_t records
 * embedded in the module block, so the types have to be complete there.
 */


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
boolean area_$alloc_resources(uint16_t count);

/* area_$remote_sync (0x00E087BA) is a nested procedure of area_$resize,
 * reached through its static link; it is a static in area/resize.c. */

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
void area_$free_segments(int16_t area_id, uint32_t start_page,
                          uint32_t end_page, int8_t clear_bitmap,
                          status_$t *status_p);

/*
 * area_$alloc_seg_table - hand out one overflow segment table
 *
 * Takes ML_LOCK_AST (0x00E09D46 `move.w #0x12,-(SP)`), refuses with NIL when
 * AREA_$FORMAT.seg_table_count has reached 64 (0x00E09D52), otherwise takes
 * seg_table_pool[AREA_$FORMAT.seg_table_next], marks it allocated, gives it a
 * freshly wired 0x400-byte overflow-slot page at 0xEE6400 + index * 0x400
 * (WP_$CALLOC 0x00E09D88, MMU_$INSTALL 0x00E09D9A, zeroed at 0x00E09E36),
 * advances the cursor past the next unallocated record (0x00E09DE8) and links
 * the record onto AREA_$GLOBALS.seg_table_list[asid] (0x00E09E1E).
 *
 * @param asid          Address space ID
 * @param area_id       Area ID
 * @param table_idx     Table index
 *
 * Returns: Pointer to the new segment table entry, or NULL when full
 *
 * Original address: 0x00E09D2E
 */
area_$seg_table_t *area_$alloc_seg_table(int16_t asid, int16_t area_id,
                                          int16_t table_idx);

/*
 * area_$free_seg_table - return an overflow segment table to the pool
 *
 * Under ML_LOCK_AST: unmaps and frees the record's slot page (MMU_$VTOP /
 * MMU_$REMOVE / MMAP_$FREE), unlinks it from seg_table_list[asid] (`prev`
 * NIL = it is the head), clears its next and allocated fields and lowers
 * the pool cursor to its index.  See area/free_seg_table.c.
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
 * area_$get_aste - Get (activating if need be) the ASTE named by one
 * segment-map slot
 *
 * Six Pascal parameters; the prologue at 0x00E09A72-0x00E09A86 reads
 *   A6+0x08 word  area_id
 *   A6+0x0A long  slot        the area_$seg_slot_t cell for this segment
 *   A6+0x0E word  seg_idx
 *   A6+0x10 byte  in_trans_held  if FALSE (0x00E09A90 `tst.b D4b` / `bmi`)
 *                             spin on the slot's in-transition bit first and
 *                             clear it (advancing AREA_$PITE_IN_TRANS_EC) at
 *                             the end; if TRUE the caller owns that bit
 *   A6+0x12 byte  create      if false and no ASTE is mapped, fail with
 *                             0x00030004 (0x00E09AFA)
 *   A6+0x14 long  status
 * and the result is left in A0 (0x00E09AE4 `movea.l A0,A2` at the caller).
 * Called with ML lock 0x12 held.  See area/get_aste.c.
 *
 * Original address: 0x00E09A6A
 */
struct aste_t *area_$get_aste(int16_t area_id, area_$seg_slot_t *slot,
                              int16_t seg_idx, int8_t in_trans_held,
                              int8_t create, status_$t *status_p);

/*
 * area_$wait_pite_in_trans (0x00E0778E; area/wait_pite_in_trans.c) - drop
 * ML lock 0x12, wait for AREA_$PITE_IN_TRANS_EC to advance once, relock.
 */
void area_$wait_pite_in_trans(void);

/*
 * area_$rpmap_get (0x00E07370, 602 bytes; Ghidra FUN_00e07370, no map
 * symbol; area/rpmap_get.c) - the RPMAP page-cache
 * manager: returns (A0) the cached 0x400-byte remote page-map page at
 * 0xEE4C00 + (slot - 1) * 0x400 that holds the eight segment maps of group
 * seg_idx >> 3 of `entry', reading it from the partner on a miss.  Frame:
 *   (0x08) entry   longword  compared on (0x28,A3) = remote_volx
 *   (0x0C) seg_idx word      `lsr.w #0x3`
 *   (0x0E) dirty   byte      `tst.b (0xe,A6)` / `st (0xc,A0)`
 *   (0x10) flag    byte      `move.b (0x10,A6),D5b`
 *   (0x12) status
 * area_$get_aste pushes `clr.l` for the two bytes (0x00E09C50).
 */
void *area_$rpmap_get(area_$entry_t *entry, uint16_t seg_idx, int8_t dirty,
                      int8_t flag, status_$t *status_p);

/*
 * The slot cell read as the big-endian LONGWORD the code tests it as
 * (`and.l (A4),D1` with 0x3FFFFF at 0x00E09BBC / 0x00E09CDC): while state
 * bit 7 is clear, its low 22 bits are the disk address of the segment
 * group's page-map block.
 */
#define AREA_SLOT_LONG(s) \
    (((uint32_t)(s)->bits << 24) | ((uint32_t)(s)->state << 16) | \
     (uint32_t)(s)->aste_index)
#define AREA_SLOT_STORE(s, v) do {                                         \
        uint32_t area_slot_v_ = (uint32_t)(v);                              \
        (s)->bits = (uint8_t)(area_slot_v_ >> 24);                          \
        (s)->state = (uint8_t)(area_slot_v_ >> 16);                         \
        (s)->aste_index = (uint16_t)area_slot_v_;                           \
    } while (0)
#define AREA_SLOT_DADDR_MASK    0x003FFFFFu
#define AREA_SLOT_HAS_ASTE      0x80    /* state bit 7: aste_index is valid */
#define AREA_SLOT_IN_TRANS      0x40    /* state bit 6 */

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
