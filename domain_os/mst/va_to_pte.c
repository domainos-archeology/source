/*
 * mst_$va_to_pte - Look up the MST page-table entry for a virtual address
 *
 * Original address: 0x00E4411C, 174 bytes (0x00E4411C-0x00E441C8).
 *
 * Parameter block (frame is `link.w A6,-0x10`, arguments from A6+0x08):
 *   +0x08 asid       word      0x00E4412A  move.w  (0x8,A6),-(SP)
 *   +0x0A va         longword  0x00E44132  move.l  (0xa,A6),-(SP)
 *   +0x0E prot_out   longword  0x00E441B6  movea.l (0xe,A6),A1
 *   +0x12 entry_out  longword  0x00E44190  movea.l (0x12,A6),A3
 *   +0x16 status     longword  0x00E44126  movea.l (0x16,A6),A2
 *
 * Body:
 *   0x00E44124-0x00E4413C  Pascal result slot (`subq.l #2,SP`) then the three
 *                          arguments of MST_$VA_TO_SEGNO(va, &local_seg, asid);
 *                          the segment number comes back in D0w and the
 *                          in-segment index in the local at (-0xe,A6).
 *   0x00E44144  cmpi.w #0x39,D0w / bhi -> fail   (segno must be <= 0x39)
 *   0x00E44152  A0 = MST_ASID_BASE (0xE243D4), A1 = MST (0xEE5800)
 *   0x00E4415E  D3w = (local_seg >> 6) + MST_ASID_BASE[segno]
 *   0x00E44166  D0w = D3w * 2, the BYTE offset into MST
 *   0x00E44168  tst.w (A1,D0w) / beq -> fail     (no page-table page)
 *   0x00E44176  D3 = MST[..] zero-extended to a longword (andi.l #0xffff)
 *   0x00E4417A  A1 = MSTE_PAGES (0xEF6400)
 *   0x00E44186  D3 <<= 8 then <<= 2, i.e. page * 0x400
 *   0x00E4418A  A1 += D3
 *   0x00E4418E  D3w = (local_seg & 0x3f) << 4, the 16-byte entry offset
 *   0x00E44198  A1 += D3w   (word index, sign-extended)
 *   0x00E4419C  lea (-0x400,A1),A1
 *
 * The `-0x400` at 0x00E4419C is not a fixup on the store: it is applied to
 * the entry address itself, before it is stored, tested and read.  The word
 * MST holds is therefore a ONE-BASED page-table page number, and the entry
 * lives at
 *
 *     MSTE_PAGES + (page - 1) * 0x400 + (local_seg & 0x3f) * 16
 *
 * (bead source-ylxp: the C previously formed the address with the base
 * 0xEF6000 - i.e. 0xEF6400-0x400 - and then subtracted 0x400 a second time,
 * so entry_out, the validity test and the protection byte were all one
 * page-table page too low.)
 *
 * On success  *entry_out = the entry address, *prot_out = (byte[0x0a] & 0x3e) >> 1,
 * *status = 0.  On failure *status = status_$reference_to_illegal_address and,
 * when the entry itself is empty, *entry_out is cleared (0x00E441AC clr.l (A3)).
 */

#include "mst/mst_internal.h"
#include "ml/ml.h"

/*
 * Base of the page-table-entry pages.  `movea.l #0xef6400,A1` at 0x00E4417A;
 * the SAU2 map names this object MSTE_PAGES at EF6400.
 */
#define MSTE_PAGES_BASE MST_PAGE_TABLE_BASE

/* Offset of the protection byte inside a 16-byte entry: `and.b (0xa,A1),D0b` */
#define MSTE_PROT_BYTE_OFF 0x0A

void mst_$va_to_pte(uint16_t asid, uint32_t va, uint16_t *prot_out,
                    void **entry_out, status_$t *status)
{
    uint16_t local_seg;   /* (-0xe,A6) */
    uint16_t segno;       /* D0w out of MST_$VA_TO_SEGNO */
    int16_t dir_index;    /* D3w */
    int16_t mst_byte_off; /* D0w, byte offset into MST */
    uint16_t page_entry;  /* D3w reloaded at 0x00E44176 */
    uint32_t entry_va;    /* A1 */
    uint8_t *entry;

    /* 0x00E44124-0x00E4413C */
    local_seg = 0;
    segno = MST_$VA_TO_SEGNO(va, &local_seg, asid);

    /* 0x00E44144 cmpi.w #0x39,D0w / bhi.b 0x00E4416E */
    if (segno > 0x39) {
        *status = status_$reference_to_illegal_address; /* 0x00E4416E */
        return;
    }

    /* 0x00E4414C-0x00E44162: D3w = (local_seg >> 6) + MST_ASID_BASE[segno] */
    dir_index = (int16_t)((int16_t)(local_seg >> 6) +
                          (int16_t)MST_ASID_BASE[segno]);

    /* 0x00E44164-0x00E44166: D0w = D3w * 2 (word arithmetic, then sign-extended
     * by the (0x0,A1,D0w*0x1) addressing mode) */
    mst_byte_off = (int16_t)(dir_index * 2);

    /* 0x00E44168 tst.w (0x0,A1,D0w*0x1) / bne.b 0x00E44176 */
    page_entry = MST[mst_byte_off / 2];
    if (page_entry == 0) {
        *status = status_$reference_to_illegal_address; /* 0x00E4416E */
        return;
    }

    /* 0x00E4417A-0x00E4419C: MSTE_PAGES + page*0x400 + (seg & 0x3f)*16 - 0x400 */
    entry_va = MSTE_PAGES_BASE;
    entry_va += (uint32_t)page_entry * 0x400u;
    entry_va += (uint32_t)(int32_t)(int16_t)((local_seg & 0x3Fu) << 4);
    entry_va -= 0x400u;

    entry = (uint8_t *)ARCH_VA_TO_PTR(entry_va);

    /* 0x00E441A0 move.l A1,(A3) */
    *entry_out = entry;

    /* 0x00E441A2 tst.l (A1) */
    if (*(const uint32_t *)(const void *)entry == 0) {
        *status = status_$reference_to_illegal_address; /* 0x00E441A6 */
        *entry_out = (void *)0;                         /* 0x00E441AC clr.l (A3) */
        return;
    }

    /* 0x00E441B0-0x00E441BC: moveq #0x3e / and.b (0xa,A1) / lsr.w #1 */
    *prot_out = (uint16_t)((entry[MSTE_PROT_BYTE_OFF] & 0x3E) >> 1);

    /* 0x00E441BE clr.l (A2) */
    *status = status_$ok;
}
