/*
 * dir_$map_page - Map a directory data page through the 2-slot LRU
 *
 * Maps one 0x400-byte page of a directory's data.  The handle carries two
 * cache slots, each covering a GROUP of 32 pages (0x8000 bytes); the group
 * number is page_idx >> 5 and the page's offset inside the group is
 * (page_idx & 0x1F) << 10.
 *
 * On a miss in the current slot the routine toggles dir_$handle_t.cur_slot
 * and tries the other one; on a full miss it evicts a slot - never the one
 * dir_$handle_t.max_slots names, which is where DIR_$WIRE_PAGE pinned a
 * page - and remaps it with MST_$REMAP_PRIVI.
 *
 * Returns the page's virtual address.
 *
 * Original address: 0x00E4B340
 * Original size: 260 bytes
 */

#include "dir/dir_internal.h"

/*
 * 0x00E4B448, the longword 0x00008000 that follows DIR_$CONST_ONE_W in the
 * DIR code region.  Image bytes: 00 00 80 00.  It is the size of one cache
 * group and dir_$map_page is its only reader, handing it to
 * MST_$REMAP_PRIVI twice - as config2 (`pea (0x60,PC)` at 0x00E4B3E6) and as
 * config3 (`pea (0x74,PC)` at 0x00E4B3D2).  Both callees read it as a
 * longword.  (Ghidra labelled the cell by its address, 0x00E4B448.)  source-ka0m.
 */
static const uint32_t dir_$map_seg_len_00e4b448 = 0x00008000;

/* `cmpi.l #0x8000,D3` at 0x00E4B416 - the size the remap must report. */
#define DIR_MAP_GROUP_SIZE      0x8000

/* page_idx >> 5 selects the group; the low 5 bits index within it. */
#define DIR_MAP_GROUP_SHIFT     5
#define DIR_MAP_PAGE_MASK       0x1F

void *dir_$map_page(void *handle, int16_t page_idx)
{
    dir_$handle_t *h = (dir_$handle_t *)handle;     /* A0 */
    int16_t   slot;
    uint32_t  group;
    uint32_t  offset;

    /*
     * 0x00E4B350-0x00E4B364.  `clr.l D0` / `move.w D2w,D0w` / `lsr.l #0x5,D0`
     * zero-extends page_idx to a longword before the shift, so a negative
     * page_idx still yields a small positive group number.
     */
    slot  = h->cur_slot;
    group = (uint32_t)(uint16_t)page_idx >> DIR_MAP_GROUP_SHIFT;

    if (DIR_HANDLE_CACHE_GROUP(h, slot) == (uint16_t)group) {
        /* 0x00E4B366-0x00E4B376: hit in the current slot.  The offset is
         * computed in a WORD (`lsl.w #0x8` then `lsl.w #0x2`) and then
         * zero-extended. */
        offset = (uint32_t)(uint16_t)((page_idx & DIR_MAP_PAGE_MASK) << 10);
        return ARCH_VA_TO_PTR(DIR_HANDLE_CACHE_BASE(h, slot) + offset);
    }

    /* 0x00E4B37C: `bchg.b #0x0,(0x1f,A0)` toggles bit 0 of the cur_slot
     * word, i.e. flips between the two slots. */
    h->cur_slot ^= 1;
    slot = h->cur_slot;

    if (DIR_HANDLE_CACHE_GROUP(h, slot) == (uint16_t)group) {
        /* 0x00E4B392-0x00E4B3A2: hit in the other slot. */
        offset = (uint32_t)(uint16_t)((page_idx & DIR_MAP_PAGE_MASK) << 10);
        return ARCH_VA_TO_PTR(DIR_HANDLE_CACHE_BASE(h, slot) + offset);
    }

    /*
     * 0x00E4B3A8-0x00E4B3B2: full miss.  If the slot we just toggled to is
     * the one holding a wired page, toggle back and evict the other one.
     */
    if (h->cur_slot == h->max_slots) {
        h->cur_slot ^= 1;
    }
    slot = h->cur_slot;

    {
        /* 0x00E4B3C2-0x00E4B3C6: this shift is a WORD `lsr.w #0x5`. */
        uint16_t  grp16 = (uint16_t)page_idx >> DIR_MAP_GROUP_SHIFT;
        /* 0x00E4B3D6-0x00E4B3E2: `lsl.l #0x8` then `lsl.l #0x7`. */
        uint32_t  map_addr = (uint32_t)grp16 << 15;
        uint32_t  result;               /* A6-0x08 */
        status_$t status;               /* A6-0x0C */

        DIR_HANDLE_CACHE_GROUP(h, slot) = grp16;

        /* 0x00E4B3CA-0x00E4B3FC.  The va_ptr argument is the slot's own base
         * cell, which the callee reads in and the A0 result overwrites. */
        DIR_HANDLE_CACHE_BASE(h, slot) = ARCH_PTR_TO_VA(
            MST_$REMAP_PRIVI(&DIR_$CONST_ONE_W,
                             &DIR_HANDLE_CACHE_BASE(h, slot),
                             (void *)&dir_$map_seg_len_00e4b448,
                             &map_addr,
                             (void *)&dir_$map_seg_len_00e4b448,
                             &result,
                             &status));

        /* 0x00E4B404-0x00E4B414 */
        if (status != status_$ok) {
            CRASH_SYSTEM(&status);
        }

        /* 0x00E4B416-0x00E4B426 */
        if (result != DIR_MAP_GROUP_SIZE) {
            CRASH_SYSTEM(&Naming_bad_request_header_ver_err);
        }

        /* 0x00E4B428-0x00E4B438 */
        offset = (uint32_t)(uint16_t)((page_idx & DIR_MAP_PAGE_MASK) << 10);
        return ARCH_VA_TO_PTR(DIR_HANDLE_CACHE_BASE(h, slot) + offset);
    }
}
