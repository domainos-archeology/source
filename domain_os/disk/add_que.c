/*
 * DISK_$ADD_QUE - Add a chain of requests to a driver's elevator queue
 *
 * 0x00E3C716 - 0x00E3C9F8 (740 bytes, `DISK_` module, A5 = DISK_$DATA at
 * 0xE7A1CC).  Re-emitted from the disassembly on 2026-09-08; the earlier
 * file had the wrong source for `chunk_len`, counted requests instead of
 * cylinder runs, and left both merge arms unemitted.
 *
 * The request chain is sorted by absolute disk address (header[7], +0x3c)
 * unless bit 0 of `flags` says the caller already sorted it; the sorted
 * chain is then cut into cylinder runs, one entry of the module's
 * request array (DISK_$DATA + 0xB08) per run, and the runs are spliced
 * into the queue's two lists around the disk's current cylinder:
 *
 *   direction bit set   (queue->position bit 31, `tst.w (0x4,A0)` < 0):
 *       list_a (+0x08) ascending  <- runs at or above the current cylinder
 *       list_b (+0x0c) descending <- the runs below it
 *   direction bit clear:
 *       list_a (+0x08) descending <- runs at or below the current cylinder
 *       list_b (+0x0c) ascending  <- the runs above it
 *
 * Both lists end in a sentinel whose cylinder word is 0xFFFF (see
 * disk_$que_t in disk/disk_internal.h).
 *
 * Arguments (0x00E3C724-0x00E3C72E; args are pushed right to left, so
 * (0x8,A6) is the last push):
 *   (0x8,A6)  flags     word by value; bit 0 = "chain is already sorted"
 *   (0xa,A6)  dev       address of the driver record: word +0x08 bit 9
 *                       means the driver keeps its own queue (fatal here),
 *                       word +0x0a is its ML_$LOCK resource id
 *   (0xe,A6)  queue     the disk_$que_t; its first longword is the spin lock
 *   (0x12,A6) req_list  VA of the first request; the slot is rewritten
 *                       when the sort moves a new request to the front
 *
 * The two merge helpers at 0x00E3C5DA and 0x00E3C690 are nested Pascal
 * procedures: they take the parent's A5 and, for the ascending one, the
 * parent's group_end local through the static link (`movea.l (A6),A0`
 * at 0x00E3C5F0, `(-0x4,A0)`).  They are the static functions below.
 */

#include "disk/disk_internal.h"
#include "misc/misc.h"
#include "ml/ml.h"

/*
 * 0x00E3C9FA: 00 08 00 2E, passed by `pea (0x2c0,PC)` at 0x00E3C738 to
 * CRASH_SYSTEM (0x00E3C73C) when the driver record's word +0x08 has bit 9
 * set.  stcode.db.10.2: 0x0008002E "queued drivers not supported".
 */
static const status_$t disk_$queued_drivers_not_supported_00e3c9fa = 0x0008002E;

/* Driver record words DISK_$ADD_QUE reads (0x00E3C72E, 0x00E3C7B2). */
#define DISK_QUE_DEV_FLAGS_OFFSET   0x08
#define DISK_QUE_DEV_FLAG_OWN_QUEUE 0x0200      /* btst.l #0x9 */
#define DISK_QUE_DEV_LOCK_OFFSET    0x0a

static inline uint16_t disk_$que_dev_word(const void *dev, int off)
{
    return *(const uint16_t *)((const uint8_t *)dev + off);
}

/* The request array: entry i (1-based) is the head of cylinder run i. */
static inline disk_io_req_t *disk_$que_run(uint16_t i)
{
    return (disk_io_req_t *)ARCH_VA_TO_PTR(DISK_$QUE_ARRAY[i]);
}

static inline disk_io_req_t *req_next(const disk_io_req_t *r)
{
    return (disk_io_req_t *)ARCH_VA_TO_PTR(r->next);
}

static inline disk_io_req_t *req_run_end(const disk_io_req_t *r)
{
    return (disk_io_req_t *)ARCH_VA_TO_PTR(r->reserved_18);
}

/* The cylinder word at +0x04 is the high half of daddr. */
static inline uint16_t req_cyl(const disk_io_req_t *r)
{
    return (uint16_t)(r->daddr >> 16);
}

/*
 * 0x00E3C5DA - 0x00E3C68E (182 bytes): merge runs n, n+1, ... (up to the
 * zero terminator the parent wrote at array[count + 1]) into the ascending
 * list whose head cell is `list`.  `group_end` is the parent's (-0x4,A6),
 * the last request of the whole chain.  When `mark` is true, bit 6 of the
 * op_flags byte (+0x1f) of the last run end spliced in is set
 * (0x00E3C618, 0x00E3C680).
 */
static void disk_$add_que_merge_ascending(uint32_t *list, uint16_t n, int8_t mark,
                                          disk_io_req_t *group_end)
{
    disk_io_req_t *head;
    disk_io_req_t *cur;
    disk_io_req_t *run;
    disk_io_req_t *prev;
    uint16_t idx;
    uint16_t cyl;

    head = (disk_io_req_t *)ARCH_VA_TO_PTR(*list);

    /* 0x00E3C5F4: the list holds only its sentinel - hang the whole
     * remaining chain in front of it. */
    if (head->next == 0) {
        group_end->next = ARCH_PTR_TO_VA(head);                 /* 0x00E3C5FC */
        *list = DISK_$QUE_ARRAY[n];                             /* 0x00E3C60C */
        if (mark < 0) {
            group_end->op_flags |= 0x40;                        /* 0x00E3C618 */
        }
        return;
    }

    /* 0x00E3C620 - 0x00E3C67A */
    prev = NULL;
    cur = head;
    idx = n;
    run = disk_$que_run(idx);
    for (;;) {
        cyl = req_cyl(run);                                     /* 0x00E3C63C */
        if (cyl > req_cyl(cur)) {                               /* bhi 0x00E3C644 */
            /* 0x00E3C674: step over the existing run */
            prev = req_run_end(cur);
            cur = req_next(prev);
            continue;
        }
        if (prev == NULL) {
            *list = ARCH_PTR_TO_VA(run);                        /* 0x00E3C64E */
        } else {
            prev->next = ARCH_PTR_TO_VA(run);                   /* 0x00E3C652 */
        }
        prev = req_run_end(run);                                /* 0x00E3C654 */
        if (cyl == req_cyl(cur)) {
            run->reserved_18 = cur->reserved_18;                /* 0x00E3C65E */
        }
        prev->next = ARCH_PTR_TO_VA(cur);                       /* 0x00E3C664 */
        idx++;                                                  /* 0x00E3C666 */
        run = disk_$que_run(idx);
        if (run == NULL) {
            break;
        }
    }
    if (mark < 0) {
        prev->op_flags |= 0x40;                                 /* 0x00E3C680 */
    }
}

/*
 * 0x00E3C690 - 0x00E3C714 (134 bytes): merge runs n, n-1, ..., 1 (down to
 * the zero terminator at array[0]) into the descending list whose head
 * cell is `list`.  The cylinder compare is signed here (`blt` at
 * 0x00E3C6CA), so the 0xFFFF sentinel reads as -1 and every run sorts in
 * front of it.  No empty-list special case in this helper.
 */
static void disk_$add_que_merge_descending(uint32_t *list, uint16_t n, int8_t mark)
{
    disk_io_req_t *cur;
    disk_io_req_t *run;
    disk_io_req_t *prev;
    uint16_t idx;
    int16_t cyl;

    prev = NULL;                                                /* 0x00E3C6A4 */
    cur = (disk_io_req_t *)ARCH_VA_TO_PTR(*list);               /* 0x00E3C6AA */
    idx = n;
    run = disk_$que_run(idx);                                   /* 0x00E3C6C0 */
    for (;;) {
        cyl = (int16_t)req_cyl(run);                            /* 0x00E3C6C2 */
        if (cyl < (int16_t)req_cyl(cur)) {                      /* blt 0x00E3C6CA */
            /* 0x00E3C6FA: step over the existing run */
            prev = req_run_end(cur);
            cur = req_next(prev);
            continue;
        }
        if (prev == NULL) {
            *list = ARCH_PTR_TO_VA(run);                        /* 0x00E3C6D4 */
        } else {
            prev->next = ARCH_PTR_TO_VA(run);                   /* 0x00E3C6D8 */
        }
        prev = req_run_end(run);                                /* 0x00E3C6DA */
        if (cyl == (int16_t)req_cyl(cur)) {
            run->reserved_18 = cur->reserved_18;                /* 0x00E3C6E4 */
        }
        prev->next = ARCH_PTR_TO_VA(cur);                       /* 0x00E3C6EA */
        idx--;                                                  /* 0x00E3C6EC */
        run = disk_$que_run(idx);
        if (run == NULL) {
            break;
        }
    }
    if (mark < 0) {
        prev->op_flags |= 0x40;                                 /* 0x00E3C706 */
    }
}

void DISK_$ADD_QUE(uint16_t flags, void *dev, disk_$que_t *queue, void *req_list)
{
    disk_io_req_t *list;        /* (0x12,A6) */
    disk_io_req_t *run;         /* A1 in the run loop */
    disk_io_req_t *req;         /* A0 */
    disk_io_req_t *group_start; /* A2 */
    disk_io_req_t *group_end;   /* (-0x4,A6) */
    disk_io_req_t *prev_a1;     /* D1 in the sort */
    disk_io_req_t *prev_a0;     /* D2 in the sort */
    int8_t sorted;              /* D4b */
    int16_t chunk_len;          /* D6w */
    uint16_t count;             /* D2w: number of cylinder runs */
    uint16_t ahead;             /* D5w */
    uint16_t cur_cyl;           /* D3w */
    uint16_t below;             /* D4w after 0x00E3C8AA */
    uint16_t pos;
    uint16_t i;
    uint16_t n;
    ml_$spin_token_t token;     /* (-0x20,A6) */

    list = (disk_io_req_t *)req_list;

    /* 0x00E3C72C - 0x00E3C742 */
    if ((disk_$que_dev_word(dev, DISK_QUE_DEV_FLAGS_OFFSET) & DISK_QUE_DEV_FLAG_OWN_QUEUE) != 0) {
        CRASH_SYSTEM(&disk_$queued_drivers_not_supported_00e3c9fa);
    }

    /* 0x00E3C744 - 0x00E3C74E: `sne D4b`; the first request's +0x1c word
     * (the transfer chunk disk_$map_request stored there) is read before
     * the loop below reuses that word as a run count. */
    sorted = ((flags & 1) != 0) ? -1 : 0;
    chunk_len = (int16_t)list->flags;

sort_again:
    /* 0x00E3C752 - 0x00E3C7AC: exchange sort of the chain by header[7],
     * unsigned; `list` follows the front request. */
    if (sorted >= 0) {
        prev_a1 = NULL;                                         /* D1 */
        run = list;                                             /* A1 */
        while (run != NULL) {                                   /* 0x00E3C7A8 */
            prev_a0 = run;                                      /* 0x00E3C760 */
            req = req_next(run);
            while (req != NULL) {                               /* 0x00E3C79E */
                if (req->header[7] < run->header[7]) {          /* 0x00E3C766 */
                    disk_io_req_t *run_next = req_next(run);    /* D0 */
                    disk_io_req_t *tmp;
                    if (prev_a1 != NULL) {
                        prev_a1->next = ARCH_PTR_TO_VA(req);    /* 0x00E3C778 */
                    }
                    run->next = req->next;                      /* 0x00E3C77A */
                    if (req == run_next) {
                        req->next = ARCH_PTR_TO_VA(run);        /* 0x00E3C780 */
                    } else {
                        prev_a0->next = ARCH_PTR_TO_VA(run);    /* 0x00E3C786 */
                        req->next = ARCH_PTR_TO_VA(run_next);   /* 0x00E3C788 */
                    }
                    if (run == list) {
                        list = req;                             /* 0x00E3C790 */
                    }
                    tmp = run;                                  /* 0x00E3C794 */
                    run = req;
                    req = tmp;
                }
                prev_a0 = req;                                  /* 0x00E3C79A */
                req = req_next(req);
            }
            prev_a1 = run;                                      /* 0x00E3C7A4 */
            run = req_next(run);
        }
    }

    /* 0x00E3C7AE - 0x00E3C7BC */
    ML_$LOCK((int16_t)disk_$que_dev_word(dev, DISK_QUE_DEV_LOCK_OFFSET));

    /* 0x00E3C7BE - 0x00E3C7DE */
    ahead = 0;
    cur_cyl = (uint16_t)((queue->position & DISK_QUE_POSITION_MASK) >> DISK_QUE_POSITION_SHIFT);
    count = 0;
    DISK_$QUE_ARRAY[0] = 0;
    run = list;
    group_start = NULL;     /* A2 is never written when the chain is empty;
                             * 0x00E3C89A then reads whatever A2 held */
    group_end = NULL;

    /* 0x00E3C7E2 - 0x00E3C896: one array entry per cylinder run */
    while (run != NULL) {
        group_start = run;                                      /* 0x00E3C7E2 */
        count++;
        DISK_$QUE_ARRAY[count] = ARCH_PTR_TO_VA(run);           /* 0x00E3C7EE */
        if (ahead == 0) {                                       /* 0x00E3C7F2 */
            if (!(cur_cyl > req_cyl(run))) {                    /* bhi 0x00E3C7FA */
                ahead = count;
            }
        }
        run->flags = 1;                                         /* 0x00E3C7FE */
        group_end = run;                                        /* 0x00E3C804 */
        req = req_next(run);
        while (req != NULL) {                                   /* 0x00E3C884 */
            /* 0x00E3C80C: a pre-sorted chain that is out of order is
             * sorted after all - drop the lock and start over */
            if (sorted < 0) {
                if (group_end->header[7] > req->header[7]) {    /* bls 0x00E3C81C */
                    sorted = 0;
                    ML_$UNLOCK((int16_t)disk_$que_dev_word(dev, DISK_QUE_DEV_LOCK_OFFSET));
                    goto sort_again;                            /* 0x00E3C830 */
                }
            }
            /* 0x00E3C834: cylinder change ends the run */
            if (req_cyl(req) != req_cyl(run)) {
                if (group_start->flags != 1) {                  /* 0x00E3C83E */
                    req_next(group_start)->reserved_18 = ARCH_PTR_TO_VA(group_end); /* 0x00E3C848 */
                }
                break;                                          /* 0x00E3C84E */
            }
            /* 0x00E3C850: contiguous with the group so far? */
            if ((uint32_t)((int32_t)chunk_len + (int32_t)group_end->header[7]) == req->header[7]) {
                group_start->flags++;                           /* 0x00E3C862 */
            } else {
                if (group_start->flags != 1) {                  /* 0x00E3C868 */
                    req_next(group_start)->reserved_18 = ARCH_PTR_TO_VA(group_end); /* 0x00E3C872 */
                }
                group_start = req;                              /* 0x00E3C876 */
                req->flags = 1;
            }
            group_end = req;                                    /* 0x00E3C87E */
            req = req_next(req);
        }
        run->reserved_18 = ARCH_PTR_TO_VA(group_end);           /* 0x00E3C88A */
        run = req;                                              /* 0x00E3C890 */
    }

    /* 0x00E3C89A - 0x00E3C8A4: close the last group */
    if (group_start->flags != 1) {
        req_next(group_start)->reserved_18 = ARCH_PTR_TO_VA(group_end);
    }

    /* 0x00E3C8AA - 0x00E3C8CC: the last run at or below cur_cyl */
    if (ahead == 0) {
        below = count;                                          /* 0x00E3C8AE */
    } else if (req_cyl(disk_$que_run(ahead)) == cur_cyl) {      /* 0x00E3C8C0 */
        below = ahead;
    } else {
        below = (uint16_t)(ahead - 1);                          /* 0x00E3C8CA */
    }

    /* 0x00E3C8CE - 0x00E3C8D6: terminator for the ascending walk */
    DISK_$QUE_ARRAY[(uint16_t)(count + 1)] = 0;

    /* 0x00E3C8DA - 0x00E3C8E6 */
    token = ML_$SPIN_LOCK(queue);

    /* 0x00E3C8EA - 0x00E3C8F2: `tst.w (0x4,A0)` - the direction bit */
    if ((int16_t)(queue->position >> 16) < 0) {
        /* 0x00E3C8F4 - 0x00E3C962: heading up.  Re-read the position now
         * that the queue is locked; if it moved, find `ahead` again. */
        pos = (uint16_t)((queue->position & DISK_QUE_POSITION_MASK) >> DISK_QUE_POSITION_SHIFT);
        if (pos != cur_cyl) {                                   /* 0x00E3C900 */
            ahead = 0;
            if (count != 0) {                                   /* 0x00E3C906 */
                n = (uint16_t)(count - 1);                      /* D1: dbf count */
                i = 1;                                          /* 0x00E3C910 */
                do {
                    if (!(pos > req_cyl(disk_$que_run(i)))) {   /* bhi 0x00E3C91C */
                        ahead = i;                              /* 0x00E3C91E */
                        break;
                    }
                    i++;
                } while (n-- != 0);                             /* dbf 0x00E3C926 */
            }
        }
        /* 0x00E3C92A - 0x00E3C930 */
        if (ahead != 0) {
            count = (uint16_t)(ahead - 1);
        }
        /* 0x00E3C932 - 0x00E3C946: runs below the head, descending */
        if (count != 0) {
            disk_$add_que_merge_descending(&queue->list_b, count, -1);
        }
        /* 0x00E3C948 - 0x00E3C962: runs at or above it, ascending */
        if (ahead != 0) {
            disk_$add_que_merge_ascending(&queue->list_a, ahead,
                                          (count == 0) ? -1 : 0, group_end);
        }
    } else {
        /* 0x00E3C964 - 0x00E3C9DE: heading down.  Same re-read; if the
         * position moved, find `below` again from the top of the array. */
        pos = (uint16_t)((queue->position & DISK_QUE_POSITION_MASK) >> DISK_QUE_POSITION_SHIFT);
        if (pos != cur_cyl) {                                   /* 0x00E3C970 */
            below = 0;
            if (count != 0) {                                   /* 0x00E3C976 */
                n = (uint16_t)(count - 1);                      /* D3: dbf count */
                i = count;                                      /* 0x00E3C980 */
                do {
                    if (!(pos < req_cyl(disk_$que_run(i)))) {   /* bcs 0x00E3C994 */
                        below = i;                              /* 0x00E3C996 */
                        break;
                    }
                    i--;
                } while (n-- != 0);                             /* dbf 0x00E3C99E */
            }
        }
        /* 0x00E3C9A2 - 0x00E3C9C4: runs above the head, ascending */
        if (below == 0) {
            count = 1;                                          /* 0x00E3C9A6 */
            disk_$add_que_merge_ascending(&queue->list_b, count, -1, group_end);
        } else if (count == below) {
            count = 0;                                          /* 0x00E3C9AE */
        } else {
            count = (uint16_t)(below + 1);                      /* 0x00E3C9B4 */
            if (count != 0) {                                   /* beq 0x00E3C9B6 */
                disk_$add_que_merge_ascending(&queue->list_b, count, -1, group_end);
            }
        }
        /* 0x00E3C9C6 - 0x00E3C9DE: runs at or below it, descending */
        if (below != 0) {
            disk_$add_que_merge_descending(&queue->list_a, below,
                                           (count == 0) ? -1 : 0);
        }
    }

    /* 0x00E3C9E0 - 0x00E3C9F8 */
    ML_$SPIN_UNLOCK(queue, token);
}
