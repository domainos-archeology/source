/*
 * AST - Active Segment Table Management
 *
 * This module provides active segment management for Domain/OS.
 * It sits above the MMU and MMAP layers, managing the relationship
 * between objects (files), segments, and physical pages.
 *
 * Key concepts:
 * - AOTE (Active Object Table Entry): Represents a cached object (file)
 * - ASTE (Active Segment Table Entry): Represents a segment mapping
 * - Segment Map: 32 page entries per segment (1KB pages, 32KB segments)
 *
 * Key data structures:
 * - AOTE: 192 bytes (0xC0) per entry, hash-chained by UID
 * - ASTE: 20 bytes (0x14) per entry
 * - Segment Map: 128 bytes (0x80) per segment at 0xED5000
 *
 * Memory layout (m68k):
 * - AST globals: 0xE1DC80
 * - ASTE array: 0xEC5400
 * - AOTE area: grows from 0xEC7B60
 * - Segment maps: 0xED5000
 *
 * Original source was likely Pascal, converted to C.
 */

#ifndef AST_H
#define AST_H

#include "base/base.h"
#include "ec/ec.h"
#include "ml/ml.h"
/* file_$obj_loc_t - the 0x20-byte object-location record AST_$GET_LOCATION and
 * AST_$GET_ATTRIBUTES fill from aote+0x9C.  Defined in file/file.h, which is
 * where FILE_$PRIV_LOCK (its other producer) already documents the layout. */
#include "file/file.h"

/* AST status codes (module 0x03) */
#define status_$ast_incompatible_request 0x00030006
#define status_$ast_write_concurrency_violation 0x00030005
#define status_$ast_eof 0x00030001

/* PMAP status codes (module 0x05) */
#define status_$pmap_bad_assoc 0x00050006
#define status_$pmap_page_null 0x00050008
#define status_$pmap_read_concurrency_violation 0x0005000A

/* OS status codes (module 0x03) */
#define status_$os_only_local_access_allowed 0x0003000A

/*
 * Forward declarations
 */
struct aote_t;
struct aste_t;

/*
 * ASTE (Active Segment Table Entry)
 *
 * Represents a segment mapping. Each ASTE links an object (via AOTE)
 * to a segment number. Size: 20 bytes (0x14).
 */
typedef struct aste_t {
  struct aste_t *next; /* 0x00: Next ASTE in chain (or free list) */
  struct aote_t *aote; /* 0x04: Pointer to owning AOTE */
  uint16_t segment;    /* 0x08: First segment number << 5 (or page offset) */
  uint16_t unknown_0a; /* 0x0A: Unknown */
  uint16_t timestamp;  /* 0x0C: Timestamp for LRU */
  uint16_t seg_index;  /* 0x0E: Segment index (for segment map lookup) */
  uint8_t page_count;  /* 0x10: Number of pages mapped */
  uint8_t wire_count;  /* 0x11: Wire/reference count */
  uint16_t flags;      /* 0x12: Flags - see ASTE_FLAG_* below */
} aste_t;

/* Layout recovered from the disassembly -- see the field comments above. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(aste_t, next) == 0x00, "aste_t.next");
_Static_assert(__builtin_offsetof(aste_t, aote) == 0x04, "aste_t.aote");
_Static_assert(__builtin_offsetof(aste_t, segment) == 0x08, "aste_t.segment");
_Static_assert(__builtin_offsetof(aste_t, unknown_0a) == 0x0A, "aste_t.unknown_0a");
_Static_assert(__builtin_offsetof(aste_t, timestamp) == 0x0C, "aste_t.timestamp");
_Static_assert(__builtin_offsetof(aste_t, seg_index) == 0x0E, "aste_t.seg_index");
_Static_assert(__builtin_offsetof(aste_t, page_count) == 0x10, "aste_t.page_count");
_Static_assert(__builtin_offsetof(aste_t, wire_count) == 0x11, "aste_t.wire_count");
_Static_assert(__builtin_offsetof(aste_t, flags) == 0x12, "aste_t.flags");
_Static_assert(sizeof(aste_t) == 0x14, "aste_t size");
#endif

/* ASTE flags (at offset 0x12) */
#define ASTE_FLAG_IN_TRANS 0x8000 /* In transition (being modified) */
#define ASTE_FLAG_LOCKED 0x4000   /* Locked - cannot be freed */
#define ASTE_FLAG_DIRTY 0x2000    /* Dirty - needs writeback */
#define ASTE_FLAG_AREA 0x1000     /* Area mapping (vs single segment) */
#define ASTE_FLAG_REMOTE 0x0800   /* Remote (network) object */
#define ASTE_FLAG_BUSY 0x0040     /* Busy flag (cleared on scan) */

/* ASTE index mask */
#define ASTE_INDEX_MASK 0x01FF /* 9-bit index */

/*
 * AOTE (Active Object Table Entry)
 *
 * Represents a cached object (file). Contains object attributes,
 * UID, and links to ASTEs. Size: 192 bytes (0xC0).
 */
typedef struct aote_t {
  struct aote_t *hash_next; /* 0x00: Next in hash chain */
  struct aste_t *aste_list; /* 0x04: List of ASTEs for this object */
  /*
   * 0x08: the object's LOCATION word -- not a UID.  This is the value
   * ast_$force_activate_segment takes as its second argument and the value
   * AST_$GET_LOCATION hands back through its `location_out` parameter
   * (`move.l (0x8,A0),(A1)` at 0x00E04766).  Encoding, from
   * ast_$force_activate_segment 0x00E021B2-0x00E021F0 and
   * AST_$LOOKUP_WITH_HINTS 0x00E01D08-0x00E01D2A:
   *
   *   bit 31       set  -> the object is remote (`tst.w (0xc,A6)` / `smi`
   *                        at 0x00E021B2-0x00E021B6 tests this longword's
   *                        sign through its high word, and mmap/ws_scan.c
   *                        tests `aote->vol_uid & 0x80000000`)
   *   bits 20..30  remote only: the network number, filled in by
   *                        NETWORK_$INSTALL_NET (`(x & 0xFFF00000) | node`
   *                        at 0x00E01D2A, ast/load_aote.c:117 and
   *                        ast/activate_aote_canned.c:47)
   *   bits 0..19   remote only: the node id (`and.l #0xfffff` at
   *                        0x00E021E6, copied to obj_loc.node aote+0xB0)
   *   low byte     local only: the logical volume index, copied to
   *                        aote+0xB8 (`move.b D0b,(0xb8,A3)` at 0x00E021DC
   *                        after masking off bit 31)
   *   0            "location unknown" -- 0x00E02222 `and.l #0x7fffffff` /
   *                        `bne` selects the search-by-hint path instead of
   *                        the direct VTOC lookup.
   *
   * For a local object the word is replaced after the lookup by
   * obj_loc.block_hint (`move.l (0xa0,A3),(0x8,A3)` at 0x00E022AE).
   * TODO(source-thsz, 0x00E04766): the field is still spelled `vol_uid`
   * here and in mmap/ws_scan.c; renaming it to `location` is a tree-wide
   * change outside the ast/ pass that recovered the encoding.
   */
  uint32_t vol_uid;         /* 0x08: object location word (see above) */

  /*
   * 0x0C - 0x9B: object attributes.  Layout recovered instruction by
   * instruction from AST_$SET_ATTR_DISPATCH (0x00E04B00); the case that
   * touches each field is named in the comment.
   */
  uint8_t obj_type;      /* 0x0C: object type; 0 selects the 0x3FFF attr set */
  uint8_t sub_type;      /* 0x0D: sub type; 1 or 2 block the last unref */
  uint8_t attr_flags_hi; /* 0x0E: high byte of the 16-bit attribute flags */
  uint8_t attr_flags_lo; /* 0x0F: low byte of the same word */

  uid_t uid;             /* 0x10: object UID (hashed by ast_$lookup_aote_by_uid) */
  uid_t dtc;             /* 0x18: creation time (attr 4) */
  /*
   * 0x20: the object's current LENGTH IN BYTES, 32 bits.  Bead source-traa.
   * Every site that touches it treats it as a byte count over 1KB pages;
   * nothing anywhere reads it as a page or segment count, and it is not a
   * truncated copy of anything (see the 0x28 comment below -- that pair is a
   * clock, not a 48-bit length).  The complete set of accesses in the image,
   * found by disassembling all 1871 functions and keeping every `(0x20,An)`
   * in a function that also touches the AOTE-only offsets 0x9C/0xB9/0xBF:
   *
   *   writers
   *     0x00E02AB6  ast_$setup_page_read: when the last page just allocated
   *                 ends at or past the current length, length :=
   *                 last_page_byte_offset + 0x400, i.e. grown to the next
   *                 whole 1KB page (`cmp.l (0x20,A1),D0` / `blt` /
   *                 `addi.l #0x400,D0` at 0x00E02AAA-0x00E02AB6).
   *     0x00E02EBE  ast_$read_area_pages_network: same idiom, 0x00E02E9A.
   *     0x00E04918  AST_$GET_ATTRIBUTES: after refreshing the attribute
   *                 record from the VTOC or the network, writes back
   *                 max(cached length, record+0x14) -- 0x00E048F0-0x00E048FC.
   *                 record+0x14 IS this field: 0x00E0490A copies 0x24
   *                 longwords from the record over aote+0x0C, so record+0x14
   *                 lands at aote+0x20.  The max() keeps a length the local
   *                 pager already grew from being pulled back by a stale
   *                 on-disk value; it is the reason the field only ever
   *                 grows on the refresh path.
   *     0x00E06098  AST_$TRUNCATE: length := the new-length argument
   *                 (`move.l (0xc,A6),(0x20,A0)`), a byte count -- so the
   *                 field is NOT a monotone high-water mark; truncation
   *                 lowers it.
   *   readers (all convert to a page index with `(length - 1) >> 10`, or
   *   compare against a byte offset built as page_index * 0x400)
   *     0x00E03258  AST_$TOUCH: `tst.l` for the empty object, then
   *                 `(len-1)>>10` vs seg*32 + page (0x00E0325E-0x00E0326A).
   *     0x00E05A78  AST_$PURIFY: snapshots it under the AST lock.
   *     0x00E066C0  AST_$INVALIDATE: same `(len-1)>>10` page clamp.
   *     0x00E06BF4  AST_$GET_SEG_MAP: snapshots it into the frame and
   *                 compares it against byte offsets at 0x00E06D52/0x00E06D80.
   *     0x00E13074  pmap_$write_page: `(seg*32+page)*0x400` vs length, then
   *                 length again to size the partial last page of the write.
   *     ast_$get_common_attributes 0x00E04A30 reads it as record+0x14 and
   *                 stores it as the common record's +0x04 length.
   *   NOT this field: AST_$SET_ATTR_DISPATCH's `(0x20,A1)`/`(0x20,A2)` at
   *     0x00E04E78/0x00E04FAA are attribute-record+0x20 (owner1_ext), and
   *     PMAP_$PURIFIER_L's 0x00E13EB4/0x00E13F1E walk a 0x24-byte record
   *     (`lea (-0x24,A1),A1`), not an AOTE.
   */
  uint32_t length;       /* 0x20: current object length in bytes */
  uint32_t unknown_24;   /* 0x24: page counter; ast_$setup_page_read adds the
                          * number of pages just allocated (`add.l D5,(0x24,A1)`
                          * at 0x00E02AE2) */

  /*
   * 0x28/0x2C: a 48-bit Apollo clock (high 32 / low 16), NOT a length, in
   * spite of the field names.  Bead source-traa turned this up while
   * establishing that 0x20 is the only length in the AOTE:
   *   - ast_$setup_page_read 0x00E02ABA-0x00E02ACE calls TIME_$CLOCK on
   *     aote+0x40 and then copies aote+0x40/0x44 into aote+0x28/0x2C.
   *   - pmap/purifier_l.c:334-336 does the same pair in the other
   *     direction: TIME_$CLOCK(aote+0x28) then aote+0x40/0x44 := 0x28/0x2C.
   *   - ast_$read_area_pages_network 0x00E02EC8 stores the freshly read
   *     clock into both pairs.
   *   - AST_$SET_ATTR_DISPATCH's "rounded" cases (0x00E04DA6-0x00E04DD4)
   *     round a non-zero LOW 16 bits up by adding one to the high 32 and
   *     zeroing the low -- rounding a clock to the next whole tick, which
   *     is meaningless for a length; and attribute 9, which writes 0x28 with
   *     a 32-bit value and zeroes 0x2C (0x00E04D88), is FILE_ATTR_DTM_AST.
   * TODO(source-xk18, 0x00E02ACE): rename this pair (and the 0x30 pair that
   * shares the misnaming) once mmap/ and pmap/, which spell it `len_high`,
   * can be updated in the same pass.
   */
  uint32_t len_high;     /* 0x28: 48-bit clock, high 32 bits (DTM) */
  uint16_t len_low;      /* 0x2C: 48-bit clock, low 16 bits */
  uint16_t unknown_2e;   /* 0x2E */

  uint32_t dtm_high;     /* 0x30: DTM, high 32 bits (attr 10/0x18/0x1A/0x1B) */
  uint16_t dtm_low;      /* 0x34: DTM, low 16 bits */
  uint16_t unknown_36;   /* 0x36 */

  uint32_t dtu_high;     /* 0x38: DTU; TIME_$ABS_CLOCK writes 6 bytes here */
  uint16_t dtu_low;      /* 0x3C */
  uint16_t unknown_3e;   /* 0x3E */

  uint32_t dta_high;     /* 0x40: attribute-modified clock (common tail 0xE05100) */
  uint16_t dta_low;      /* 0x44 */
  uint16_t unknown_46;   /* 0x46 */

  uid_t mod_time;        /* 0x48: modification time (attr 5) */
  uint32_t blocks;       /* 0x50: block count (attr 0x0B) */

  /*
   * 0x54 - 0x7F: the 44-byte ACL image.  Attr 0x13 copies it verbatim from
   * the low 44 bytes of an ast_$attr_rec_t (0xE04F1C, 11 longword moves),
   * so the two layouts must stay in step.
   */
  uid_t owner1;          /* 0x54 (attr 0x10) */
  uid_t owner2;          /* 0x5C (attr 0x11) */
  uid_t owner3;          /* 0x64 (attr 0x12) */
  uint8_t rights1;       /* 0x6C */
  uint8_t rights2;       /* 0x6D */
  uint8_t rights3;       /* 0x6E */
  uint8_t rights4;       /* 0x6F */
  uint8_t rights5;       /* 0x70 */
  int8_t access_flags;   /* 0x71: bit 7 = OS-only access; bits 4-6 = mode bits */
  uint16_t unknown_72;   /* 0x72 */
  uint32_t owner1_ext;   /* 0x74 */
  uint32_t owner2_ext;   /* 0x78 */
  uint32_t owner3_ext;   /* 0x7C */

  uint16_t refcount;     /* 0x80: reference count (attrs 6, 7, 8) */
  uint16_t linkcount;    /* 0x82: link count (attr 0x16) */
  uid_t uid_84;          /* 0x84 (attr 0x0E) */
  uid_t uid_8c;          /* 0x8C (attr 0x0F) */
  uid_t acl_uid;         /* 0x94 (attrs 3, 0x13, 0x14) */

  /* 0x9C - 0xBB: object UID and related info */
  uid_t obj_uid;          /* 0x9C: secondary object UID */
  uint32_t unknown_a4[5]; /* 0xA4 */
  uint8_t vol_index;      /* 0xB8: volume index (AST_$DISMOUNT) */
  int8_t remote_flag;     /* 0xB9: negative when the object is remote */
  uint16_t unknown_ba;    /* 0xBA */

  /* 0xBC - 0xBF: Flags and status */
  uint16_t status_flags; /* 0xBC: Status flags */
  uint8_t ref_count;     /* 0xBE: Reference count */
  uint8_t flags;         /* 0xBF: Flags - see AOTE_FLAG_* below */
} aote_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(aote_t, hash_next) == 0x00, "aote_t.hash_next");
_Static_assert(__builtin_offsetof(aote_t, aste_list) == 0x04, "aote_t.aste_list");
_Static_assert(__builtin_offsetof(aote_t, vol_uid) == 0x08, "aote_t.vol_uid");
_Static_assert(__builtin_offsetof(aote_t, sub_type) == 0x0D, "aote_t.sub_type");
_Static_assert(__builtin_offsetof(aote_t, attr_flags_lo) == 0x0F, "aote_t.attr_flags_lo");
_Static_assert(__builtin_offsetof(aote_t, length) == 0x20, "aote_t.length");
_Static_assert(__builtin_offsetof(aote_t, unknown_24) == 0x24, "aote_t.unknown_24");
_Static_assert(__builtin_offsetof(aote_t, len_low) == 0x2C, "aote_t.len_low");
_Static_assert(__builtin_offsetof(aote_t, unknown_2e) == 0x2E, "aote_t.unknown_2e");
_Static_assert(__builtin_offsetof(aote_t, dtm_low) == 0x34, "aote_t.dtm_low");
_Static_assert(__builtin_offsetof(aote_t, unknown_36) == 0x36, "aote_t.unknown_36");
_Static_assert(__builtin_offsetof(aote_t, dtu_low) == 0x3C, "aote_t.dtu_low");
_Static_assert(__builtin_offsetof(aote_t, unknown_3e) == 0x3E, "aote_t.unknown_3e");
_Static_assert(__builtin_offsetof(aote_t, dta_low) == 0x44, "aote_t.dta_low");
_Static_assert(__builtin_offsetof(aote_t, unknown_46) == 0x46, "aote_t.unknown_46");
_Static_assert(__builtin_offsetof(aote_t, rights2) == 0x6D, "aote_t.rights2");
_Static_assert(__builtin_offsetof(aote_t, rights3) == 0x6E, "aote_t.rights3");
_Static_assert(__builtin_offsetof(aote_t, rights4) == 0x6F, "aote_t.rights4");
_Static_assert(__builtin_offsetof(aote_t, rights5) == 0x70, "aote_t.rights5");
_Static_assert(__builtin_offsetof(aote_t, unknown_72) == 0x72, "aote_t.unknown_72");
_Static_assert(__builtin_offsetof(aote_t, owner2_ext) == 0x78, "aote_t.owner2_ext");
_Static_assert(__builtin_offsetof(aote_t, owner3_ext) == 0x7C, "aote_t.owner3_ext");
_Static_assert(__builtin_offsetof(aote_t, unknown_a4) == 0xA4, "aote_t.unknown_a4");
_Static_assert(__builtin_offsetof(aote_t, unknown_ba) == 0xBA, "aote_t.unknown_ba");
_Static_assert(__builtin_offsetof(aote_t, ref_count) == 0xBE, "aote_t.ref_count");
#endif

#if defined(ARCH_M68K)
_Static_assert(offsetof(aote_t, obj_type) == 0x0C, "aote_t.obj_type");
_Static_assert(offsetof(aote_t, attr_flags_hi) == 0x0E, "aote_t.attr_flags_hi");
_Static_assert(offsetof(aote_t, uid) == 0x10, "aote_t.uid");
_Static_assert(offsetof(aote_t, dtc) == 0x18, "aote_t.dtc");
_Static_assert(offsetof(aote_t, len_high) == 0x28, "aote_t.len_high");
_Static_assert(offsetof(aote_t, dtm_high) == 0x30, "aote_t.dtm_high");
_Static_assert(offsetof(aote_t, dtu_high) == 0x38, "aote_t.dtu_high");
_Static_assert(offsetof(aote_t, dta_high) == 0x40, "aote_t.dta_high");
_Static_assert(offsetof(aote_t, mod_time) == 0x48, "aote_t.mod_time");
_Static_assert(offsetof(aote_t, blocks) == 0x50, "aote_t.blocks");
_Static_assert(offsetof(aote_t, owner1) == 0x54, "aote_t.owner1");
_Static_assert(offsetof(aote_t, owner2) == 0x5C, "aote_t.owner2");
_Static_assert(offsetof(aote_t, owner3) == 0x64, "aote_t.owner3");
_Static_assert(offsetof(aote_t, rights1) == 0x6C, "aote_t.rights1");
_Static_assert(offsetof(aote_t, access_flags) == 0x71, "aote_t.access_flags");
_Static_assert(offsetof(aote_t, owner1_ext) == 0x74, "aote_t.owner1_ext");
_Static_assert(offsetof(aote_t, refcount) == 0x80, "aote_t.refcount");
_Static_assert(offsetof(aote_t, linkcount) == 0x82, "aote_t.linkcount");
_Static_assert(offsetof(aote_t, uid_84) == 0x84, "aote_t.uid_84");
_Static_assert(offsetof(aote_t, uid_8c) == 0x8C, "aote_t.uid_8c");
_Static_assert(offsetof(aote_t, acl_uid) == 0x94, "aote_t.acl_uid");
_Static_assert(offsetof(aote_t, obj_uid) == 0x9C, "aote_t.obj_uid");
_Static_assert(offsetof(aote_t, vol_index) == 0xB8, "aote_t.vol_index");
_Static_assert(offsetof(aote_t, remote_flag) == 0xB9, "aote_t.remote_flag");
_Static_assert(offsetof(aote_t, status_flags) == 0xBC, "aote_t.status_flags");
_Static_assert(offsetof(aote_t, flags) == 0xBF, "aote_t.flags");
_Static_assert(sizeof(aote_t) == 0xC0, "sizeof aote_t");
#endif

/*
 * ast_$attr_rec_t - the record AST_$SET_ATTRIBUTE passes for the
 * "set several owners at once" attribute types (0x13, 0x14) and, for its
 * rights bytes only, type 0x15.
 *
 * Bytes 0x00..0x2B are a byte-for-byte image of aote_t 0x54..0x7F: attr 0x13
 * copies them with eleven longword moves at 0xE04F1C.
 */
typedef struct ast_$attr_rec_t {
  uid_t owner1;         /* 0x00 -> aote_t.owner1 */
  uid_t owner2;         /* 0x08 -> aote_t.owner2 */
  uid_t owner3;         /* 0x10 -> aote_t.owner3 */
  uint8_t rights1;      /* 0x18 -> aote_t.rights1 */
  uint8_t rights2;      /* 0x19 -> aote_t.rights2 */
  uint8_t rights3;      /* 0x1A -> aote_t.rights3 */
  uint8_t rights4;      /* 0x1B -> aote_t.rights4 */
  uint8_t rights5;      /* 0x1C -> aote_t.rights5 */
  int8_t access_flags;  /* 0x1D -> aote_t.access_flags bit 7 */
  uint16_t unknown_1e;  /* 0x1E */
  uint32_t owner1_ext;  /* 0x20 -> aote_t.owner1_ext */
  uint32_t owner2_ext;  /* 0x24 -> aote_t.owner2_ext */
  uint32_t owner3_ext;  /* 0x28 -> aote_t.owner3_ext */
  uid_t acl_uid;        /* 0x2C -> aote_t.acl_uid */
} ast_$attr_rec_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(ast_$attr_rec_t, owner2) == 0x08, "attr_rec.owner2");
_Static_assert(offsetof(ast_$attr_rec_t, owner3) == 0x10, "attr_rec.owner3");
_Static_assert(offsetof(ast_$attr_rec_t, rights1) == 0x18, "attr_rec.rights1");
_Static_assert(offsetof(ast_$attr_rec_t, access_flags) == 0x1D, "attr_rec.access_flags");
_Static_assert(offsetof(ast_$attr_rec_t, owner1_ext) == 0x20, "attr_rec.owner1_ext");
_Static_assert(offsetof(ast_$attr_rec_t, acl_uid) == 0x2C, "attr_rec.acl_uid");
_Static_assert(sizeof(ast_$attr_rec_t) == 0x34, "sizeof ast_$attr_rec_t");
/* The two layouts must agree: attr 0x13 copies one onto the other. */
_Static_assert(offsetof(aote_t, owner1_ext) - offsetof(aote_t, owner1) ==
                   offsetof(ast_$attr_rec_t, owner1_ext),
               "attr_rec image does not match aote_t 0x54..0x7F");
#endif

/*
 * ast_$subject_t - the caller-supplied subject (the parent's 0x14 parameter,
 * uplevel-referenced by AST_$SET_ATTR_DISPATCH case 0x14).  Holds the
 * requesting principal's owner UIDs plus a supplementary group list that the
 * dispatcher searches at 0xE05028.
 */
typedef struct ast_$subject_t {
  uid_t owner1;        /* 0x00 */
  uid_t owner2;        /* 0x08 */
  uid_t owner3;        /* 0x10 */
  uint32_t unknown_18; /* 0x18 */
  uid_t groups[9];     /* 0x1C: entries 1..8 are searched at 0xE05028 */
} ast_$subject_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(ast_$subject_t, owner3) == 0x10, "subject.owner3");
_Static_assert(offsetof(ast_$subject_t, groups) == 0x1C, "subject.groups");
#endif

/* AOTE flags (at offset 0xBF) */
#define AOTE_FLAG_IN_TRANS 0x80 /* In transition */
#define AOTE_FLAG_BUSY 0x40     /* Busy/locked */
#define AOTE_FLAG_DIRTY 0x20    /* Dirty - needs writeback */
#define AOTE_FLAG_TOUCHED 0x10  /* Recently accessed */

/* AOTE remote flag (at offset 0xB9) */
#define AOTE_REMOTE_FLAG 0x80 /* Object is remote (network) */

/*
 * Segment Map Entry
 *
 * Each segment has 32 page entries (4 bytes each = 128 bytes total).
 * Located at 0xED5000 + (segment_index * 0x80).
 */
typedef struct segmap_entry_t {
  uint32_t entry; /* Page mapping entry */
} segmap_entry_t;

/* Segment map entry flags (high byte) */
#define SEGMAP_IN_TRANS 0x80000000      /* Page in transition */
#define SEGMAP_VALID 0x40000000         /* Valid mapping */
#define SEGMAP_WIRED 0x20000000         /* Page is wired */
#define SEGMAP_COPY_ON_WRITE 0x00400000 /* Copy-on-write page */
#define SEGMAP_PPN_MASK 0x007FFFFF      /* Physical page number mask */

/*
 * AST Global State
 *
 * Global variables for the AST subsystem, based at 0xE1DC80.
 */

/*
 * Architecture-independent macros for AST access
 */
#if defined(ARCH_M68K)
/* AST globals base */
#define AST_GLOBALS_BASE 0xE1DC80

/* AOTE hash table (at base + 0x00, 256 entries * 4 bytes) */
#define AOTH (*(uint32_t *)0xE1DC80)

/* ASTE array base */
#define ASTE_BASE ((aste_t *)0xEC5400)

/* Segment map base */
#define SEGMAP_BASE ((segmap_entry_t *)0xED5000)

/* AST globals at various offsets from base */
#define AST_$AOTE_LIMIT (*(aote_t **)0xE1E074)        /* 0x3F4 */
#define AST_$FREE_ASTE_HEAD (*(aste_t **)0xE1E078)    /* 0x3F8 */
#define AST_$ASTE_SCAN_POS (*(aste_t **)0xE1E07C)     /* 0x3FC */
#define AST_$ASTE_LIMIT (*(aste_t **)0xE1E080)        /* 0x400 */
#define AST_$DISM_SEQN (*(uint32_t *)0xE1E084)        /* 0x404 */
#define AST_$DISM_EC (*(ec_$eventcount_t *)0xE1E088)  /* 0x408 */
#define AST_$UPDATE_SCAN (*(aote_t **)0xE1E104)       /* 0x484 */
#define AST_$UPDATE_TIMESTAMP (*(uint16_t *)0xE1E108) /* 0x488 */
#define AST_$AOTE_SEQN (*(uint32_t *)0xE1E0B4)        /* 0x434 */

/* Event counters */
#define AST_$AST_IN_TRANS_EC (*(ec_$eventcount_t *)0xE1E0A8)  /* 0x428 */
#define AST_$PMAP_IN_TRANS_EC (*(ec_$eventcount_t *)0xE1E0CC) /* 0x44C */

/* Statistics */
#define AST_$ALLOC_WORST_AST (*(uint32_t *)0xE1E0C4) /* 0x444 */
#define AST_$ALLOC_TOTAL_AST (*(uint32_t *)0xE1E0C8) /* 0x448 */
#define AST_$WS_FLT_CNT (*(uint32_t *)0xE1E0D8)      /* 0x458 */
#define AST_$PAGE_FLT_CNT (*(uint32_t *)0xE1E0DC)    /* 0x45C */
#define AST_$ALLOC_TOO_FEW_CNT (*(uint32_t *)0xE1E0E0) /* 0x460 (used by osinfo) */
#define AST_$ALLOC_CNT (*(uint32_t *)0xE1E0E4)       /* 0x464 (used by osinfo) */
#define AST_$FREE_ASTES (*(uint16_t *)0xE1E0E8)      /* 0x468 */
#define AST_$GROW_AHEAD_CNT (*(uint16_t *)0xE1E0EC)  /* 0x46C */
#define AST_$SIZE_AOT (*(uint16_t *)0xE1E0EE)        /* 0x46E */
#define AST_$SIZE_AST (*(uint16_t *)0xE1E0F0)        /* 0x470 */
#define AST_$ASTE_AREA_CNT (*(uint16_t *)0xE1E0F2)   /* 0x472 */
#define AST_$ASTE_R_CNT (*(uint16_t *)0xE1E0F4)      /* 0x474 */
#define AST_$ASTE_L_CNT (*(uint16_t *)0xE1E0F6)      /* 0x476 */
#else
/* For non-m68k platforms */
extern uint8_t *ast_globals_base;
extern uint32_t ast_aoth[];
extern aste_t *ast_aste_base;
extern segmap_entry_t *ast_segmap_base;

#define AST_GLOBALS_BASE ((uintptr_t)ast_globals_base)
extern aote_t *ast_aote_limit;
extern aste_t *ast_free_aste_head;
extern aste_t *ast_aste_scan_pos;
extern aste_t *ast_aste_limit;
extern uint32_t ast_dism_seqn;
extern ec_$eventcount_t ast_dism_ec;
extern aote_t *ast_update_scan;
extern uint16_t ast_update_timestamp;
extern uint32_t ast_aote_seqn;
extern ec_$eventcount_t ast_ast_in_trans_ec;
extern ec_$eventcount_t ast_pmap_in_trans_ec;
extern uint32_t ast_alloc_worst;
extern uint32_t ast_alloc_total;
extern uint32_t ast_ws_flt_cnt;
extern uint32_t ast_page_flt_cnt;
extern uint32_t ast_alloc_too_few_cnt;
extern uint32_t ast_alloc_cnt;
extern uint16_t ast_free_astes;
extern uint16_t ast_grow_ahead_cnt;
extern uint16_t ast_size_aot;
extern uint16_t ast_size_ast;
extern uint16_t ast_aste_area_cnt;
extern uint16_t ast_aste_r_cnt;
extern uint16_t ast_aste_l_cnt;

#define AOTH ast_aoth[0]
#define ASTE_BASE ast_aste_base
#define SEGMAP_BASE ast_segmap_base
#define AST_$AOTE_LIMIT ast_aote_limit
#define AST_$FREE_ASTE_HEAD ast_free_aste_head
#define AST_$ASTE_SCAN_POS ast_aste_scan_pos
#define AST_$ASTE_LIMIT ast_aste_limit
#define AST_$DISM_SEQN ast_dism_seqn
#define AST_$DISM_EC ast_dism_ec
#define AST_$UPDATE_SCAN ast_update_scan
#define AST_$UPDATE_TIMESTAMP ast_update_timestamp
#define AST_$AOTE_SEQN ast_aote_seqn
#define AST_$AST_IN_TRANS_EC ast_ast_in_trans_ec
#define AST_$PMAP_IN_TRANS_EC ast_pmap_in_trans_ec
#define AST_$ALLOC_WORST_AST ast_alloc_worst
#define AST_$ALLOC_TOTAL_AST ast_alloc_total
#define AST_$WS_FLT_CNT ast_ws_flt_cnt
#define AST_$PAGE_FLT_CNT ast_page_flt_cnt
#define AST_$ALLOC_TOO_FEW_CNT ast_alloc_too_few_cnt
#define AST_$ALLOC_CNT ast_alloc_cnt
#define AST_$FREE_ASTES ast_free_astes
#define AST_$GROW_AHEAD_CNT ast_grow_ahead_cnt
#define AST_$SIZE_AOT ast_size_aot
#define AST_$SIZE_AST ast_size_ast
#define AST_$ASTE_AREA_CNT ast_aste_area_cnt
#define AST_$ASTE_R_CNT ast_aste_r_cnt
#define AST_$ASTE_L_CNT ast_aste_l_cnt
#endif

/* Get ASTE entry by index */
#define ASTE_FOR_INDEX(idx) (&ASTE_BASE[(idx)])

/* Get segment map for segment index */
#define SEGMAP_FOR_SEG(seg)                                                    \
  ((segmap_entry_t *)((char *)SEGMAP_BASE + ((seg) << 7)))

/* Maximum sizes */
#define AST_MAX_AOTE 0x118 /* 280 entries */
#define AST_MAX_ASTE 0x1F8 /* 504 entries */
#define AST_MIN_AOTE 0x28  /* 40 entries */
#define AST_MIN_ASTE 0x50  /* 80 entries */

/*
 * Lock IDs used by AST
 */
#define AST_LOCK_ID 0x12  /* Main AST lock */
#define PMAP_LOCK_ID 0x14 /* PMAP lock */

/*
 * Segment map entry flags
 */
#define SEGMAP_FLAG_IN_TRANS 0x80000000  /* Page in transition */
#define SEGMAP_FLAG_IN_USE 0x40000000    /* Page is in use (installed) */
#define SEGMAP_FLAG_INSTALLED 0x20000000 /* Page installed in MMU */
#define SEGMAP_FLAG_COW 0x00400000       /* Copy-on-write */
#define SEGMAP_DISK_ADDR_MASK 0x007FFFFF /* Disk address / PPN mask */

/*
 * Note: Physical page attributes are tracked using mmape_t from mmap/mmap.h.
 * The MMAPE array is located at 0xEB2800, with 16 bytes per physical page.
 * AST uses mmape_t fields like seg_offset (page index), segment (seg_index),
 * and disk_addr for page mapping.
 */

/*
 * Function prototypes - Initialization
 */
void AST_$INIT(void);
void AST_$ACTIVATE_AOTE_CANNED(uint32_t *attrs, uint32_t *obj_info);

/*
 * AST_$ACTIVATE_CANNED_SEG - activate one segment of a well-known object
 *
 * Takes the AST lock (ML_$LOCK(0x12)), finds the AOTE for the UID, and
 * either creates the object's first ASTE -- initialising its 32-entry
 * segment map at 0x00ED4F80 -- or looks up (creating if need be) the ASTE
 * for the given segment and bumps its reference count.  There is no status
 * parameter: every failure goes to CRASH_SYSTEM.  The ASTE comes back in A0.
 *
 * The first byte of the UID selects the two paths, which is what makes this
 * the "canned" variant: the well-known UIDs all start with a zero byte.
 *
 * Parameters:
 *   uid - UID of the object (by reference)
 *   seg - segment number (a word, pushed with a 2-byte alignment pad)
 *
 * Original address: 0x00E2F1D4 (462 bytes)
 */
aste_t *AST_$ACTIVATE_CANNED_SEG(uid_t *uid, uint16_t seg);

/*
 * Function prototypes - ASTE management
 */
aste_t *AST_$ALLOCATE_ASTE(void);
void AST_$FREE_ASTE(aste_t *aste);
uint16_t AST_$ADD_ASTES(uint16_t *count, status_$t *status);
uint16_t AST_$ADD_AOTES(uint16_t *count, status_$t *status);
/* Request structure for AST_$LOCATE_ASTE */
typedef struct locate_request_t {
  uint32_t uid_high; /* 0x00: UID high word */
  uint32_t uid_low;  /* 0x04: UID low word */
  uint16_t segment;  /* 0x08: Segment number */
  uint16_t hint;     /* 0x0A: ASTE index hint (low 9 bits) */
} locate_request_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(locate_request_t, uid_high) == 0x00, "locate_request_t.uid_high");
_Static_assert(__builtin_offsetof(locate_request_t, uid_low) == 0x04, "locate_request_t.uid_low");
_Static_assert(__builtin_offsetof(locate_request_t, segment) == 0x08, "locate_request_t.segment");
_Static_assert(__builtin_offsetof(locate_request_t, hint) == 0x0A, "locate_request_t.hint");

aste_t *AST_$LOCATE_ASTE(locate_request_t *request);

/*
 * Function prototypes - Page operations
 */
void AST_$PAGE_ZERO(uint32_t ppn);
void AST_$INVALIDATE_PAGE(aste_t *aste, uint32_t *segmap_entry, uint32_t ppn);
void AST_$FREE_PAGES(aste_t *aste, int16_t start_page, int16_t end_page,
                     int16_t flags);
void AST_$RELEASE_PAGES(aste_t *aste, int8_t return_to_pool);
void AST_$FETCH_PMAP_PAGE(void *uid_info, uint32_t *output_buf, uint16_t flags,
                          status_$t *status);

/*
 * MSTE (Memory Segment Table Entry) structure
 * Contains segment mapping information
 */
typedef struct mste_t {
  uid_t uid;           /* 0x00: Object UID */
  uint16_t segment;    /* 0x08: Segment number */
  uint16_t unknown_0a; /* 0x0A: Unknown */
  /* 0x0C: the object's location word, in the aote_t.vol_uid encoding.  The
   * only use in the image is AST_$MSTE_ACTIVATE_AND_WIRE 0x00E02F64
   * (`move.l (0xc,A2),-(SP)`), which hands it to
   * ast_$force_activate_segment as that routine's `location` argument;
   * bead source-sy5u. */
  uint32_t location;   /* 0x0C: object location word */
} mste_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(mste_t, uid) == 0x00, "mste_t.uid");
_Static_assert(__builtin_offsetof(mste_t, segment) == 0x08, "mste_t.segment");
_Static_assert(__builtin_offsetof(mste_t, unknown_0a) == 0x0A, "mste_t.unknown_0a");
_Static_assert(__builtin_offsetof(mste_t, location) == 0x0C, "mste_t.location");

/*
 * Function prototypes - Activation and wiring
 */
aste_t *AST_$MSTE_ACTIVATE_AND_WIRE(mste_t *mste, status_$t *status);
aste_t *AST_$ACTIVATE_AND_WIRE(uid_t *uid, uint16_t seg, status_$t *status);
uint16_t AST_$TOUCH(aste_t *aste, uint32_t mode, uint16_t page, uint16_t count,
                    uint32_t *ppn_array, status_$t *status, uint16_t flags);
/*
 * AST_$TOUCH_AREA (0x00E03548) - fault a run of area pages in.
 *
 * Parameter layout recovered from the prologue; the frame is
 * `link.w A6,-0x70` so the arguments start at A6+0x08:
 *   A6+0x08 word  area_id     0x00E03556 move.w (0x8,A6),D5w; D5*0x30+0xD94BD0
 *                             is the AREA_$ENTRY (its +0x28 partner word
 *                             selects the remote path)
 *   A6+0x0A word  seg_index   0x00E03580 move.w (0xa,A6),D1w; D1<<7 indexes
 *                             PMAP_$SEGMAP (0xED5000)
 *   A6+0x0C word  page        0x00E0355A move.w (0xc,A6),D6w; D6<<2 is the
 *                             byte offset of the first PMAP entry
 *   A6+0x0E long  area_page   0x00E03672/0x00E037AA - the area-relative page
 *                             number (bste*32+seg), stored into the qblk at
 *                             +0x28 and reported to NETLOG as area_page>>5
 *   A6+0x12 long  ppn_array   0x00E035A0 movea.l (0x12,A6),A3 - caller's PPN
 *                             list, filled in by ast_$allocate_pages
 *   A6+0x16 long  status      0x00E0355E movea.l (0x16,A6),A0; clr.l (A0)
 *
 * The caller reserves a 2-byte Pascal result slot at A6+0x1A, but the body
 * never writes it, so this is a procedure.
 */
void AST_$TOUCH_AREA(uint16_t area_id, uint16_t seg_index, int16_t page,
                     uint32_t area_page, uint32_t *ppn_array,
                     status_$t *status);

/*
 * Function prototypes - Association
 */
void AST_$PMAP_ASSOC(aste_t *aste, uint16_t page, uint32_t ppn, uint16_t flags1,
                     uint16_t flags2, status_$t *status);
void AST_$ASSOC(uid_t *uid, uint16_t seg, uint32_t mode, uint16_t page,
                uint16_t flags, uint32_t ppn, status_$t *status);
void AST_$ASSOC_AREA(uint16_t seg_index, int16_t page, uint32_t ppn,
                     status_$t *status);

/*
 * Function prototypes - Copy operations
 */
void AST_$COPY_AREA(uint16_t partner_index, uint16_t unused, aste_t *src_aste,
                    aste_t *dst_aste, uint16_t start_seg, char *buffer,
                    status_$t *status);

/*
 * ============================================================================
 * ast_$common_attr_t - the 0x18-byte "common attributes" summary
 * ============================================================================
 *
 * AST_$GET_COMMON_ATTRIBUTES (0x00E04A00) asks AST_$GET_ATTRIBUTES for the
 * full 0x90-byte attribute record (`link.w A6,-0x90`, buffer at A6-0x90) and
 * distils this out of it.  The big record is a verbatim copy of the AOTE from
 * +0x0C onwards (0x00E049A2: `lea (0xc,A2),A0` + `moveq #0x23` + 36 longword
 * moves), so record offset R corresponds to aote_t offset 0x0C+R and every
 * field below is named after the aote_t member it comes from.
 *
 * The A6 displacements in the listing are relative to A6-0x90, so a
 * displacement d is record offset d+0x90 - getting that wrong is what put the
 * wrong source offsets in ast/get_common_attributes.c before source-pdur.
 */
typedef struct ast_$common_attr_t {
    uint8_t     obj_type;       /* 0x00 <- aote+0x0C  (0x00E04A2C, one long) */
    uint8_t     sub_type;       /* 0x01 <- aote+0x0D */
    uint8_t     attr_flags_hi;  /* 0x02 <- aote+0x0E, then bit 1 is replaced by
                                 *      aote+0x71 bit 5 (0x00E04A76-0x00E04A88)
                                 *      and bit 0 by aote+0x71 bit 4
                                 *      (0x00E04A8C-0x00E04A9C) */
    uint8_t     attr_flags_lo;  /* 0x03 <- aote+0x0F */
    uint32_t    length;         /* 0x04 <- aote+0x20 (0x00E04A30, from record
                                 *      +0x14): the object's length in bytes.
                                 *      DIR_$VALIDATE_HANDLE stores it as the
                                 *      directory's size and substitutes one
                                 *      0x400-byte page when it is zero
                                 *      (0x00E4B550-0x00E4B560).
                                 *      AST_$GET_ATTRIBUTES keeps the larger of
                                 *      the cached and the freshly read value
                                 *      (0x00E048F0-0x00E04918), so it only
                                 *      ever grows.
                                 *      It is a DIFFERENT field from the
                                 *      48-bit length at aote+0x28/+0x2C
                                 *      (attributes 9/0x17/0x1A): the merge at
                                 *      0x00E048F0-0x00E04918 keeps the larger
                                 *      of the two 32-bit values across the
                                 *      0x90-byte attribute refresh, so
                                 *      aote+0x20 only ever grows, and no case
                                 *      in AST_$SET_ATTR_DISPATCH (0x00E04B00)
                                 *      writes it.  Which of the two is
                                 *      authoritative is bead source-traa. */
    uid_t       mod_time;       /* 0x08 <- aote+0x48, attribute 5 */
    uint32_t    blocks;         /* 0x10 <- aote+0x50, attribute 0x0B
                                 *      (0x00E04A36-0x00E04A42 copies 12 bytes
                                 *      from record+0x3C in one dbf loop) */
    uint16_t    refcount;       /* 0x14 <- aote+0x80: the object reference
                                 *      count.  `move.w (-0x1c,A6),(0x14,A2)`
                                 *      at 0x00E04A46; -0x1C+0x90 = record
                                 *      +0x74 = aote+0x80, which is the word
                                 *      AST_$SET_ATTR_DISPATCH's attributes
                                 *      6/7/8 raise and lower
                                 *      (`addq.w #0x1,(0x80,A2)` at 0x00E04CEE
                                 *      and `subq` at 0x00E04D52, both guarded
                                 *      by `cmpi.w #-0xb` = 0xFFF5). */
    int8_t      access_flags;   /* 0x16: bit 7 <- aote+0x71 bit 7 (OS-only
                                 *      access), bit 6 <- aote+0x71 bit 6
                                 *      (0x00E04A4C-0x00E04A72).  A Domain
                                 *      boolean when only bit 7 is consulted. */
    uint8_t     pad_17;         /* 0x17: never written */
} ast_$common_attr_t;

_Static_assert(offsetof(ast_$common_attr_t, sub_type)      == 0x01, "cattr.sub_type");
_Static_assert(offsetof(ast_$common_attr_t, attr_flags_hi) == 0x02, "cattr.attr_flags_hi");
_Static_assert(offsetof(ast_$common_attr_t, attr_flags_lo) == 0x03, "cattr.attr_flags_lo");
_Static_assert(offsetof(ast_$common_attr_t, length)        == 0x04, "cattr.length");
_Static_assert(offsetof(ast_$common_attr_t, mod_time)      == 0x08, "cattr.mod_time");
_Static_assert(offsetof(ast_$common_attr_t, blocks)        == 0x10, "cattr.blocks");
_Static_assert(offsetof(ast_$common_attr_t, refcount)      == 0x14, "cattr.refcount");
_Static_assert(offsetof(ast_$common_attr_t, access_flags)  == 0x16, "cattr.access_flags");
_Static_assert(sizeof(ast_$common_attr_t) == 0x18, "sizeof ast_$common_attr_t");

/* ast_$common_attr_t.access_flags bits (aote_t.access_flags, aote+0x71). */
#define AST_CATTR_OS_ONLY       0x80    /* bit 7 */
#define AST_CATTR_MODE_BIT6     0x40    /* bit 6 */

/* ast_$common_attr_t.attr_flags_hi bits that AST_$GET_COMMON_ATTRIBUTES
 * overwrites from aote+0x71 (rather than leaving as aote+0x0E's own bits). */
#define AST_CATTR_HI_MODE_BIT5  0x02    /* <- aote+0x71 bit 5 */
#define AST_CATTR_HI_MODE_BIT4  0x01    /* <- aote+0x71 bit 4 */

/*
 * The full 0x90-byte record AST_$GET_ATTRIBUTES fills (aote+0x0C onwards).
 * Kept opaque for now; only its size matters to callers.
 */
#define AST_ATTR_REC_SIZE       0x90

/*
 * ast_$acl_attr_t - the 0x38-byte record AST_$GET_ACL_ATTRIBUTES fills from
 * the 0x90-byte attribute record (0x00E04AD6 - 0x00E04AF2):
 *   +0x00 <- attrs[0x00]        (one longword)
 *   +0x04 <- attrs[0x88..0x8F]  (the default-ACL UID)
 *   +0x0C <- attrs[0x48..0x73]  (11 longwords = the 44-byte ACL data block)
 */
typedef struct ast_$acl_attr_t {
    uint8_t     obj_flags[4];   /* 0x00: attrs[0].  Byte 0 selects the
                                 *       ACL_$PRIM_CREATE copy path
                                 *       (0x00E47AAA); byte 3 bit 0 is the
                                 *       "local" test (0x00E479C2). */
    uid_t       default_acl;    /* 0x04: the object's default-ACL UID */
    uint8_t     acl_data[44];   /* 0x0C: the 44-byte ACL data block */
} ast_$acl_attr_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(ast_$acl_attr_t, default_acl) == 0x04, "acl_attr.default_acl");
_Static_assert(offsetof(ast_$acl_attr_t, acl_data)    == 0x0C, "acl_attr.acl_data");
_Static_assert(sizeof(ast_$acl_attr_t) == 0x38, "sizeof ast_$acl_attr_t");
#endif

/*
 * Function prototypes - Attributes
 */
/*
 * AST_$GET_LOCATION (0x00e046c8) takes a 0x20-byte location record, not a
 * bare UID.  The caller stores the object UID at +0x08 and clears bit 6 of
 * the flags byte at +0x1D; on success the routine overwrites all 0x20 bytes
 * with the 8 longwords at aote+0x9C (loop at 0x00e04770).  Argument 3 is a
 * 4-byte cell every caller pea's but the routine never touches; argument 4
 * receives the longword at aote+0x08 (0x00e04766).
 */
#define AST_$LOC_REC_SIZE   0x20
#define AST_$LOC_REC_UID    0x08
#define AST_$LOC_REC_FLAGS  0x1D

void AST_$GET_LOCATION(file_$obj_loc_t *loc_rec, uint16_t flags,
                       uint32_t *unused, uint32_t *location_out,
                       status_$t *status);
/*
 * AST_$GET_ATTRIBUTES (0x00E047A0) takes the SAME 0x20-byte object-location
 * record as AST_$GET_LOCATION, not a bare UID: it reads the object UID from
 * loc_rec+0x08 (`lea (0x8,A4),A0` at 0x00E047D2, `pea (0x8,A4)` at
 * 0x00E047EC / 0x00E04810 / 0x00E04836) and, on every non-error path,
 * overwrites all 0x20 bytes from aote+0x9C (0x00E0492C and 0x00E049B0).
 * It also tests loc_rec+0x1D bit 7 at 0x00E049D6 and fills loc_rec+0x10 with
 * ROUTE_$PORT_ARRAY[0].network when it is zero (0x00E049E8).
 */
void AST_$GET_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags, void *attrs,
                         status_$t *status);
void AST_$GET_COMMON_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags,
                                ast_$common_attr_t *attrs, status_$t *status);
/*
 * AST_$GET_ACL_ATTRIBUTES (0x00e04aaa) is a thin wrapper over
 * AST_$GET_ATTRIBUTES: it takes the same 0x20-byte object-location record,
 * runs the lookup into a private 0x90-byte attribute buffer and then copies
 * three slices of it into the caller's ast_$acl_attr_t.
 */
void AST_$GET_ACL_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags,
                             ast_$acl_attr_t *acl, status_$t *status);
void AST_$SET_ATTRIBUTE(uid_t *uid, uint16_t attr_id, void *value,
                        status_$t *status);
/*
 * AST_$SET_ATTR (0x00e05400).  `value` is a POINTER: 0x00E05450 forwards the
 * longword at A6+0x0E to ast_$set_attribute_internal, which dereferences it
 * at 0x00E05320.  `clock` is likewise a pointer to a 6-byte clock_t
 * (0x00E05224 / 0x00E05228).
 */
void AST_$SET_ATTR(uid_t *uid, int16_t attr_id, void *value, uint8_t flags,
                   clock_t *clock, status_$t *status);
void AST_$GET_DTV(uid_t *uid, uint32_t location, uint32_t *dtv,
                  status_$t *status);
uint8_t AST_$SET_DTS(uint16_t flags, uid_t *uid, uint32_t *dtv,
                     uint32_t *access_time, status_$t *status);

/*
 * Function prototypes - Object operations
 */
void AST_$LOAD_AOTE(uint32_t *attrs, uint32_t *obj_info);
uint16_t AST_$PURIFY(uid_t *uid, uint16_t flags, int16_t segment,
                     uint32_t *segment_list, uint16_t unused,
                     status_$t *status);
void AST_$COND_FLUSH(uid_t *uid, uint32_t *timestamp, status_$t *status);
void AST_$TRUNCATE(uid_t *uid, uint32_t new_size, uint16_t flags,
                   uint8_t *result, status_$t *status);
void AST_$INVALIDATE(uid_t *uid, uint32_t start_page, uint32_t count,
                     int16_t flags, status_$t *status);
void AST_$RESERVE(uid_t *uid, uint32_t start_byte, uint32_t byte_count,
                  status_$t *status);
void AST_$DISMOUNT(uint16_t vol_index, uint8_t flags, status_$t *status);
/*
 * AST_$GET_SEG_MAP (0x00e06b1e).  Arguments 4 and 5 are longword *values*,
 * not pointers: every caller pushes them with `pea (imm).w`
 * (0x00E4BB12/0x00E4BB16) and the routine reads them with
 * `move.l (0x14,A6),D1` (0x00E06B44) and `cmpi.l #0x20,(0x18,A6)`
 * (0x00E06B66).
 */
void AST_$GET_SEG_MAP(uid_t *uid, uint32_t start_offset,
                      uint32_t unused, uint32_t seg_count, uint32_t map_size,
                      uint16_t flags, uint32_t *output, status_$t *status);

/*
 * Function prototypes - Page allocation
 */
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

/*
 * Function prototypes - Maintenance
 */
void AST_$UPDATE(void);
uint32_t AST_$GET_DISM_SEQN(void);
void AST_$SET_TROUBLE(uid_t **uid_ptr);
void AST_$SAVE_CLOBBERED_UID(uid_t *uid);
uint8_t AST_$REMOVE_CORRUPTED_PAGE(uint32_t ppn);

/*
 * Function prototypes - Synchronization
 */
void AST_$WAIT_FOR_AST_INTRANS(void);

#endif /* AST_H */
