/*
 * pmap_$fill_write_qblks - Fill a chain of disk queue blocks for a page batch
 *
 * 0x00E1327E - 0x00E1359A (798 bytes).  Re-emitted from the disassembly
 * 2026-09-27 through the records the code addresses: the earlier emission
 * was structurally faithful but reached the ASTE table, the AOTE and the
 * MMAPE through raw offsets and cleared the segmap's INSTALLED bit through
 * a byte-pointer cast (0x00E1356E `bclr.b #5,(A2)` is bit 29 of the
 * longword).  Called only by PMAP_$PURIFIER_L (0x00E13CC8).
 *
 * Arguments:
 *   (0x8,A6)  pages  -> count longword VPNs (A1 walks it, (-0xC4,A6))
 *   (0xc,A6)  qblk   VA of the first queue block; the chain is followed
 *                    through the block's first longword (0x00E13556)
 *   (0x10,A6) count  word; `subq.w #1` / `bcc' loop, so 0 does nothing
 *
 * Per page (0x00E132AE - 0x00E1358E), with A2 = the MMAPE (0xEB4800 +
 * vpn * 16, fields at -0x2000), A3 = 0xEC5400 + seg * 0x14 (= the 1-based
 * ASTE table entry, aste_t is 0x14 bytes) and the block header at +0x20:
 *   header[0..1]  the object UID: ANON_$UID.high / aote+0x2A word for an
 *                 anonymous page (flags2 bit 7), else aote->uid
 *   +0x31 byte    aote->sub_type (0 for anonymous)
 *   op_flags      aote->vol_index (aote+0x24 high word for anonymous)
 *   header[2]     aste->segment << 5 | page index
 *   header[3]     TIME_$CURRENT_CLOCKH
 *   +0x30..+0x3B  zero (byte 0x31 written before)
 * A page with no disk address (mmape.disk_addr & 0x3FFFFF == 0) gets one:
 * every later page of the batch on the same segment without an address
 * is collected (0x00E1338E - 0x00E133E8), a hint is looked up under the
 * PMAP lock from the nearest neighbour in the segment map that has one
 * (backward then forward, VALID entries through their MMAPE, 0x00E13400 -
 * 0x00E13490) or from aste->fm_block >> 4, BAT_$ALLOCATE(vol, hint, n, 1)
 * is called (a failure crashes) and the addresses are stored into the
 * collected MMAPEs; aste->flags bit 13 (ASTE_FLAG_DIRTY, `bset.b #5' on
 * the high byte) is set.  Then daddr and ppn are
 * stored, the write is NETLOGged (kind 3), and if DISK_$DO_CHKSUM is on
 * and the page is INSTALLED it is removed from the MMU.
 */

#include "pmap/pmap_internal.h"
#include "ast/ast.h"
#include "bat/bat.h"
#include "disk/disk.h"
#include "misc/misc.h"
#include "mmap/mmap.h"
#include "mmu/mmu.h"
#include "netlog/netlog.h"
#include "time/time.h"
#include "uid/uid.h"

/* PMAP_DADDR_MASK (the low 22 bits of mmape_t.disk_addr): pmap/pmap_internal.h */
/* 0x00E13548: NETLOG record kind for a page write */
#define PMAP_NETLOG_KIND_WRITE  3
/* 0x00E134FE: `bset.b #5,(-0x2,A1)` with A1 = aste + 0x14 -> bit 5 of the byte at
 * aste + 0x12, the HIGH byte of the 16-bit flags word = bit 13 = ASTE_FLAG_DIRTY */
#define PMAP_ASTE_FLAG_ALLOCATED ASTE_FLAG_DIRTY

/* Queue block longword indexes (disk_io_req_t offsets / 4) */
#define QB_DADDR    1       /* +0x04 */
#define QB_PPN      5       /* +0x14 */
#define QB_OPFLAGS  7       /* +0x1C..+0x1F: op_flags is the low byte */
#define QB_HDR      8       /* +0x20: header[0] */

/* The entry for `seg' of the 1-based ASTE table (0xEC53F0 + seg * 0x14) */
static inline aste_t *pmap_$aste_for_segment(uint16_t seg)
{
    return AST_ASTE_ENTRY((int16_t)seg);
}

/* A neighbour's block address: the entry's own low 22 bits, or its
 * MMAPE's when the entry is VALID (0x00E13410 / 0x00E13458). */
static inline uint32_t pmap_$neighbour_daddr(uint32_t entry)
{
    if ((entry & PMAP_SEGMAP_L_VALID) != 0) {
        return MMAPE_FOR_VPN(entry & PMAP_SEGMAP_L_VPN_MASK)->disk_addr & PMAP_DADDR_MASK;
    }
    return entry & PMAP_DADDR_MASK;
}

void pmap_$fill_write_qblks(int32_t *pages, uint32_t *qblk, int16_t count)
{
    uint32_t *qb;                   /* (-0x9C,A6): current block */
    int32_t *page_ptr;              /* (-0xC4,A6) */
    int16_t remaining;              /* (-0xAC,A6) */
    int16_t page_index;             /* (-0xA8,A6): 1-based */
    uint32_t vpn;
    mmape_t *page;                  /* (-0xB0,A6) */
    aste_t *aste;                   /* (-0xB4,A6) */
    aote_t *aote;                   /* A0 / A4 */
    uint16_t seg;
    uint16_t page_idx;              /* D4 */
    uint16_t vol;                   /* D5 */
    uint32_t *seg_entry;            /* (-0x90,A6) */
    uint32_t daddr;                 /* D2 */
    mmape_t *needs_addr[16];        /* (-0x88,A6) + 4n */
    uint32_t blocks[16];            /* (-0x48,A6) */
    status_$t status[4];            /* (-0x98,A6) */
    int16_t n_needed;               /* D3 */
    int16_t k, j;

    /* 0x00E13286 - 0x00E132AA */
    qb = qblk;
    remaining = (int16_t)(count - 1);
    if (remaining < 0) {
        return;
    }
    page_index = 1;
    page_ptr = pages;

    do {
        /* 0x00E132AE - 0x00E132DC */
        vpn = (uint32_t)*page_ptr;
        page = MMAPE_FOR_VPN(vpn);
        seg = page->segment;
        aste = pmap_$aste_for_segment(seg);
        page_idx = page->seg_offset;

        /* 0x00E132E0 - 0x00E13330: the object identity for the header */
        if ((int8_t)page->flags2 < 0) {
            qb[QB_HDR + 0] = ANON_$UID.high;                        /* 0x00E13316 */
            aote = aste->aote;
            qb[QB_HDR + 1] = aote->dtm_high & 0xFFFFu;              /* the word at aote+0x2A */
            qb[QB_HDR + 4] = 0;                                     /* clr.b (0x31,A1) */
            vol = (uint16_t)(aote->unknown_24 >> 16);               /* the word at aote+0x24 */
        } else {
            aote = aste->aote;
            qb[QB_HDR + 0] = aote->uid.high;
            qb[QB_HDR + 1] = aote->uid.low;
            qb[QB_HDR + 4] = (uint32_t)aote->sub_type << 16;        /* move.b (0xd,A0),(0x31,A1) */
            vol = aote->vol_index;
        }

        /* 0x00E13334 - 0x00E1335E */
        qb[QB_HDR + 2] = ((uint32_t)aste->segment << 5) + page_idx;
        qb[QB_HDR + 3] = TIME_$CURRENT_CLOCKH;
        qb[QB_HDR + 4] &= 0x00FF0000u;                              /* clr.b (0x30) and 0x32..0x33 */
        qb[QB_HDR + 5] = 0;                                         /* 0x34..0x37 */
        qb[QB_HDR + 6] = 0;                                         /* 0x38..0x3B */
        qb[QB_OPFLAGS] = (qb[QB_OPFLAGS] & 0xFFFFFF00u) | (vol & 0xFF); /* move.b D5b,(0x1f,A1) */

        /* 0x00E13362 - 0x00E1337C: the segment map entry, "movea.l #0xed5000,A0"
         * then (-0x80,A0,page*4): 1-based row seg of PMAP_$SEGMAP */
        seg_entry = (uint32_t *)&PMAP_SEGMAP_ROW(seg)[page_idx];

        /* 0x00E13380 - 0x00E1338A */
        if ((page->disk_addr & PMAP_DADDR_MASK) == 0) {
            /* 0x00E1338E - 0x00E133E8: this page and every later one of
             * the batch on the same segment without an address */
            n_needed = 0;
            j = (int16_t)(count - page_index);
            if (j >= 0) {
                int32_t *scan = pages + page_index;
                do {
                    mmape_t *other = MMAPE_FOR_VPN((uint32_t)scan[-1]);
                    if (other->segment == page->segment &&
                        (other->disk_addr & PMAP_DADDR_MASK) == 0) {
                        needs_addr[n_needed] = other;
                        n_needed++;
                    }
                    scan++;
                } while (j-- != 0);
            }

            /* 0x00E133EC - 0x00E133FE */
            daddr = 0;
            ML_$LOCK(PMAP_LOCK_ID);

            /* 0x00E13400 - 0x00E13438: backward over the earlier pages */
            k = (int16_t)(page_idx - 1);
            if (k >= 0) {
                uint32_t *p = seg_entry;
                do {
                    p--;
                    daddr = pmap_$neighbour_daddr(*p);
                    if (daddr != 0) {
                        break;
                    }
                } while (k-- != 0);
            }

            /* 0x00E1343C - 0x00E13484: forward over the later pages */
            if (daddr == 0) {
                k = (int16_t)(0x1F - (page_idx + 1));
                if (k >= 0) {
                    uint32_t *p = seg_entry;
                    do {
                        p++;
                        daddr = pmap_$neighbour_daddr(*p);
                        if (daddr != 0) {
                            break;
                        }
                    } while (k-- != 0);
                }
                /* 0x00E13484 - 0x00E13490 */
                if (daddr == 0) {
                    daddr = aste->fm_block >> 4;
                }
            }

            /* 0x00E13492 - 0x00E134CE: BAT_$ALLOCATE(vol, hint, n, 1,
             * blocks, status); `move.w #1` is use_reserved */
            ML_$UNLOCK(PMAP_LOCK_ID);
            BAT_$ALLOCATE(vol, daddr, n_needed, 1, blocks, status);
            if (status[0] != status_$ok) {
                CRASH_SYSTEM(status);
            }

            /* 0x00E134D0 - 0x00E134F6 */
            k = (int16_t)(n_needed - 1);
            if (k >= 0) {
                for (j = 0; j <= k; j++) {
                    needs_addr[j]->disk_addr &= ~PMAP_DADDR_MASK;
                    needs_addr[j]->disk_addr |= blocks[j];
                }
            }

            /* 0x00E134FA */
            aste->flags |= PMAP_ASTE_FLAG_ALLOCATED;
        }

        /* 0x00E13504 - 0x00E1351E */
        qb[QB_DADDR] = page->disk_addr & PMAP_DADDR_MASK;
        qb[QB_PPN] = vpn;

        /* 0x00E13524 - 0x00E1354E */
        if (NETLOG_$OK_TO_LOG < 0) {
            NETLOG_$LOG_IT(PMAP_NETLOG_KIND_WRITE, &qb[QB_HDR],
                           (uint16_t)(qb[QB_HDR + 2] >> 5), page_idx,
                           (uint16_t)vpn, 0, 0, 0);
        }

        /* 0x00E13552 - 0x00E13580 */
        qb = (uint32_t *)ARCH_VA_TO_PTR(qb[0]);
        if (DISK_$DO_CHKSUM < 0 && (*seg_entry & PMAP_SEGMAP_L_INSTALLED) != 0) {
            *seg_entry &= ~PMAP_SEGMAP_L_INSTALLED;
            MMU_$REMOVE(vpn);
        }

        /* 0x00E13582 - 0x00E1358E */
        page_index++;
        page_ptr++;
    } while (remaining-- != 0);
}
