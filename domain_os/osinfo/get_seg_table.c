/*
 * OSINFO_$GET_SEG_TABLE - copy the AST or AOT to the caller
 *
 * Original address: 0x00E5C5C4
 * Size: 208 bytes (0x00E5C5C4 .. 0x00E5C693)
 *
 * Reports AST_$SIZE_AST (0xE1E0F0) as the total, then copies up to that
 * many entries - the caller's maximum if it is smaller, with the status
 * "array too small" - from either the AST (type 2, 0x14-byte entries at
 * 0xEC5400, map symbol AST) or the AOT (type 1, 0x80-byte entries at
 * 0xED5000, map symbol AST_PMAPS / VM_TABLES).  Any other type copies
 * nothing.
 *
 * Frame (link.w A6,-0x4; D2/D3/A2-A4 saved):
 *   (0x8,A6)   type_ptr           pointer to a word -> D0
 *   (0xc,A6)   buffer             -> D1
 *   (0x10,A6)  max_entries_ptr    pointer to a word -> A0
 *   (0x14,A6)  actual_entries_ptr pointer to a word -> A2
 *   (0x18,A6)  total_entries_ptr  pointer to a word -> A3
 *   (0x1c,A6)  status             pointer -> A1, cleared first
 *
 *   0x00E5C5E6  move.w AST_$SIZE_AST,(A3)
 *   0x00E5C5EC  move.w (A0),D2w / beq -> done              max 0: nothing
 *   0x00E5C5F2  move.w AST_$SIZE_AST,D3w / cmp.w D3w,D2w / blt   (signed)
 *   0x00E5C5FC    move.w D3w,(A2)                          max >= size: size
 *   0x00E5C600    move.w D2w,(A2) / move.l #0x200001,(A1)  max <  size: max
 *   0x00E5C60A  cmpi.w #2,(A4)                              AST:
 *   0x00E5C610    move.w (A2),D0w / subq.w #1 / bmi         count - 1 as dbf
 *   0x00E5C616    A0 = 0xEC5400 + 0x14, A1 = buffer + 0x14; per entry
 *                 A4 = A0 - 0x14, A2 = A1 - 0x14, five longwords, +0x14
 *   0x00E5C648  cmpi.w #1,(A4)                              AOT:
 *   0x00E5C65A    A0 = 0xED5000 + 0x80, A1 = buffer + 0x80; per entry
 *                 32 longwords, +0x80
 *
 * Re-emitted from the disassembly 2026-09-27: the previous C copied the
 * AOT from AST_GLOBALS_BASE (0xE1DC80) instead of 0xED5000.
 */

#include "osinfo/osinfo_internal.h"
#include "ast/ast.h"
#include "pmap/pmap.h"   /* PMAP_$SEGMAP */

/* `movea.l #0xec5400,A3` at 0x00E5C616: the AST, 0x14-byte entries */
#define OSINFO_AST_BASE_VA      0x00EC5400u
/*
 * `movea.l #0xed5000,A3` at 0x00E5C65A: the 0x80-byte entries at map
 * AST_PMAPS, which is the MODULE_DATA block PMAP_$SEGMAP (pmap/pmap.h,
 * source-iq58) - linked in map order, so the image literal would name
 * different memory; row 0 is the image's 0xED5000.
 */
#define OSINFO_AOT_BASE         ((const uint32_t *)&PMAP_$SEGMAP.row[0][0])

void OSINFO_$GET_SEG_TABLE(short *type_ptr, void *buffer,
                           short *max_entries_ptr, short *actual_entries_ptr,
                           short *total_entries_ptr, status_$t *status)
{
    int16_t max_entries;            /* D2w */
    int16_t size_ast;               /* D3w */
    int16_t count;                  /* D0w, the dbf counter */
    int16_t j;
    const uint32_t *src;
    uint32_t *dst;

    /* 0x00E5C5E0 .. 0x00E5C5E6 */
    *status = status_$ok;
    *total_entries_ptr = (short)AST_$SIZE_AST;

    /* 0x00E5C5EC .. 0x00E5C5EE */
    max_entries = *max_entries_ptr;
    if (max_entries == 0) {
        return;
    }

    /* 0x00E5C5F2 .. 0x00E5C602: signed compare */
    size_ast = (int16_t)AST_$SIZE_AST;
    if (max_entries < size_ast) {
        *actual_entries_ptr = max_entries;
        *status = status_$os_info_array_too_small;
    } else {
        *actual_entries_ptr = size_ast;
    }

    /* 0x00E5C608 .. 0x00E5C60E */
    if (*type_ptr == SEG_TABLE_TYPE_AST) {
        /* 0x00E5C610 .. 0x00E5C614 */
        count = (int16_t)(*actual_entries_ptr - 1);
        if (count < 0) {
            return;
        }
        src = (const uint32_t *)ARCH_VA_TO_PTR(OSINFO_AST_BASE_VA);
        dst = (uint32_t *)buffer;
        /* 0x00E5C628 .. 0x00E5C642: count + 1 entries of five longwords */
        for (; count != -1; count--) {
            for (j = 4; j != -1; j--) {
                *dst++ = *src++;
            }
        }
        return;
    }

    /* 0x00E5C648 .. 0x00E5C64C */
    if (*type_ptr == SEG_TABLE_TYPE_AOTE) {
        /* 0x00E5C64E .. 0x00E5C652 */
        count = (int16_t)(*actual_entries_ptr - 1);
        if (count < 0) {
            return;
        }
        src = OSINFO_AOT_BASE;
        dst = (uint32_t *)buffer;
        /* 0x00E5C66C .. 0x00E5C686: count + 1 entries of 32 longwords */
        for (; count != -1; count--) {
            for (j = 0x1f; j != -1; j--) {
                *dst++ = *src++;
            }
        }
    }
}
