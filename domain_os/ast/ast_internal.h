/*
 * AST Internal Header
 *
 * This header contains internal function prototypes and data structures
 * used only within the AST subsystem. External code should use ast.h.
 */

#ifndef AST_INTERNAL_H
#define AST_INTERNAL_H

#include "ast/ast.h"
#include "dxm/dxm.h"
#include "mmap/mmap.h"
#include "mmu/mmu.h"
#include "time/time.h"
#include "uid/uid.h"
#include "bat/bat.h"
#include "disk/disk.h"
#include "file/file.h"
#include "fm/fm.h"
#include "misc/misc.h"
#include "vtoc/vtoc.h"
#include "netlog/netlog.h"
#include "pmap/pmap.h"
#include "netbuf/netbuf.h"
#include "network/network.h"
#include "rem_file/rem_file.h"
#include "dbuf/dbuf.h"
#include "wp/wp.h"
#include "anon/anon.h"
#include "area/area.h"

/* MMAP working-set-list page counts at 0xE232xx (see mmap/mmap.h) */
/* MMAP_$WSL_*_CNT are macros over MMAP_WSL[].page_count; see mmap/mmap.h. */

/* Internal AST functions (VTOC_$SEARCH_VOLUMES comes from vtoc/vtoc.h) */
extern void AST_$LOOKUP_WITH_HINTS(void *uid_info, uint32_t *vol_ptr, void *attrs, status_$t *status);
/*
 * AST_$DEACTIVATE_SEGMENT (0x00E01950) - module-local; see
 * ast/deactivate_segment.c.  Four Pascal parameters: the ASTE, two single
 * BYTE flags (A6+0x0C and A6+0x0E), and the status cell.
 */
void AST_$DEACTIVATE_SEGMENT(aste_t *aste, int8_t purge, int8_t keep,
                             status_$t *status);

/*
 * AST_$SET_ATTR_DISPATCH (0x00E04B00) is a Pascal procedure nested inside
 * ast_$set_attribute_internal: it declares no parameters and reads the
 * parent's frame through the static link in D4.  It is therefore emitted as a
 * static procedure in ast/set_attribute_internal.c and is deliberately not
 * declared here - it has no callable global ABI.
 */

/*
 * Internal helper functions
 */

/* Look up AOTE by UID - returns AOTE pointer */
aote_t *ast_$lookup_aote_by_uid(uid_t *uid);

/* Force lookup/activate AOTE for an object - returns AOTE pointer.
 *
 * `location` (A6+0x0C) is a LONGWORD and the callee reads it nine times;
 * the earlier note here claiming A6+0x0C was dead was wrong, and closing
 * bead source-sy5u meant reading the disassembly rather than the decompiler:
 *
 *   0x00E02194  move.l (0xc,A6),(0x8,A3)      aote->location := location
 *   0x00E021B2  tst.w  (0xc,A6) / smi         bit 31 -> aote+0xB9 remote flag
 *   0x00E021CC  tst.w  (0xc,A6) / bmi         local vs remote branch
 *   0x00E021D8  and.l  #0x7fffffff            low byte -> aote+0xB8 vol index
 *   0x00E021E6  and.l  #0xfffff               node id -> aote+0xB0
 *   0x00E021FA  move.l (0xc,A6),-(SP)         NETWORK_$GET_NET(location, ...)
 *   0x00E02228  and.l  #0x7fffffff / bne      zero selects the hint search
 *   0x00E02244  pea    (0xc,A6)               &location to AST_$LOOKUP_WITH_HINTS,
 *                                             which writes the resolved
 *                                             network/node back through it
 *   0x00E02260  move.l (0xc,A6),(0x8,A3)      re-store after that resolution
 *
 * The encoding is documented on aote_t.location in ast/ast.h -- it is the
 * same word AST_$GET_LOCATION returns (0x00E04766).  Of the thirteen
 * callers, AST_$MSTE_ACTIVATE_AND_WIRE passes mste->location (0x00E02F64)
 * and AST_$GET_DTV forwards its own second argument (0x00E054C6); the other
 * eleven pass a longword zero, meaning "location unknown, go find it".
 * AST_$GET_ATTRIBUTES is one of those eleven: A6-0x14 is a dedicated frame
 * cell cleared by `clr.l (-0x14,A6)` at 0x00E04822, never stored to again,
 * and pushed at 0x00E04832 -- a plain zero, not a volume or a segment.
 */
aote_t *ast_$force_activate_segment(uid_t *uid, uint32_t location, status_$t *status, int8_t force);

/* Look up existing ASTE for AOTE/segment */
aste_t* ast_$lookup_aste(aote_t *aote, int16_t segment);

/* Look up or create ASTE for AOTE/segment */
aste_t* ast_$lookup_or_create_aste(aote_t *aote, uint16_t segment, status_$t *status);

/* Wait for page transition to complete */
void ast_$wait_for_page_transition(void);

/* Allocate pages - returns count, takes count_flags and ppn_array */
/*
 * ast_$allocate_pages - allocate `count` physical pages
 *
 * Three Pascal parameters, not two: `move.w (0x8,A6),D2w` at 0x00E00D4E
 * takes the requested count, `cmp.w (0xa,A6),D0w` at 0x00E00E56 compares
 * the running total against a SECOND word (the minimum that must be
 * obtained before the routine stops waking the purifier), and
 * `movea.l (0xc,A6),A4` at 0x00E00D52 takes the array.  Every caller in
 * the image pushes (count, 1, ppn_array).
 */
int16_t ast_$allocate_pages(int16_t count, int16_t min_count,
                            uint32_t *ppn_array);

/* Clear transition bits in segment map */
void ast_$clear_transition_bits(uint32_t *segmap, uint16_t count);

/* Setup page read */
void ast_$setup_page_read(aste_t *aste, uint32_t *segmap, uint16_t start_page,
                          uint16_t count, uint16_t flags, status_$t *status);

/*
 * Allocate and zero the pages for a copy-on-write run.
 *
 * A nested procedure of AST_$TOUCH.  Its two real stack arguments are a
 * pointer into the segment map (0x08,A6) and a page count (0x0C,A6); the
 * remaining three are AST_$TOUCH's own `flags` (only bit 1 is read, via
 * `btst.b #0x1,(0x1d,A2)`), `ppn_array` (0x14,A2) and `status` (0x18,A2),
 * reached through the static link and passed explicitly here.
 */
int16_t ast_$count_valid_pages(uint32_t *segmap_entry, int16_t count,
                               uint16_t touch_flags,
                               uint32_t *ppn_array,
                               status_$t *status);

/* Read area pages from disk */
int16_t ast_$read_area_pages(aste_t *aste, uint32_t *segmap, uint32_t *ppn_array,
                             uint16_t start_page, uint16_t count,
                             status_$t *status);

/* Read area pages from network */
int16_t ast_$read_area_pages_network(aste_t *aste, uint32_t *segmap, uint32_t *ppn_array,
                                     uint16_t start_page, uint16_t count, uint8_t flags,
                                     status_$t *status);

/*
 * ast_$process_aote (0x00E01AD2) - process/deactivate one AOTE.
 *
 * All three flag arguments are single Domain BOOLEAN bytes, not words: the
 * prologue reads them with `move.b (0xc,A6),D2b` (0x00E01ADA),
 * `move.b (0xe,A6),D3b` (0x00E01ADE) and `move.b (0x10,A6),D4b`
 * (0x00E01AE2), and each is tested with `tst.b` / `bmi` (0x00E01B00,
 * 0x00E01B3E, 0x00E01B78).  The callers push them one byte at a time -
 * `st -(SP)` / `clr.w -(SP)` / `move.b (0xa,A6),-(SP)` at 0x00E01DD8,
 * 0x00E06138-0x00E06140, 0x00E05BFC-0x00E05C00 and 0x00E06A9C-0x00E06AA4 -
 * so the frame is aote (0x08), flags1 (0x0C), flags2 (0x0E), flags3 (0x10)
 * and status (0x12).  (source-o7gq)
 *
 *   flags1  TRUE = skip the AST_$PURIFY pass, and AST_$DEACTIVATE_SEGMENT's
 *           `purge` argument (0x00E01B4C `move.b D2b,-(SP)`)
 *   flags2  TRUE = deactivate even a type-2 object with attribute bit 1 set,
 *           and AST_$DEACTIVATE_SEGMENT's `keep` argument (0x00E01B4A
 *           `move.b D3b,-(SP)`)
 *   flags3  TRUE = wait for an in-transition ASTE instead of giving up
 *
 * Returns the busy/in-transition byte computed at 0x00E01AF0-0x00E01AFC.
 */
uint16_t ast_$process_aote(aote_t *aote, boolean flags1, boolean flags2,
                           boolean flags3, status_$t *status);

/* Free/release AOTE */
void ast_$release_aote(aote_t *aote);

/* Allocate new AOTE */
aote_t* ast_$allocate_aote(void);

/* ast_$purify_aote (0x00E013A0): `flags` is ONE BYTE read from (0xc,A6)
 * and handed to VTOCE_$WRITE (0x00E014B8). */
void ast_$purify_aote(aote_t *aote, boolean flags, status_$t *status);

/* ast_$update_aste (0x00E01566): `write_now` is ONE BYTE (`move.b (0x10,A6)`
 * at 0x00E01682) forwarded to FM_$WRITE. */
void ast_$update_aste(aste_t *aste, segmap_entry_t *segmap, boolean write_now,
                      status_$t *status);

/* Invalidate pages with wait */
/*
 * ast_$invalidate_with_wait (0x00E062FA) - nested procedure of
 * AST_$INVALIDATE; the parent's aote (-0x14), start page (0xC), is_remote
 * (-0x1A) and the (-0xC) cell it clears are passed explicitly.
 */
status_$t ast_$invalidate_with_wait(uint32_t end_page, aote_t *aote,
                                    uint32_t start_page, int8_t is_remote,
                                    uint32_t *parent_scratch);

/* Invalidate pages without wait */
/* ast_$invalidate_no_wait (0x00E064B0) - nested procedure of AST_$INVALIDATE;
 * the parent's aote (-0x14) and start page (0xC) are passed explicitly. */
void ast_$invalidate_no_wait(uint32_t end_page, aote_t *aote,
                             uint32_t start_page);

/*
 * ast_$flush_installed_pages (0x00E03FBC) - nested procedure of
 * AST_$FREE_PAGES; the parent's (0x8,A6) ASTE, (-0x100,A6) page array and
 * (-0x116,A6) count word are passed explicitly (see the .c).
 */
void ast_$flush_installed_pages(aste_t *aste, uint32_t *ppn_array,
                                uint16_t *installed_count);

/*
 * Set attribute on object (0x00E05214).
 *
 * The fifth argument is the caller's subject record: AST_$SET_ATTR_DISPATCH
 * reads it uplevel at (0x14,A6) for attribute type 0x14.  Callers that use
 * neither 0x14 nor the ACL merge pass NULL, as the recursive ADD_REFCOUNT call
 * at 0xE051A4 does.
 */
void ast_$set_attribute_internal(uid_t *uid, uint16_t attr_type, void *value,
                                 boolean wait_flag, ast_$subject_t *subject,
                                 clock_t *clock_info, status_$t *status);

/* ast_$validate_uid: declared in ast/ast.h -- VTOC_$SEARCH_VOLUMES
 * (vtoc/search_volumes.c) calls it (bead source-3uo). */

/*
 * Internal global variables: the AST_ module cells (the AOTE hash table,
 * list heads, scan positions, limits, counters, eventcounts,
 * vol_indices / vol_info_count, AST_$NOT_FOUND, the timestamp mask and the
 * clobbered UID) are fields of AST_$DATA and the ASTE / AOTE tables are
 * AST_$AOT, both in ast/ast.h (source-gmxj).
 */

/*
 * Cell holding AST_$SET_TROUBLE's address.  AST_$SAVE_CLOBBERED_UID pushes
 * the ADDRESS of this cell to DXM_$ADD_CALLBACK (0x00E0724E);
 * dxm_$callback_t keeps the queue entry 16 bytes on every target
 * (source-wy9y).
 *
 * The cell holds 0x00e071ea (AST_$SET_TROUBLE); defined in ast/ast_data.c.
 */
extern dxm_$callback_t PTR_AST_$SET_TROUBLE_00e07272;

/* AST_$ZERO_BUFF / AST_$COPY_BUFF are declared in ast/ast.h. */

/*
 * The status AST_$ACTIVATE_AOTE_CANNED hands CRASH_SYSTEM.  The cell is a
 * literal inside the AST_ init code segment (between AST_$ACTIVATE_AOTE_CANNED
 * at 0xE2F0C2 and AST_$ACTIVATE_ASTE_CANNED at 0xE2F1D4), so the map gives it
 * no name; the image holds 0x80030003.
 */
extern status_$t status_$t_00e2f1d0;

/* ASTE allocation functions */
extern aste_t *AST_$ALLOCATE_ASTE(void);
extern void AST_$FREE_ASTE(aste_t *aste);
extern void AST_$WAIT_FOR_AST_INTRANS(void);

/* Process info for statistics - include proc1.h for PROC1_$CURRENT, etc. */
#include "proc1/proc1.h"

/* The per-process read statistics are PROC1_$DATA.stats[pid].stat[2] (disk)
 * and .stat[3] (network) in proc1/proc1.h. */

#endif /* AST_INTERNAL_H */
