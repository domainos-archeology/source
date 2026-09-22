/*
 * DBUF_$GET_BLOCK - Look a disk block up in the buffer cache, reading it in
 *
 * 0x00E3A5B0 - 0x00E3A8B4 (774 bytes, A5 = 0xE78B58, the DBUF data block).
 * Re-emitted from the disassembly on 2026-09-19.  Wrong before: the victim
 * search walked FORWARD from the head (the image walks backward from the
 * tail through `prev`, 0x00E3A6A2-0x00E3A6BC, so the least recently used
 * free buffer is taken); after a read or writeback the code returned the
 * buffer directly with ref_count = 1 (the image goes back to the search
 * at 0x00E3A618 and lets the cache hit path claim it); the dirty test was
 * a big-endian word cast; and the header handed to DISK_$READ /
 * DISK_$WRITE was a two-longword local, not the eight-longword frame area.
 *
 * Arguments (see dbuf/dbuf.h for the two-word tail):
 *   (0x8,A6)  vol_idx     word (D6)
 *   (0xa,A6)  block       longword disk address (D2)
 *   (0xe,A6)  uid         -> uid_t (A2)
 *   (0x12,A6) block_hint  longword (D3)
 *   (0x16,A6) block_type  word (D4), stored as a byte
 *   (0x18,A6) flags       word (D5): DBUF_GET_NO_READ, DBUF_GET_OK_IF_STOPPED
 *   (0x1a,A6) status      -> status_$t (A3)
 * Result: A0 = the buffer's VA (A2), or NULL after a failed read.
 *
 * Frame: (-0x2e,A6) spin token, (-0x24,A6) eventcount value + 1,
 * (-0x20,A6) the eight-longword header area.
 *
 * Control flow, by label:
 *   retry  (0x00E3A608) take the spin lock
 *   search (0x00E3A618) snapshot the eventcount, scan the LRU list
 *   settle (0x00E3A836) after a transfer: lock, clear busy; with no
 *          waiters fall straight back into `search` still holding the
 *          lock, otherwise unlock, EC_$ADVANCE and `retry`
 *   wait   (0x00E3A872) count as a waiter, EC_$WAIT, `retry`
 */

#include "dbuf/dbuf_internal.h"

/* 0x00E3A5FA: `move.w #0x10,-(SP)` - the NETLOG record kind */
#define DBUF_NETLOG_KIND_GET_BLOCK  0x10

void *DBUF_$GET_BLOCK(uint16_t vol_idx, int32_t block, uid_t *uid,
                      uint32_t block_hint, uint16_t block_type,
                      uint16_t flags, status_$t *status)
{
    ml_$spin_token_t token;         /* (-0x2e,A6) */
    int32_t wait_value;             /* (-0x24,A6) */
    uint32_t header[8] = {0};       /* (-0x20,A6); zeroed where the image
                                     * leaves frame contents */
    dbuf_$entry_t *e;               /* A4 */
    dbuf_$entry_t *h;
    uint32_t result;                /* A2 */

    /* 0x00E3A5DA */
    *status = status_$ok;

    /* 0x00E3A5DC - 0x00E3A604 */
    if (NETLOG_$OK_TO_LOG < 0) {
        NETLOG_$LOG_IT(DBUF_NETLOG_KIND_GET_BLOCK, (uint32_t *)uid,
                       (uint16_t)(block_hint >> 5), (uint16_t)(block_hint & 0x1F),
                       block_type, vol_idx, 0, 0);
    }

retry:
    /* 0x00E3A608 - 0x00E3A614 */
    token = ML_$SPIN_LOCK(&DBUF_SPIN_LOCK);

search:
    /* 0x00E3A618 - 0x00E3A61C */
    wait_value = dbuf_$eventcount.value + 1;

    /* 0x00E3A620 - 0x00E3A6A0: cache lookup, head to tail */
    e = dbuf_$entry_ptr(dbuf_$head);
    for (;;) {
        if (e->block == block && DBUF_GET_VOL(e) == vol_idx) {
            /* 0x00E3A634: `tst.w (0xc,A4)` - busy is bit 15 of the word */
            if ((e->flags & DBUF_ENTRY_BUSY) != 0) {
                goto wait;
            }
            e->ref_count++;                                     /* 0x00E3A63C */
            if ((flags & DBUF_GET_NO_READ) != 0) {              /* 0x00E3A640 */
                e->uid.high = uid->high;
                e->uid.low = uid->low;
                e->hint = block_hint;
                e->type = (uint8_t)block_type;
            }
            /* 0x00E3A658 - 0x00E3A67C: unlink and push to the head */
            if (e->prev != 0) {
                dbuf_$entry_ptr(e->prev)->next = e->next;
                if (e->next != 0) {
                    dbuf_$entry_ptr(e->next)->prev = e->prev;
                }
                h = dbuf_$entry_ptr(dbuf_$head);
                h->prev = ARCH_PTR_TO_VA(e);
                e->next = dbuf_$head;
                e->prev = 0;
                dbuf_$head = ARCH_PTR_TO_VA(e);
            }
            /* 0x00E3A680 - 0x00E3A692 */
            result = e->data;
            ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
            goto done;
        }
        if (e->next == 0) {
            break;
        }
        e = dbuf_$entry_ptr(e->next);
    }

    /* 0x00E3A6A2 - 0x00E3A6BC: e is the tail; walk back to the first
     * entry that is neither referenced nor busy */
    for (;;) {
        if (e->ref_count == 0 && (e->flags & DBUF_ENTRY_BUSY) == 0) {
            break;
        }
        if (e->prev == 0) {
            goto wait;
        }
        e = dbuf_$entry_ptr(e->prev);
    }

    /* 0x00E3A6BE - 0x00E3A6C6: `btst.l #0xe` on the word = dirty */
    if ((e->flags & DBUF_ENTRY_DIRTY) != 0) {
        /* 0x00E3A6C8 - 0x00E3A736: write the victim back */
        e->flags |= DBUF_ENTRY_BUSY;
        ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
        dbuf_$fill_write_header(e, header);
        e->flags &= (uint8_t)~DBUF_ENTRY_DIRTY;
        DISK_$WRITE((int16_t)DBUF_GET_VOL(e), (uint32_t)e->block, e->ppn,
                    header, status);
        if (*status != status_$ok) {
            DBUF_$TROUBLE |= (uint16_t)(1u << DBUF_GET_VOL(e));
            *status = status_$ok;
        }
        goto settle;
    }

    /* 0x00E3A73A - 0x00E3A75E: push the clean victim to the head */
    if (e->prev != 0) {
        dbuf_$entry_ptr(e->prev)->next = e->next;
        if (e->next != 0) {
            dbuf_$entry_ptr(e->next)->prev = e->prev;
        }
        h = dbuf_$entry_ptr(dbuf_$head);
        h->prev = ARCH_PTR_TO_VA(e);
        e->next = dbuf_$head;
        e->prev = 0;
        dbuf_$head = ARCH_PTR_TO_VA(e);
    }

    /* 0x00E3A762 - 0x00E3A794: claim it.  `or.b D6b` ORs the whole low
     * byte of vol_idx in, not just its low nibble. */
    e->flags = (uint8_t)((e->flags & 0xF0) | (uint8_t)vol_idx);
    e->block = block;
    e->uid.high = uid->high;
    e->uid.low = uid->low;
    e->hint = block_hint;
    e->type = (uint8_t)block_type;
    e->flags |= DBUF_ENTRY_BUSY;
    ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);

    /* 0x00E3A79C */
    if ((flags & DBUF_GET_NO_READ) != 0) {
        goto settle;
    }

    /* 0x00E3A7A4 - 0x00E3A7CE: read it in; the header area carries the
     * caller's uid and hint */
    header[0] = uid->high;
    header[1] = uid->low;
    header[2] = block_hint;
    DISK_$READ((int16_t)vol_idx, (uint32_t)block, e->ppn, header, status);
    if (*status == status_$ok) {
        goto settle;
    }
    /* 0x00E3A7D0 - 0x00E3A7DC */
    if ((flags & DBUF_GET_OK_IF_STOPPED) != 0 &&
        *status == status_$storage_module_stopped) {
        goto settle;
    }

    /* 0x00E3A7DE - 0x00E3A834: give the buffer up, flag the status */
    e->flags &= 0xF0;
    e->block = -1;
    token = ML_$SPIN_LOCK(&DBUF_SPIN_LOCK);
    e->flags &= (uint8_t)~DBUF_ENTRY_BUSY;
    if (dbuf_$waiters != 0) {
        ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
        EC_$ADVANCE(&dbuf_$eventcount);
    } else {
        ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
    }
    *status |= (status_$t)0x80000000;                           /* bset.b #7,(A3) */
    result = 0;
    goto done;

settle:
    /* 0x00E3A836 - 0x00E3A86E */
    token = ML_$SPIN_LOCK(&DBUF_SPIN_LOCK);
    e->flags &= (uint8_t)~DBUF_ENTRY_BUSY;
    if (dbuf_$waiters == 0) {
        goto search;                                            /* lock kept */
    }
    ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
    EC_$ADVANCE(&dbuf_$eventcount);
    goto retry;

wait:
    /* 0x00E3A872 - 0x00E3A8A6: ecs = { &ec, 0, 0 }, vals = { wait, 0, 0 }
     * (`move.l (SP),-(SP)` duplicates the zero) */
    dbuf_$waiters++;
    ML_$SPIN_UNLOCK(&DBUF_SPIN_LOCK, token);
    EC_$WAIT((ec_$wait_ecs_t){ { &dbuf_$eventcount, NULL, NULL } },
             (ec_$wait_vals_t){ { wait_value, 0, 0 } });
    dbuf_$waiters--;
    goto retry;

done:
    /* 0x00E3A8AA */
    return ARCH_VA_TO_PTR(result);
}
