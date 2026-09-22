/*
 * DISK_$SORT - Sort a request chain by disk address and pull near sectors up
 *
 * 0x00E3C3E4 - 0x00E3C596 (436 bytes) plus the nested procedure at
 * 0x00E3C370 - 0x00E3C3E2 (116 bytes, disk_$sort_swap_entries).
 * Re-emitted from the disassembly on 2026-09-19.  Wrong before: the
 * coalescing pass (0x00E3C4B8 - 0x00E3C586) was a read-only scan with the
 * swap left unemitted, and the sort loops were a different exchange sort.
 *
 * Arguments:
 *   (0x8,A6) vol        the volume descriptor (+0x7c form, A2): dev_info
 *                       (+0x18) -> driver record whose word +0x08 bit 9
 *                       selects the sort key, and bat_step (+0x26)
 *   (0xc,A6) queue_ptr  -> VA of the first request (A3); read once into
 *                       the frame and written back at 0x00E3C58A
 *
 * Frame (the five list cursors the nested procedure reaches through the
 * static link):
 *   -0x14 head      -0x0c cur      -0x08 prev_cur
 *   -0x10 nxt       -0x04 prev_nxt
 *
 * Pass 1 (0x00E3C406 or 0x00E3C460): for each `cur`, every later `nxt`
 * with a smaller key (unsigned `bcc`) is swapped with it; the key is
 * header[7] (+0x3c, the absolute disk address) unless the driver's bit 9
 * is set, in which case it is daddr (+0x04).
 *
 * Pass 2 (0x00E3C4B8), skipped when bat_step == 1: walking `cur` behind
 * `prev_cur`, a `cur` on the same cylinder and head as `prev_cur` and
 * fewer than bat_step sectors after it makes the scan continue down the
 * chain for the first later request on that cylinder/head whose sector
 * distance from `prev_cur` is at least bat_step; that request is swapped
 * with `cur` (0x00E3C560).  Any cylinder/head change ends the inner scan.
 */

#include "disk/disk_internal.h"

/* 0x00E3C400: `btst.l #0x9` on the driver record's word +0x08 */
#define DISK_SORT_DEV_FLAG_BY_DADDR   0x0200

static inline disk_io_req_t *req_at(uint32_t va)
{
    return (disk_io_req_t *)ARCH_VA_TO_PTR(va);
}

static inline uint16_t req_cyl(uint32_t va)
{
    return (uint16_t)(req_at(va)->daddr >> 16);
}

static inline uint16_t req_head(uint32_t va)
{
    return (uint16_t)((req_at(va)->daddr >> 8) & 0xFF);
}

static inline uint16_t req_sector(uint32_t va)
{
    return (uint16_t)(req_at(va)->daddr & 0xFF);
}

/*
 * 0x00E3C370 - 0x00E3C3E2: exchange the requests `cur` and `nxt` within
 * the chain.  Nested procedure: every operand is a parent frame slot
 * ((-0x14,A0) head, (-0xc,A0) cur, (-0x8,A0) prev_cur, (-0x10,A0) nxt,
 * (-0x4,A0) prev_nxt with A0 = the parent's A6).  Afterwards `cur` names
 * the request now in cur's old place (the old nxt) and `nxt` the old cur.
 */
static void disk_$sort_swap_entries(uint32_t *head, uint32_t *cur,
                                    uint32_t *prev_cur, uint32_t *nxt,
                                    uint32_t *prev_nxt)
{
    uint32_t cur_next;              /* D0 at 0x00E3C38E */
    uint32_t old_cur;               /* D0 at 0x00E3C3BC */

    /* 0x00E3C37A - 0x00E3C384 */
    if (*cur == *head) {
        *head = *nxt;
    }
    /* 0x00E3C38A - 0x00E3C3A0 */
    cur_next = req_at(*cur)->next;
    req_at(*prev_cur)->next = *nxt;
    req_at(*cur)->next = req_at(*nxt)->next;
    /* 0x00E3C3A2 - 0x00E3C3B8: adjacent, or with requests between */
    if (cur_next == *nxt) {
        req_at(*nxt)->next = *cur;
    } else {
        req_at(*nxt)->next = cur_next;
        req_at(*prev_nxt)->next = *cur;
    }
    /* 0x00E3C3BC - 0x00E3C3D6 */
    old_cur = *cur;
    if (*prev_cur == *cur) {
        *prev_cur = *nxt;
    }
    *cur = *nxt;
    *nxt = old_cur;
}

void DISK_$SORT(void *vol, void **queue_ptr)
{
    disk_$volume_t *v = (disk_$volume_t *)vol;                  /* A2 */
    const uint8_t *dev = (const uint8_t *)v->dev_info;          /* 0x00E3C3F8 */
    uint32_t head;                  /* (-0x14,A6) */
    uint32_t cur;                   /* (-0xc,A6) */
    uint32_t prev_cur;              /* (-0x8,A6) */
    uint32_t nxt;                   /* (-0x10,A6) */
    uint32_t prev_nxt;              /* (-0x4,A6) */
    uint16_t dist;                  /* D0w in pass 2 */

    /* 0x00E3C3F4 */
    head = ARCH_PTR_TO_VA(*queue_ptr);

    /* 0x00E3C3FC - 0x00E3C4B6: pass 1 */
    if ((*(const uint16_t *)(dev + 0x08) & DISK_SORT_DEV_FLAG_BY_DADDR) == 0) {
        /* 0x00E3C406: key = header[7] */
        cur = head;
        prev_cur = cur;
        while (cur != 0) {
            prev_nxt = cur;
            nxt = req_at(cur)->next;
            while (nxt != 0) {
                if (req_at(nxt)->header[7] < req_at(cur)->header[7]) {  /* bcc 0x00E3C430 */
                    disk_$sort_swap_entries(&head, &cur, &prev_cur, &nxt, &prev_nxt);
                }
                prev_nxt = nxt;
                nxt = req_at(nxt)->next;
            }
            prev_cur = cur;
            cur = req_at(cur)->next;
        }
    } else {
        /* 0x00E3C460: key = daddr */
        cur = head;
        prev_cur = cur;
        while (cur != 0) {
            prev_nxt = cur;
            nxt = req_at(cur)->next;
            while (nxt != 0) {
                if (req_at(nxt)->daddr < req_at(cur)->daddr) {          /* bcc 0x00E3C48A */
                    disk_$sort_swap_entries(&head, &cur, &prev_cur, &nxt, &prev_nxt);
                }
                prev_nxt = nxt;
                nxt = req_at(nxt)->next;
            }
            prev_cur = cur;
            cur = req_at(cur)->next;
        }
    }

    /* 0x00E3C4B8 - 0x00E3C586: pass 2, unless bat_step == 1 */
    if (v->bat_step != 1) {
        prev_cur = head;
        cur = req_at(head)->next;                               /* 0x00E3C57E */
        while (cur != 0) {
            /* 0x00E3C4D0 - 0x00E3C516: same cylinder and head, and
             * fewer than bat_step sectors after prev_cur (signed `bge`) */
            if (req_cyl(prev_cur) == req_cyl(cur) &&
                req_head(prev_cur) == req_head(cur)) {
                dist = (uint16_t)(req_sector(cur) - req_sector(prev_cur));
                if ((int16_t)dist < (int16_t)v->bat_step) {
                    /* 0x00E3C566 - 0x00E3C572 */
                    prev_nxt = cur;
                    nxt = req_at(cur)->next;
                    while (nxt != 0) {
                        /* 0x00E3C51A - 0x00E3C55E */
                        if (req_cyl(prev_cur) != req_cyl(nxt) ||
                            req_head(prev_cur) != req_head(nxt)) {
                            break;
                        }
                        dist = (uint16_t)(req_sector(nxt) - req_sector(prev_cur));
                        if ((int16_t)dist >= (int16_t)v->bat_step) {
                            disk_$sort_swap_entries(&head, &cur, &prev_cur, &nxt, &prev_nxt);
                            break;                              /* 0x00E3C564 */
                        }
                        prev_nxt = nxt;
                        nxt = req_at(nxt)->next;
                    }
                }
            }
            /* 0x00E3C574 - 0x00E3C586 */
            prev_cur = cur;
            cur = req_at(cur)->next;
        }
    }

    /* 0x00E3C58A */
    *queue_ptr = ARCH_VA_TO_PTR(head);
}
