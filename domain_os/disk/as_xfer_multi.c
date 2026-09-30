/*
 * DISK_$AS_XFER_MULTI - Multiple asynchronous page transfers on one volume
 *
 * 0x00E6B962 - 0x00E6BC16 (694 bytes).  Re-emitted in full from the
 * disassembly on 2026-09-08; the earlier file left the queue-block fill
 * unemitted and had the frame laid out by guesswork.
 *
 * Arguments (all Pascal var parameters):
 *   (0x8,A6)  vol_idx_ptr   -> word volume index (D6)
 *   (0xc,A6)  count_ptr     -> word transfer count (D7), at most 16
 *   (0x10,A6) op_type_ptr   -> word (D4): 1 = write (`cmpi.w #0x1`
 *                              at 0x00E6B9CA / 0x00E6BABC / 0x00E6BAE6),
 *                              anything else goes down the READ_MULTI
 *                              path, but the block headers are copied
 *                              out only when it is exactly 0 (`tst.w
 *                              D4w; bne` at 0x00E6BB50 and 0x00E6BBB4)
 *   (0x14,A6) daddr_array   -> count longword disk addresses
 *   (0x18,A6) info_array    -> count pointers to eight-longword block
 *                              headers: copied in for a write
 *                              (0x00E6B9D0), copied out for op_type 0
 *                              (0x00E6BBB8)
 *   (0x1c,A6) buffer_array  -> count page-aligned buffer VAs
 *   (0x20,A6) status_array  -> count per-transfer status words (out)
 *   (0x24,A6) status        -> overall status (out)
 *
 * Frame (link -0x32c):
 *   -0x300  header[16][8]   local copies of the block headers, 0x20 each
 *   -0x100  page_status[16] per-transfer status from the queue blocks
 *   -0xc0   daddr[16]
 *   -0x80   buffer[16]
 *   -0x40   wired[16]       MST_$WIRE results
 *   -0x304  st              working status
 *   -0x30c  tail, -0x310 head   DISK_$GET_QBLKS chain
 *   -0x312  pages_done      word; what READ_MULTI reports, or count
 *   -0x324/-0x328/-0x32c    loop cursors
 *
 * Every per-page loop is `move.w count-1,Dn` / `dbf`, skipped when the
 * count is zero (0x00E6B984 `bmi`).
 */

#include "cache/cache.h"
#include "disk/disk_internal.h"
#include "mst/mst.h"
#include "wp/wp.h"

/* 0x00E6B9E6: `andi.l #0x3ff,D2` - every buffer must start a 1K page */
#define DISK_XFER_PAGE_ALIGN_MASK  0x3ffu
/* 0x00E6B9CA / 0x00E6BABC / 0x00E6BAE6: `cmpi.w #0x1,D4w` */
#define DISK_XFER_OP_WRITE          1
/* 0x00E6BB50 / 0x00E6BBB4: `tst.w D4w` - the header copy-out is for 0 only */
#define DISK_XFER_OP_READ           0
/* The frame holds room for this many transfers. */
#define DISK_XFER_MAX_PAGES         16

void DISK_$AS_XFER_MULTI(uint16_t *vol_idx_ptr, int16_t *count_ptr,
                         int16_t *op_type_ptr, uint32_t *daddr_array,
                         uint32_t **info_array, uint32_t *buffer_array,
                         uint32_t *status_array, status_$t *status)
{
    uint32_t    header[DISK_XFER_MAX_PAGES][8];     /* (-0x300,A6) */
    status_$t   page_status[DISK_XFER_MAX_PAGES];   /* (-0x100,A6) */
    uint32_t    daddr[DISK_XFER_MAX_PAGES];         /* (-0xc0,A6) */
    uint32_t    buffer[DISK_XFER_MAX_PAGES];        /* (-0x80,A6) */
    uint32_t    wired[DISK_XFER_MAX_PAGES];         /* (-0x40,A6) */
    status_$t   st;                                 /* (-0x304,A6) */
    uint32_t    tail;                               /* (-0x30c,A6) */
    uint32_t    head;                               /* (-0x310,A6) */
    int16_t     pages_done;                         /* (-0x312,A6) */
    uint16_t    vol_idx;                            /* D6 */
    int16_t     count;                              /* D7 */
    int16_t     op_type;                            /* D4 */
    int16_t     last;                               /* D5 = count - 1 */
    disk_io_req_t *req;
    int16_t     i;
    int16_t     j;
    int16_t     k;

    /* 0x00E6B96A - 0x00E6B982 */
    vol_idx = *vol_idx_ptr;
    count = *count_ptr;
    op_type = *op_type_ptr;
    pages_done = 0;
    last = (int16_t)(count - 1);

    /*
     * 0x00E6B984 - 0x00E6BA0C: gather the per-page inputs.  A misaligned
     * buffer stops the gather and jumps to the result fill with only the
     * pages before it copied; page_status[] and header[] are then whatever
     * the frame held (the original reads its stack; the fill below
     * overwrites every status_array entry with "transfer not executed",
     * so only a read's header copies carry that garbage out).
     */
    if (last >= 0) {
        for (i = 0; i <= last; i++) {
            daddr[i] = daddr_array[i];                              /* 0x00E6B9BA */
            buffer[i] = buffer_array[i];                            /* 0x00E6B9C4 */
            if (op_type == DISK_XFER_OP_WRITE) {                    /* 0x00E6B9CA */
                for (k = 0; k < 8; k++) {                           /* moveq #7 / dbf */
                    header[i][k] = info_array[i][k];
                }
            }
            if ((buffer_array[i] & DISK_XFER_PAGE_ALIGN_MASK) != 0) { /* 0x00E6B9E2 */
                st = status_$disk_buffer_not_page_aligned;
                goto fill_results;                                  /* 0x00E6B9F6 */
            }
        }
    }

    /* 0x00E6BA10 - 0x00E6BA6C: wire every page; on a failure unwire the
     * ones already wired (i of them) and go to the result fill */
    if (last >= 0) {
        for (i = 0; i <= last; i++) {
            wired[i] = MST_$WIRE(buffer_array[i], &st);             /* 0x00E6BA30 */
            if (st != 0) {                                          /* tst.l 0x00E6BA3C */
                j = (int16_t)(i - 1);                               /* 0x00E6BA42: D3 - 2 */
                if (j < 0) {
                    goto fill_results;                              /* 0x00E6BA46 */
                }
                for (k = 0; k <= j; k++) {                          /* dbf 0x00E6BA5E */
                    WP_$UNWIRE(wired[k]);
                }
                goto fill_results;                                  /* 0x00E6BA62 */
            }
        }
    }

    /* 0x00E6BA70 - 0x00E6BA90 */
    CACHE_$FLUSH_VIRTUAL();
    DISK_$GET_QBLKS(count, &head, &tail);

    /* 0x00E6BA92 - 0x00E6BAE2: fill the queue blocks along the +0x00
     * chain.  A write also plants the header and stores the volume index's
     * low byte in op_flags (+0x1f, `move.b D6b,(0x1f,A0)`). */
    req = (disk_io_req_t *)ARCH_VA_TO_PTR(head);
    if (last >= 0) {
        for (i = 0; i <= last; i++) {
            req->daddr = daddr[i];                                  /* 0x00E6BAB0 */
            req->ppn = wired[i];                                    /* 0x00E6BAB6 */
            if (op_type == DISK_XFER_OP_WRITE) {
                for (k = 0; k < 8; k++) {                           /* 0x00E6BACC */
                    req->header[k] = header[i][k];
                }
                req->op_flags = (uint8_t)vol_idx;                   /* 0x00E6BAD2 */
            }
            req = (disk_io_req_t *)ARCH_VA_TO_PTR(req->next);       /* 0x00E6BAD6 */
        }
    }

    /* 0x00E6BAE6 - 0x00E6BB20 */
    if (op_type == DISK_XFER_OP_WRITE) {
        DISK_$WRITE_MULTI(0, ARCH_VA_TO_PTR(head), &st);            /* clr.w 0x00E6BAF4 */
        pages_done = count;                                         /* 0x00E6BB00 */
    } else {
        DISK_$READ_MULTI(vol_idx, 0, 0, head, tail, &pages_done, &st);
    }

    /* 0x00E6BB24 - 0x00E6BB74: collect results along the +0x08 chain
     * (free_next - the multi routines have relinked the blocks), unwire,
     * and for a read copy the block headers back */
    req = (disk_io_req_t *)ARCH_VA_TO_PTR(head);
    if (last >= 0) {
        for (i = 0; i <= last; i++) {
            page_status[i] = req->status;                           /* 0x00E6BB3E */
            WP_$UNWIRE(wired[i]);                                   /* 0x00E6BB44 */
            if (op_type == DISK_XFER_OP_READ) {                     /* 0x00E6BB50: tst.w D4w / bne */
                for (k = 0; k < 8; k++) {                           /* 0x00E6BB60 */
                    header[i][k] = req->header[k];
                }
            }
            req = (disk_io_req_t *)ARCH_VA_TO_PTR(req->free_next);  /* 0x00E6BB6C */
        }
    }

    /* 0x00E6BB78 - 0x00E6BB88 */
    DISK_$RTN_QBLKS(count, head, tail);

fill_results:
    /* 0x00E6BB8C - 0x00E6BBD6: hand back every page's status and, for a
     * read, its header */
    if (last >= 0) {
        for (i = 0; i <= last; i++) {
            status_array[i] = (uint32_t)page_status[i];             /* 0x00E6BBAE */
            if (op_type == DISK_XFER_OP_READ) {                     /* 0x00E6BBB4: tst.w D4w / bne */
                for (k = 0; k < 8; k++) {                           /* 0x00E6BBC4 */
                    info_array[i][k] = header[i][k];
                }
            }
        }
    }

    /* 0x00E6BBDA - 0x00E6BC02: pages pages_done .. count-1 were never
     * transferred (count - pages_done of them, `dbf` on count - (done+1)) */
    j = (int16_t)(pages_done + 1);
    if ((int16_t)(count - j) >= 0) {
        for (i = (int16_t)(j - 1); i <= last; i++) {
            status_array[i] = status_$disk_transfer_not_executed;   /* 0x00E6BBF8 */
        }
    }

    /* 0x00E6BC06 - 0x00E6BC0A */
    *status = st;
}
