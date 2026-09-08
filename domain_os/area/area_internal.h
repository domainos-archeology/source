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
