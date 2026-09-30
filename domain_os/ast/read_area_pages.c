/*
 * ast_$read_area_pages - Read a run of a local segment's pages from disk
 *
 * Allocates `count` frames (PMAP lock held on entry, released after the
 * allocation), builds a DISK_$GET_QBLKS chain whose head block carries
 * the object UID and first page number and whose blocks carry one
 * (disk address, ppn) pair each, and reads them in one DISK_$READ_MULTI.
 * A failure status gets bit 31.  With the PMAP lock retaken the chain is
 * returned, the frames that were not filled are freed, and the calling
 * process's page-read statistic is bumped.
 *
 * Parameters (frame at 0x00E02AF6, `link.w A6,-0x20`):
 *   aste       (0x08,A6)  (A3)
 *   segmap     (0x0C,A6)  (A2) the entries whose low 22 bits are disk addresses
 *   ppn_array  (0x10,A6)  (D5) receives the frames
 *   start_page (0x14,A6)  word (D2)
 *   count      (0x16,A6)  word
 *   status     (0x18,A6)  (D6)
 * Locals: (-0xC) tail, (-0x10) head, (-0x12) volume index word,
 *         (-0x16) pages read, (-0x20) the stat increment.
 *
 * Returns D0w = pages read.
 *
 * Original address: 0x00E02AF6 (348 bytes).  No A5.
 */

#include "ast/ast_internal.h"
#include "mmap/mmap.h"
#include "disk/disk.h"
/*
 * TODO(source-jtfb): disk_io_req_t is defined in disk/disk_internal.h;
 * this routine fills the queue blocks DISK_$GET_QBLKS hands out.
 */
#include "disk/disk_internal.h"

int16_t ast_$read_area_pages(aste_t *aste, uint32_t *segmap,
                             uint32_t *ppn_array, uint16_t start_page,
                             uint16_t count, status_$t *status)
{
    aote_t *aote;               /* A0 */
    int16_t allocated;          /* D3 */
    int16_t pages_read;         /* (-0x16,A6) / D2 */
    uint16_t vol_idx;           /* (-0x12,A6) */
    uint32_t qblk_head;         /* (-0x10,A6) / D4 */
    uint32_t qblk_tail;         /* (-0xC,A6) */
    disk_io_req_t *req;         /* A1 */
    int16_t i;

    /* 0x00E02B12..0x00E02B22: allocate_pages(count, 1, array) */
    allocated = ast_$allocate_pages((int16_t)count, 1, ppn_array);

    /* 0x00E02B24..0x00E02B30 */
    ML_$UNLOCK(PMAP_LOCK_ID);

    /* 0x00E02B32..0x00E02B3C: zero-extended volume index */
    aote = aste->aote;
    vol_idx = aote->vol_index;

    /* 0x00E02B40..0x00E02B52: result slot discarded */
    DISK_$GET_QBLKS(allocated, &qblk_head, &qblk_tail);

    /* 0x00E02B56..0x00E02B80: the head block's header: page number
     * (+0x28), object UID (+0x20/+0x24), and the byte at +0x30 cleared */
    req = (disk_io_req_t *)ARCH_VA_TO_PTR(qblk_head);
    req->header[2] = ((uint32_t)aste->segment << 5) + (uint32_t)start_page;
    req->header[0] = aote->uid.high;
    req->header[1] = aote->uid.low;
    req->header[4] &= 0x00FFFFFFu;

    /* 0x00E02B84..0x00E02BA6: one block per frame; dbf on allocated - 1 */
    for (i = 0; i < allocated; i++) {
        req->ppn = ppn_array[i];
        req->daddr = *segmap++ & 0x3FFFFF;
        req = (disk_io_req_t *)ARCH_VA_TO_PTR(req->free_next);   /* (0x8,A1) */
    }

    /* 0x00E02BAA..0x00E02BC6: DISK_$READ_MULTI(vol, TRUE, TRUE, head, tail,
     * &pages_read, status); the two `st` are single bytes */
    DISK_$READ_MULTI(vol_idx, -1, -1, qblk_head, qblk_tail,
                     &pages_read, status);

    /* 0x00E02BCA..0x00E02BD4 */
    if (*status != status_$ok) {
        *status |= (status_$t)0x80000000u;
    }

    /* 0x00E02BD8..0x00E02BF6 */
    ML_$LOCK(PMAP_LOCK_ID);
    DISK_$RTN_QBLKS(allocated, qblk_head, qblk_tail);

    /* 0x00E02BFA..0x00E02C24: free array[pages_read .. allocated-1];
     * the count is allocated - (pages_read + 1), bmi skips */
    if (allocated != pages_read) {
        for (i = pages_read; i < allocated; i++) {
            MMAP_$FREE(ppn_array[i]);
        }
    }

    /* 0x00E02C28..0x00E02C42: PROC1_$STATS entry pid, longword +0x08
     * (A0 = 0xE25D20 is entry 1; -0x8 + pid*16) */
    PROC1_$DATA.stats[PROC1_$CURRENT].stat[2] += (uint32_t)(int32_t)pages_read;

    /* 0x00E02C46 */
    return pages_read;
}
