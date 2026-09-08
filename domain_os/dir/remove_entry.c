/*
 * dir_$remove_entry - Remove an entry from a directory by name
 *
 * Original address: 0x00E50FC8
 * Original size: 530 bytes (0x00E50FC8-0x00E511D9)
 *
 * Re-derived from the disassembly (bead source-05f8).  The routine has one
 * nested Pascal subprocedure, 0x00E50D5E, which 0x00E5108E calls with
 * `movea.l A6,A1` - the parent frame as the static link.  It is flattened
 * below into dir_$remove_entry_from_page with explicit uplevel arguments; the
 * only cell it writes that its caller reads back is the did_truncate byte at
 * A6-0x60, which becomes an out-parameter.
 */

#include "dir/dir_internal.h"

/*
 * ============================================================================
 * dir_$remove_entry_from_page - nested subprocedure at 0x00E50D5E (618 bytes)
 * ============================================================================
 *
 * Own parameters:  (0x08,A6) page_idx word, (0x0a,A6) status pointer -> A2.
 * Static link A1 -> A3 = dir_$remove_entry's frame, from which it uses:
 *   (0x08,A3)             the handle argument
 *   (-0x34,A3)+4*n        the B-tree path (dir_$page_path_t, 1-based)
 *   (-0x34,A3)            doubles as the AST_$PURIFY segment-list cell
 *   (-0x60,A3)            the did_truncate byte
 *   (-0x10,A3)            an 8-byte UID copy
 *   (-0x62,A3) (-0x5a,A3) (-0x58,A3) (-0x56,A3) (-0x50,A3) (-0x4c,A3)
 *   (-0x48,A3) (-0x44,A3) (-0x40,A3)   scratch the caller never reads back
 *                                      (-0x50 aliases the caller's entry
 *                                      pointer, which it has finished with)
 * The scratch cells become ordinary locals here.
 */
static void dir_$remove_entry_from_page(void *handle, dir_$page_path_t *path,
                                        int16_t page_idx, int8_t *did_truncate,
                                        status_$t *status_ret)
{
    dir_page_hdr_t *page;           /* A4 */
    dir_page_hdr_t *page0;          /* A3-0x40 */
    uint16_t       *tab;            /* A3-0x48: the index table */
    uint8_t        *entry;          /* A3-0x4c */
    uint8_t        *saved_entry;    /* A3-0x50 */
    uint16_t        hdr_size;       /* A3-0x56 */
    int16_t         n_entries;      /* A3-0x58 */
    int16_t         entry_idx;      /* A3-0x5a */
    int8_t          is_leaf;        /* A3-0x62 - Domain boolean */
    uid_t           dir_uid;        /* A3-0x10 */
    status_$t       sub_status;     /* A3-0x44 */
    uint32_t        purify_seg;     /* A3-0x34, index 0 of the path buffer */
    int16_t         i;
    int16_t         count;

    *status_ret = status_$ok;       /* 0x00E50D70: clr.l (A2) */

    /* 0x00E50D74: map the page this path level names */
    page = (dir_page_hdr_t *)dir_$map_page(handle,
                                           (int16_t)path[page_idx - 1].page_no);

    if (page->page_no == 0) {
        /* 0x00E50D90: the root page carries a root area of its own */
        page0 = page;
        hdr_size = (uint16_t)(DIR_PAGE_HDR_SIZE +
                              *(uint16_t *)((uint8_t *)page0 +
                                            DIR_PAGE_ROOT_AREA_LEN));
    } else {
        hdr_size = DIR_PAGE_HDR_SIZE;       /* 0x00E50DA4 */
    }

    /* 0x00E50DAA: signed (index_end - hdr_size) / 2 */
    n_entries = (int16_t)(((int32_t)(int16_t)page->index_end -
                           (int32_t)(int16_t)hdr_size) / 2);

    if (page->page_no != 0 && n_entries == 1) {
        /*
         * 0x00E50DD0: the last entry of a non-root page - remove the page's
         * own slot from the level above first, then throw the page away.
         */
        dir_$remove_entry_from_page(handle, path, (int16_t)(page_idx - 1),
                                    did_truncate, status_ret);
        if (*status_ret != status_$ok) {
            return;                         /* 0x00E50DE2 */
        }
        /* 0x00E50DE6 */
        dir_uid = *(uid_t *)handle;
        /*
         * 0x00E50DF2.  AST_$INVALIDATE reads its fourth argument as a BYTE
         * (`move.b (0x14,A6),D3b` at 0x00E06644) and the image supplies it
         * with `st -(SP)`, i.e. 0xFF in the word's high byte.
         */
        AST_$INVALIDATE(&dir_uid, (uint32_t)path[page_idx - 1].page_no, 1,
                        (int16_t)0xFFFF, &sub_status);
        return;                             /* 0x00E50E10 */
    }

    /* 0x00E50E14 */
    entry_idx = (int16_t)path[page_idx - 1].entry_idx;
    tab = (uint16_t *)((uint8_t *)page + hdr_size);
    if (entry_idx > n_entries) {
        CRASH_SYSTEM(&Naming_bad_request_header_ver_err);    /* 0x00E50E30 */
    }
    /* 0x00E50E3C: the table is 1-based - tab[entry_idx-1] */
    entry = (uint8_t *)page + tab[entry_idx - 1];

    /* 0x00E50E52: kind 1 == leaf */
    is_leaf = (((page->kind & DIR_PAGE_KIND_MASK) >> DIR_PAGE_KIND_SHIFT) == 1)
                  ? (int8_t)0xFF : (int8_t)0x00;

    DIR_$WIRE_PAGE(handle, page);           /* 0x00E50E64 */

    if (page->page_no == 0 && n_entries == 1) {
        /*
         * 0x00E50E82: the root page just lost its last entry - reinitialise
         * it as an empty leaf.
         */
        uint16_t heap;

        if (is_leaf >= 0) {
            CRASH_SYSTEM(&Naming_bad_request_header_ver_err);    /* 0x00E50E88 */
        }
        page->kind = 0;                                  /* 0x00E50E94 clr.w (A4) */
        page->version = 0;
        page->version = (uint8_t)((page->version & 0xC0) | 0x05);
        page->kind = (uint8_t)(page->kind & 0x3F);
        page->index_end = hdr_size;                      /* 0x00E50EA6 */
        page->heap_base = DIR_PAGE_SIZE;                 /* 0x00E50EAC */
        heap = (uint16_t)((page->heap_base - 0x0E) & 0xFFFC);
        page->heap_base = heap;
        tab[0] = heap;                                   /* 0x00E50EC2 */
        page->index_end = (uint16_t)(page->index_end + 2);
        entry = (uint8_t *)page + heap;
        *(uint16_t *)entry = 0;                          /* 0x00E50ED8 */
        entry[0] = (uint8_t)((entry[0] & 0xF8) | 0x04);
        entry[1] = 1;                                    /* 0x00E50EEA */
        *(uint16_t *)(entry + 2) = 0;                    /* 0x00E50EF4 */
        *(uint16_t *)(entry + 4) = 0xFFFF;               /* 0x00E50EFC */
        entry[0x0c] = 0;                                 /* 0x00E50F06 */
    } else {
        if (is_leaf < 0 && entry_idx == 1) {
            /*
             * 0x00E50F1A: the first entry of a leaf carries the page's
             * separator word; hand it to the entry that will take its place.
             */
            saved_entry = entry;
            entry = (uint8_t *)page + tab[1];
            *(uint16_t *)(saved_entry + 2) = *(uint16_t *)(entry + 2);
            entry_idx = 2;                               /* 0x00E50F3E */
        }
        entry[0] |= 0x80;                                /* 0x00E50F44 */

        /* 0x00E50F4C: close the hole in the index table */
        count = (int16_t)(n_entries - 1 - entry_idx);
        if (count >= 0) {
            i = entry_idx;
            do {
                tab[i - 1] = tab[i];                     /* 0x00E50F6E */
                i++;
                count--;
            } while (count != -1);                       /* dbf */
        }
        page->index_end = (uint16_t)(page->index_end - 2);   /* 0x00E50F7C */
        page->kind |= 0x20;                              /* 0x00E50F80 bset #5 */
    }

    dir_$release_wire(handle);                           /* 0x00E50F84 */

    if (is_leaf >= 0) {
        return;                                          /* 0x00E50F92 */
    }

    *did_truncate = (int8_t)0xFF;                        /* 0x00E50F94: st */
    purify_seg = page->page_no;                          /* 0x00E50F9E */
    AST_$PURIFY((uid_t *)handle, 0x0012, 0, &purify_seg, 1, status_ret);
}

/*
 * dir_$remove_entry - Remove an entry from a directory by name
 *
 * Parameters:
 *   handle     - (0x08,A6) directory handle; its first eight bytes are the
 *                directory UID, which is what AST_$PURIFY / AST_$TRUNCATE get
 *   name       - (0x0c,A6) entry name
 *   name_len   - (0x10,A6) name length WORD
 *   op_type    - (0x12,A6) -> D2, operation type WORD
 *   uid_ret    - (0x14,A6) -> A2, output: UID of a type 2/3 entry
 *   status_ret - (0x18,A6) -> A3, output: status code
 */
void dir_$remove_entry(void *handle, void *name, int16_t name_len,
                       int16_t op_type, void *uid_ret, status_$t *status_ret)
{
    /* link.w A6,-0x64 */
    uid_t            dir_uid;           /* A6-0x10 */
    dir_$page_path_t path[8];           /* A6-0x30..A6-0x11, 1-based: path[N-1] */
    uint32_t         purify_seg;        /* A6-0x34 */
    uint8_t         *page0;             /* A6-0x40 */
    status_$t        sub_status;        /* A6-0x44 */
    void            *entry_ret;         /* A6-0x50 */
    uint8_t          trunc_result;      /* A6-0x5e */
    int16_t          depth;             /* A6-0x5c */
    int8_t           did_truncate;      /* A6-0x60 */
    uint8_t         *entry_ptr;
    uint32_t        *uid_out = (uint32_t *)uid_ret;
    char             found;
    int16_t          level;             /* D3 */
    int16_t          link_page;         /* D2, reused */
    uint32_t         new_size;          /* D2, reused again */
    int16_t          last_page;

    /*
     * 0x00E50FDC.  The word 8 is the CAPACITY of the path buffer:
     * dir_$find_entry compares it against the level it is about to record
     * (`cmp.w (0x12,A6),D3w` / `bgt` at 0x00E4CAE0) and crashes past it.
     * Level N lands at extra+(N-1)*4 (0x00E4CAEE), so path[] is 1-based.
     */
    found = dir_$find_entry(handle, name, name_len, 8, &entry_ret, path,
                            &depth);
    level = depth;                      /* 0x00E51000: D3 */
    entry_ptr = (uint8_t *)entry_ret;

    if (found >= 0) {
        *status_ret = status_$naming_name_not_found;    /* 0x00E51008 */
        return;
    }

    /* 0x00E51012: types 2 and 3 hand their UID back */
    if ((*entry_ptr & 7) == 2 || (*entry_ptr & 7) == 3) {
        uid_out[0] = *(uint32_t *)(entry_ptr + 4);
        uid_out[1] = *(uint32_t *)(entry_ptr + 8);
    }

    *status_ret = status_$ok;           /* 0x00E51034: clr.l (A3) */

    /* 0x00E51036: validate the entry type against the requested operation */
    if (op_type != 0) {
        if (op_type == 4) {
            if ((*entry_ptr & 7) != 4) {
                *status_ret = status_$naming_not_a_link;        /* 0x00E5104C */
            }
        } else {
            if ((*entry_ptr & 7) == 4) {
                *status_ret = status_$naming_invalid_link_operation; /* 0x00E51060 */
            }
        }
    }
    if (*status_ret != status_$ok) {    /* 0x00E51066: tst.l (A3) */
        return;
    }

    /* 0x00E5106C: a type-4 entry names an overflow page */
    if ((*entry_ptr & 7) == 4) {
        link_page = *(int16_t *)(entry_ptr + 4);
    } else {
        link_page = -1;                 /* 0x00E51080 */
    }

    did_truncate = 0;                   /* 0x00E51084: clr.b (-0x60,A6) */
    dir_$remove_entry_from_page(handle, path, level, &did_truncate,
                                status_ret);            /* 0x00E51090 */

    if (link_page != -1 && *status_ret == status_$ok) {
        if (did_truncate >= 0) {        /* 0x00E510A0: bmi skips this */
            /* 0x00E510A6: the path entry for this level, zero-extended into
             * the segment-list cell just below the path buffer. */
            purify_seg = path[level - 1].page_no;
            AST_$PURIFY((uid_t *)handle, 0x0012, 0, &purify_seg, 1, status_ret);
        }
        if (*status_ret == status_$ok) {                /* 0x00E510DA */
            dir_uid = *(uid_t *)handle;                 /* 0x00E510DE */
            /* 0x00E510EA - see the note on the byte-wide fourth argument in
             * dir_$remove_entry_from_page. */
            AST_$INVALIDATE(&dir_uid, (uint32_t)(uint16_t)link_page, 1,
                            (int16_t)0xFFFF, &sub_status);
        }
    }

    /*
     * 0x00E5110A-0x00E511D0: only a removal that emptied a leaf page (the
     * nested procedure's `st (-0x60,A3)`) can shrink the directory.
     */
    if (did_truncate >= 0 || *status_ret != status_$ok) {
        return;
    }

    /* 0x00E51118: the handle's byte length at +0x10, in pages, minus one */
    last_page = (int16_t)((*(uint32_t *)((uint8_t *)handle + 0x10) >> 10) - 1);

    /* 0x00E51126: walk back to the last page that still has a page number */
    for (;;) {
        dir_page_hdr_t *p =
            (dir_page_hdr_t *)dir_$map_page(handle, last_page);
        if (p->kind != 0 || p->version != 0) {
            break;                      /* 0x00E51134: tst.w (A0) / bne */
        }
        if (last_page == 0) {
            CRASH_SYSTEM(&Naming_bad_request_header_ver_err);   /* 0x00E5113C */
        }
        last_page--;                    /* 0x00E51148 */
    }

    /* 0x00E5114C: (last_page + 1) pages, plus one spare unless that is
     * already the whole directory */
    new_size = ((uint32_t)(uint16_t)last_page + 1) << 10;
    if (new_size != DIR_PAGE_SIZE) {
        new_size += DIR_PAGE_SIZE;
    }
    /* 0x00E51164: shrink only when it saves at least 0x1000 bytes */
    if (new_size == DIR_PAGE_SIZE ||
        (int32_t)(*(uint32_t *)((uint8_t *)handle + 0x10) - new_size) >= 0x1000) {
        /* 0x00E5117A */
        AST_$TRUNCATE((uid_t *)handle, new_size, 0, &trunc_result,
                      &sub_status);
        if (sub_status == status_$ok) {
            *(uint32_t *)((uint8_t *)handle + 0x10) = new_size;  /* 0x00E511A0 */
        }
    }

    /* 0x00E511A4: bit 3 of page 0's first byte records "more than one page" */
    page0 = (uint8_t *)dir_$map_page(handle, 0);
    page0[0] = (uint8_t)((page0[0] & 0xF7) |
                         ((*(uint32_t *)((uint8_t *)handle + 0x10) !=
                           DIR_PAGE_SIZE) ? 0x08 : 0x00));
}
