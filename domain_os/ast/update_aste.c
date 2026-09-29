/*
 * ast_$update_aste - Write a dirty segment's file map back
 *
 * Nothing is written for an ASTE that is not DIRTY (flags bit 13) or is
 * REMOTE (bit 11); the status is cleared.  Otherwise DIRTY is cleared
 * and, under the PMAP lock, the 32 map entries are turned into the
 * on-disk form: the disk address (from the entry, or from the MMAPE of
 * an installed page) with bit 31 set when the entry's / the MMAPE
 * address's bit 22 is set.  The image is logged when NETLOG is on and
 * handed to FM_$WRITE with the segment's file-map block and the caller's
 * write-now byte.  "disk write protected" is swallowed; any other
 * failure sets bit 31 of the status and restores DIRTY.
 *
 * Parameters (frame at 0x00E01566, `link.w A6,-0xa0`):
 *   aste      (0x08,A6)
 *   segmap    (0x0C,A6)  the segment's 32 entries
 *   write_now (0x10,A6)  ONE BYTE (`move.b (0x10,A6),-(SP)` at 0x00E01682),
 *                        forwarded to FM_$WRITE
 *   status    (0x12,A6)  (D2)
 * (-0x88,A6) is the 32-longword disk image.
 *
 * Original address: 0x00E01566 (362 bytes).  No A5.
 */

#include "ast/ast_internal.h"
#include "mmap/mmap.h"

/*
 * ast_$update_aste_log - the nested NETLOG helper at 0x00E01502 (100 bytes)
 *
 * No stack arguments; it reads this function's frame through the static
 * link (`movea.l (A6),A2` at 0x00E0150A): the disk image at (-0x88) and
 * the ASTE argument at (0x8).  Both are passed explicitly here.
 *
 *   00e0150e  moveq #0x1f,D0                      ; 32 iterations
 *   00e01518  cmpi.l #-0x80000000,(-0x88,A0)      ; UNSIGNED compare, so
 *   00e01522  addq.w #0x1,D2w                     ;   bit 31 set -> count it
 *   00e0152a  subq.l #0x2,SP / clr.l / clr.w      ; p6..p8 = 0
 *   00e01530  move.w D2w,-(SP)                    ; p5 = that count
 *   00e01538  move.b (0x10,A0),D3b                ; p4 = aste->page_count
 *   00e01542  move.w (0xc,A0),-(SP)               ; p3 = aste->segment
 *   00e0154e  pea (0x10,A3)                       ; p2 = &aote->uid
 *   00e01552  move.w #0xc,-(SP)                   ; p1 = kind 12
 */
static void ast_$update_aste_log(const aste_t *aste, const uint32_t *disk)
{
    int16_t marked = 0;
    int i;

    for (i = 0; i < 32; i++) {
        if (disk[i] >= 0x80000000u) {
            marked++;
        }
    }
    NETLOG_$LOG_IT(12, (uint32_t *)&aste->aote->uid, aste->segment,
                   aste->page_count, (uint16_t)marked, 0, 0, 0);
}

void ast_$update_aste(aste_t *aste, segmap_entry_t *segmap, boolean write_now,
                      status_$t *status)
{
    aote_t *aote;               /* D3 */
    uint32_t disk[32];          /* (-0x88,A6) */
    uint32_t *ent;              /* A0 */
    uint32_t addr;
    int16_t i;

    /* 0x00E01572..0x00E01594: btst.l #0xd (DIRTY) then #0xb (REMOTE) */
    if ((aste->flags & ASTE_FLAG_DIRTY) == 0 ||
        (aste->flags & ASTE_FLAG_REMOTE) != 0) {
        *status = status_$ok;                       /* 0x00E016B4 */
        return;
    }
    aote = aste->aote;

    /* 0x00E01598..0x00E015AA: bclr.b #0x5,(0x12,A0) = bit 13 */
    aste->flags &= (uint16_t)~ASTE_FLAG_DIRTY;
    ML_$LOCK(PMAP_LOCK_ID);

    /* 0x00E015AC..0x00E01662: moveq #0x1f / dbf = 32 entries */
    ent = (uint32_t *)segmap;
    for (i = 0; i < 32; i++) {
        if ((ent[i] & SEGMAP_VALID) == 0) {
            /* 0x00E015DA..0x00E01606: the entry's own address; bit 22
             * (btst.l #6 on the high word) -> bit 31 */
            disk[i] = ent[i] & 0x3FFFFF;
            if (ent[i] & 0x00400000u) {
                disk[i] |= 0x80000000u;
            }
        } else {
            /* 0x00E01608..0x00E0164A: the MMAPE's address, its bit 22
             * likewise */
            addr = MMAPE_FOR_VPN(ent[i] & 0xFFFF)->disk_addr;
            disk[i] = addr & 0x3FFFFF;
            if (addr & 0x00400000u) {
                disk[i] |= 0x80000000u;
            }
        }
    }

    /* 0x00E01666..0x00E0167C */
    ML_$UNLOCK(PMAP_LOCK_ID);
    if (NETLOG_$OK_TO_LOG < 0) {
        ast_$update_aste_log(aste, disk);
    }

    /* 0x00E01680..0x00E016A2: FM_$WRITE(&obj_loc, fm_block, segment, disk,
     * write_now, status) */
    FM_$WRITE((fm_$file_ref_t *)(void *)&aote->obj_uid, aste->fm_block,
              aste->segment, (fm_$entry_t *)(void *)disk, write_now, status);

    /* 0x00E016A6..0x00E016C0 */
    if (*status != status_$ok) {
        if (*status == status_$disk_write_protected) {  /* 0x80007 */
            *status = status_$ok;
        } else {
            *status |= (status_$t)0x80000000u;          /* bset.b #7,(A1) */
            aste->flags |= ASTE_FLAG_DIRTY;
        }
    }
}
