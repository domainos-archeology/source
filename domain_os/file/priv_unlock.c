/*
 * FILE_$PRIV_UNLOCK - core file unlocking primitive
 *
 * Original address: 0x00E5FD32, 1658 bytes.
 * Module base A5 = 0x00E82128 (`lea (0xe82128).l,A5` at 0x00E5FD3A), so an
 * A5 displacement D in the listing is the global at 0xE82128 + D:
 *   (0x40,A5)  FILE_$LOCK_MAP_TABLE   (0x00E5FE... / 0x00E60272)
 *   (0xC8,A5)  FILE_$LOT_HASHTAB      (0x00E5FEDE, 0x00E600BE, 0x00E6010E)
 *   (0x2CE,A5) FILE_$LOT_FREE         (0x00E60132, 0x00E60138)
 *
 * There are no nested Pascal procedures: every call in the body is a `jsr`
 * to an absolute address.
 *
 * ---------------------------------------------------------------------------
 * Argument frame (link.w A6,-0xfc; 10 arguments, 32 bytes, A6+0x08..A6+0x27)
 * ---------------------------------------------------------------------------
 *   A6+0x08  long  file_uid    (movea.l at 0x00E5FD40 -> A2)
 *   A6+0x0C  long  lock_slot   pushed whole (`ext.l D0; move.l D0,-(SP)` in
 *                              FILE_$UNLOCK_PROC at 0x00E60F06); only its low
 *                              word A6+0x0E is ever read (0x00E5FDA2)
 *   A6+0x10  word  lock_mode   (0x00E5FD44 -> D3, live for the whole body)
 *   A6+0x12  word  asid        (0x00E5FD86, 0x00E5FDC0)
 *   A6+0x14  word  by_key      Pascal boolean; callers push it with
 *                              `st -(SP)` (0x00E607B2, 0x00E60D88,
 *                              0x00E60E18), which on the 68000 decrements A7
 *                              by two and stores the byte at the *even*
 *                              address, so the callee tests A6+0x14
 *                              (0x00E5FDB2, 0x00E601C4, 0x00E60348)
 *   A6+0x16  word  key         matched against entry->sequence (0x00E5FF1A)
 *   A6+0x18  long  rem_key     matched against entry->context  (0x00E5FF38)
 *   A6+0x1C  long  rem_node    matched against entry->node_low (0x00E5FF2E)
 *   A6+0x20  long  dtv_out     cleared at 0x00E5FD50, filled at 0x00E601DA
 *   A6+0x24  long  status_ret  (0x00E6038C / 0x00E60394)
 *
 * The three argument words at A6+0x10/0x12 and A6+0x14/0x16 are what the
 * previous prototype merged into a single `mode_asid` longword and a single
 * `remote_flags` longword; FILE_$VERIFY_LOCK_HOLDER (0x00E607AE..0x00E607B6)
 * pushes them as four separate items and is the proof.
 *
 * ---------------------------------------------------------------------------
 * Frame map (link.w A6,-0xfc)
 * ---------------------------------------------------------------------------
 *   A6-0x0FC  hash_bucket     pointer to FILE_$LOT_HASHTAB[hash] (0x00E600BA)
 *   A6-0x0F8  proc_row        0xEA202C + asid*300 (0x00E5FD98)
 *   A6-0x0DC  saw_other       boolean: another lock on the same UID remains
 *   A6-0x0D8  unlock_result   byte returned by REM_FILE_$UNLOCK / AST_$TRUNCATE
 *   A6-0x0D6  not_pending     boolean: (entry->flags2 & 0x02) == 0
 *   A6-0x0D4  other_exclusive boolean: a remaining lock is mode 4 or 11
 *   A6-0x0D0  did_unlock      boolean: we got as far as releasing an entry
 *   A6-0x0CE  dts_result      byte returned by AST_$SET_DTS
 *   A6-0x0CC  hash            UID_$HASH remainder
 *   A6-0x0CA  cur_entry       lock-table index being examined
 *   A6-0x0C4  prev_entry      predecessor in the hash chain walk
 *   A6-0x0C2  log_side        NETLOG argument: entry->flags2 bit 7
 *   A6-0x0BE  log_remote      NETLOG argument
 *   A6-0x0BC  log_by_key      NETLOG argument
 *   A6-0x0B6  entry_key       entry->sequence, forwarded to REM_FILE_$UNLOCK
 *   A6-0x0B4  dtv_zero        longword 0 handed to AST_$GET_DTV
 *   A6-0x0B0  local_status    the status this function ultimately reports
 *   A6-0x0AC  status2         scratch status for the calls whose failure is
 *                             deliberately swallowed
 *   A6-0x0A8  status3         REM_FILE_$UNLOCK's status
 *   A6-0x0A4  dts_scratch     AST_$SET_DTS / AST_$COND_FLUSH scratch longword
 *   A6-0x09C  entry_ctx       entry->context, forwarded to REM_FILE_$UNLOCK
 *   A6-0x098  desc            file_$obj_loc_t (uid at A6-0x90, loc_info at
 *                             A6-0x88, node at A6-0x84, flags at A6-0x7B)
 *   A6-0x078  attr_val        AST_$SET_ATTRIBUTE value scratch
 *   A6-0x040  attrs           ast_$common_attr_t (0x18 bytes) followed by the
 *                             REM_FILE_$LOCAL_READ_LOCK entry buffer at
 *                             attrs+0x18 (A6-0x28)
 */

#include "file/file_internal.h"
#include "ml/ml.h"
#include "netlog/netlog.h"

/*
 * ML resource id 5 = the lock-table lock (`move.w #0x5,-(SP)` before every
 * 0x00E20B12 / 0x00E20B62 call in this function).
 */
#define FILE_LOT_ML_LOCK        FILE_LOT_ML_LOCK_ID

/*
 * Constant cells the compiler placed in the code region and passed by
 * reference with `pea (d,PC)`.  (Address = instruction + 2 + displacement.)
 */

/* 0x00E5EA28: word 0x00FB = 251, the lock hash table modulus handed to
 * UID_$HASH at 0x00E5FD5A.  The image holds ONE such cell, shared with
 * FILE_$DELETE_INT and FILE_$LOCAL_READ_LOCK; it is defined in
 * file/file_data.c and declared in file/file_internal.h. */


/* Attribute ids written through AST_$SET_ATTRIBUTE. */
#define FILE_ATTR_DELETE_PENDING    0x07    /* 0x00E5FFD0 */
#define FILE_ATTR_LOCK_NODE         0x0B    /* 0x00E601B2 */

/* AST_$SET_DTS / AST_$GET_COMMON_ATTRIBUTES / AST_$PURIFY selector words. */
#define FILE_DTS_ON_UNLOCK          0x10    /* 0x00E60004 */
#define FILE_CATTR_SHORT            0x10    /* 0x00E5FF9E */
#define FILE_CATTR_LOCK             0x30    /* 0x00E60228 */
#define FILE_PURIFY_FLAGS           0x8000  /* high word of the longword
                                             * 0x80000000 pushed at
                                             * 0x00E60186; the callee reads
                                             * A6+0x0C as a word
                                             * (0x00E05688) and A6+0x0E as
                                             * the segment number */

/* NETLOG event kind for an unlock (`move.w #0x13,-(SP)` at 0x00E6036E). */
#define FILE_NETLOG_UNLOCK          0x13

/*
 * The 0x40-byte area at A6-0x40 is two records back to back: the
 * ast_$common_attr_t AST_$GET_COMMON_ATTRIBUTES fills (0x18 bytes) and, right
 * behind it at A6-0x28, the lock record REM_FILE_$LOCAL_READ_LOCK returns
 * (`pea (-0x28,A6)` at 0x00E6024A).  Six bytes of the frame area are unused.
 */
typedef struct file_priv_unlock_attrs_t {
    ast_$common_attr_t          cattr;      /* A6-0x40, 0x18 bytes */
    file_lock_info_internal_t   lock_info;  /* A6-0x28, 0x22 bytes */
    uint8_t                     spare[0x40 - 0x18 - 0x22];
} file_priv_unlock_attrs_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(file_priv_unlock_attrs_t, lock_info) == 0x18,
               "priv_unlock attrs.lock_info");
_Static_assert(sizeof(file_priv_unlock_attrs_t) == 0x40, "sizeof priv_unlock attrs");
#endif

/*
 * ============================================================================
 * FILE_$PRIV_UNLOCK (0x00E5FD32)
 * ============================================================================
 */
boolean FILE_$PRIV_UNLOCK(uid_t *file_uid, int32_t lock_slot,
                          uint16_t lock_mode, uint16_t asid,
                          boolean by_key, uint16_t key,
                          uint32_t rem_key, uint32_t rem_node,
                          uint32_t *dtv_out, status_$t *status_ret)
{
    /* --- frame ------------------------------------------------------- */
    uint16_t   *hash_bucket;                /* A6-0xFC */
    boolean     saw_other;                  /* A6-0xDC */
    boolean     unlock_result;              /* A6-0xD8 */
    boolean     not_pending;                /* A6-0xD6 */
    boolean     other_exclusive;            /* A6-0xD4 */
    boolean     did_unlock;                 /* A6-0xD0 */
    uint8_t     dts_result;                 /* A6-0xCE */
    int16_t     hash;                       /* A6-0xCC */
    int16_t     cur_entry;                  /* A6-0xCA */
    int16_t     prev_entry;                 /* A6-0xC4 */
    uint16_t    log_side;                   /* A6-0xC2 */
    uint16_t    log_remote;                 /* A6-0xBE */
    uint16_t    log_by_key;                 /* A6-0xBC */
    uint16_t    entry_key;                  /* A6-0xB6 */
    uint32_t    dtv_zero;                   /* A6-0xB4 */
    status_$t   local_status;               /* A6-0xB0 */
    status_$t   status2;                    /* A6-0xAC */
    status_$t   status3;                    /* A6-0xA8 */
    uint32_t    dts_scratch;                /* A6-0xA4 */
    uint32_t    entry_ctx;                  /* A6-0x9C */
    file_$obj_loc_t desc;                   /* A6-0x98 */
    union {                                 /* A6-0x78 */
        uint16_t w;
        uint32_t l;
    } attr_val;
    file_priv_unlock_attrs_t attrs;         /* A6-0x40 */

    /* --- registers --------------------------------------------------- */
    int16_t     slot;                       /* D4 */
    uint16_t    entry_mode;                 /* D4 after 0x00E6007A */
    boolean     is_exclusive;               /* D5 after 0x00E6009C */
    boolean     retry_read_lock;            /* D2 in the remote tail */
    file_lock_entry_detail_t *entry;
    file_lock_entry_detail_t *found;

    local_status  = 0;                                  /* 0x00E5FD48 */
    *dtv_out      = 0;                                  /* 0x00E5FD50 */
    unlock_result = 0;                                  /* 0x00E5FD52 */
    did_unlock    = 0;                                  /* 0x00E5FD56 */

    /*
     * 0x00E5FD5A: UID_$HASH(file_uid, &251).  Only the remainder (D0's low
     * word after the `swap`) is kept.
     */
    hash = (int16_t)(UID_$HASH(file_uid, &file_$lot_hash_modulus) & 0xFFFF);

    /*
     * 0x00E5FD6C-0x00E5FD82: mode 8 without `by_key`, and mode 9 outright,
     * skip the whole unlock body and fall straight through to the NETLOG
     * tail.  (`seq D1b` / `not.b D2b` / `and.b` / `bmi`.)
     */
    if (((lock_mode == 8) && (by_key >= 0)) || (lock_mode == 9)) {
        goto netlog;                                    /* 0x00E60328 */
    }

    /*
     * 0x00E5FD8A caches the caller's per-process lock row (0xEA202C +
     * asid*300) in A6-0xF8; FILE_$PROC_LOT_SLOT recomputes it at each use.
     */
retry:                                                  /* 0x00E5FD9C */
    cur_entry = 0;
    slot      = (int16_t)(lock_slot & 0xFFFF);          /* 0x00E5FDA2: only
                                                         * the low word of the
                                                         * longword argument */
    ML_$LOCK(FILE_LOT_ML_LOCK);                         /* 0x00E5FDAA */

    if (by_key < 0) {                                   /* 0x00E5FDB2 */
        /*
         * ----------------------------------------------------------------
         * Search the hash chain by (mode, key, rem_key, rem_node, uid)
         * 0x00E5FED2
         * ----------------------------------------------------------------
         */
        hash_bucket = &FILE_$LOT_HASHTAB[hash];
        cur_entry   = (int16_t)*hash_bucket;            /* 0x00E5FEDE */
        if (cur_entry > 0) {                            /* ble -> 0x00E5FF6C */
            for (;;) {                                  /* 0x00E5FEE8 */
                entry = FILE_$LOT_ENTRY(cur_entry);

                /* 0x00E5FEFE: mode must match, or lock_mode 0 matches any. */
                if ((((entry->flags2 & FILE_LOCK_F2_MODE_MASK)
                          >> FILE_LOCK_F2_MODE_SHIFT) == lock_mode) ||
                    (lock_mode == 0)) {
                    /* 0x00E5FF16: key must match, or key 0 matches any. */
                    if ((entry->sequence == key) || (key == 0)) {
                        /* 0x00E5FF2A / 0x00E5FF34 / 0x00E5FF3E */
                        if ((entry->node_low == rem_node) &&
                            (entry->context  == rem_key) &&
                            ((entry->flags2 & FILE_LOCK_F2_REMOTE) == 0) &&
                            (entry->uid_high == file_uid->high) &&
                            (entry->uid_low  == file_uid->low) &&
                            (entry->refcount != 0)) {
                            break;                      /* 0x00E5FF68 */
                        }
                    }
                }
                cur_entry = (int16_t)entry->next;       /* 0x00E5FF60 */
                if (cur_entry <= 0) {
                    break;
                }
            }
        }

        if (cur_entry == 0) {                           /* 0x00E5FF6C */
            /* 0x00E5FF6E */
            local_status = (lock_mode == 8)
                               ? file_$object_not_locked_by_this_process
                               : 0;
            ML_$UNLOCK(FILE_LOT_ML_LOCK);               /* 0x00E5FFE0 */
            goto netlog;                                /* 0x00E5FFEE */
        }

        if (lock_mode == 8) {                           /* 0x00E5FF84 */
            /*
             * Mode 8 is "mark delete-pending", not a real unlock: it never
             * touches the refcount.
             */
            desc.uid = *file_uid;                       /* 0x00E5FF8C */
            AST_$GET_COMMON_ATTRIBUTES(&desc, FILE_CATTR_SHORT,
                                       &attrs.cattr, &local_status);  /* 0x00E5FFA6 */
            /* 0x00E5FFB8 `move.b (-0x40,A6),D2b` then `tst.w D2w`: the
             * object's type byte, zero-extended to a word. */
            if ((local_status == 0) && (attrs.cattr.obj_type == 0)) {
                attr_val.w = 1;                         /* 0x00E5FFC0 */
                AST_$SET_ATTRIBUTE(file_uid, FILE_ATTR_DELETE_PENDING,
                                   &attr_val, &local_status);  /* 0x00E5FFD6 */
            }
            ML_$UNLOCK(FILE_LOT_ML_LOCK);               /* 0x00E5FFE0 */
            goto netlog;                                /* 0x00E5FFEE */
        }
        /* fall through to the unlock body (0x00E5FFF2) */
    } else if (slot == 0) {
        /*
         * ----------------------------------------------------------------
         * Search this process' lock row for a matching entry
         * 0x00E5FDC0
         * ----------------------------------------------------------------
         */
        int16_t count = (int16_t)FILE_$PROC_LOT_COUNT(asid) - 1;

        if (count >= 0) {                               /* 0x00E5FDD6 bmi */
            int16_t i = 1;                              /* D1 */

            do {                                        /* 0x00E5FDDE..dbf */
                cur_entry = (int16_t)FILE_$PROC_LOT_SLOT(asid, i);
                if (cur_entry != 0) {
                    entry = FILE_$LOT_ENTRY(cur_entry);

                    /* 0x00E5FE08: flags2 bit 0 set, or lock_mode 0. */
                    if (((entry->flags2 & FILE_LOCK_F2_FLAG0) != 0) ||
                        (lock_mode == 0)) {
                        if ((entry->uid_high == file_uid->high) &&
                            (entry->uid_low  == file_uid->low)) {
                            uint16_t m = (entry->flags2
                                              & FILE_LOCK_F2_MODE_MASK)
                                         >> FILE_LOCK_F2_MODE_SHIFT;
                            if ((m == lock_mode) || (lock_mode == 0)) {
                                slot = i;               /* 0x00E5FE38 */
                                break;
                            }
                        }
                    }
                }
                i++;                                    /* 0x00E5FE3C */
                count--;
            } while (count != -1);                      /* dbf D2w */
        }

        if (slot == 0) {                                /* 0x00E5FE44/46 */
            local_status = file_$object_not_locked_by_this_process;
            goto unlock_and_test;                       /* 0x00E5FEB2 */
        }

        FILE_$PROC_LOT_SLOT(asid, slot) = 0;            /* 0x00E5FEBE */
        /* fall through to the unlock body (0x00E5FFF2) */
    } else if ((uint16_t)slot <= 0x96) {
        /*
         * ----------------------------------------------------------------
         * Explicit per-process slot number
         * 0x00E5FE5C
         * ----------------------------------------------------------------
         */
        cur_entry = (int16_t)FILE_$PROC_LOT_SLOT(asid, slot);
        if (cur_entry == 0) {                           /* 0x00E5FE6E */
            local_status = file_$object_not_locked_by_this_process;
            goto unlock_and_test;
        }
        entry = FILE_$LOT_ENTRY(cur_entry);
        if ((entry->uid_high != file_uid->high) ||      /* 0x00E5FE92 */
            (entry->uid_low  != file_uid->low)) {
            local_status = file_$object_not_locked_by_this_process;
            goto unlock_and_test;
        }
        if (lock_mode != 0) {                           /* 0x00E5FE9A */
            uint16_t m = (entry->flags2 & FILE_LOCK_F2_MODE_MASK)
                         >> FILE_LOCK_F2_MODE_SHIFT;
            /* 0x00E5FEA6 / 0x00E5FEAA: mode must match and, unlike the
             * search-by-uid path above, flags2 bit 0 must be *clear*. */
            if ((m != lock_mode) ||
                ((entry->flags2 & FILE_LOCK_F2_FLAG0) != 0)) {
                local_status = file_$object_not_locked_by_this_process;
                goto unlock_and_test;
            }
        }

        FILE_$PROC_LOT_SLOT(asid, slot) = 0;            /* 0x00E5FEBE */
        /* fall through to the unlock body (0x00E5FFF2) */
    } else {
        local_status = file_$invalid_arg;               /* 0x00E5FE50 */
        goto unlock_and_test;                           /* 0x00E6030E */
    }

    /*
     * --------------------------------------------------------------------
     * Release the entry (0x00E5FFF2)
     * --------------------------------------------------------------------
     */
    did_unlock = -1;                                    /* 0x00E5FFF2 st */

    /* 0x00E60008: both the dtv and the access-time argument are the same
     * scratch longword (`move.l (SP),-(SP)` duplicates the pushed address). */
    dts_result = AST_$SET_DTS(FILE_DTS_ON_UNLOCK, file_uid,
                              &dts_scratch, &dts_scratch, &status2);

    found = FILE_$LOT_ENTRY(cur_entry);                 /* A1, 0x00E6002E */

    desc.uid      = *file_uid;                          /* 0x00E60032 */
    desc.loc_info = found->node_high;                   /* 0x00E6003A: entry+0x08 */
    desc.node     = found->node_low;                    /* 0x00E60040: entry+0x04 */

    /* 0x00E60046-0x00E6005C: desc.flags bit 7 = entry is remote,
     * bit 6 set unconditionally. */
    desc.flags = (int8_t)((desc.flags & 0x7F)
                          | ((found->flags2 & FILE_LOCK_F2_REMOTE) ? 0x80 : 0));
    desc.flags = (int8_t)(desc.flags | FILE_OBJ_LOC_SCRATCH);

    not_pending = ((found->flags2 & FILE_LOCK_F2_PENDING) == 0) ? -1 : 0;
    entry_key   = found->sequence;                      /* 0x00E6006E */
    entry_ctx   = found->context;                       /* 0x00E60074 */
    entry_mode  = (found->flags2 & FILE_LOCK_F2_MODE_MASK)
                  >> FILE_LOCK_F2_MODE_SHIFT;           /* 0x00E6007A */
    log_side    = (uint16_t)((found->flags2 & FILE_LOCK_F2_SIDE) >> 7);
    is_exclusive = ((entry_mode == 4) || (entry_mode == 0x0B)) ? -1 : 0;

    found->refcount--;                                  /* 0x00E6009E */
    if (found->refcount != 0) {                         /* 0x00E600AA */
        goto unlock_and_test;                           /* 0x00E6030E */
    }

    /*
     * Refcount hit zero: unlink the entry and, on the way, notice whether any
     * other lock on the same UID survives (0x00E600AE).
     */
    hash_bucket     = &FILE_$LOT_HASHTAB[hash];         /* 0x00E600B6 */
    prev_entry      = 0;                                /* 0x00E600C2 */
    other_exclusive = 0;                                /* 0x00E600C6 */
    saw_other       = 0;                                /* 0x00E600CA */

    {
        int16_t walk = (int16_t)*hash_bucket;           /* D1, 0x00E600BE */

        while (walk > 0) {                              /* 0x00E60162 */
            int16_t next;                               /* D2 */

            entry = FILE_$LOT_ENTRY(walk);
            next  = (int16_t)entry->next;               /* 0x00E600EA */

            if ((entry->uid_high == file_uid->high) &&  /* 0x00E600F6 */
                (entry->uid_low  == file_uid->low)) {
                if (walk == cur_entry) {                /* 0x00E600FE */
                    if (prev_entry == 0) {              /* 0x00E60104 */
                        *hash_bucket = found->next;     /* 0x00E6010E */
                    } else {
                        FILE_$LOT_ENTRY(prev_entry)->next = found->next;
                    }                                   /* 0x00E6012C */
                    found->next    = FILE_$LOT_FREE;    /* 0x00E60132 */
                    FILE_$LOT_FREE = (uint16_t)cur_entry;   /* 0x00E60138 */
                } else {
                    uint16_t m;

                    saw_other = -1;                     /* 0x00E60140 */
                    m = (entry->flags2 & FILE_LOCK_F2_MODE_MASK)
                        >> FILE_LOCK_F2_MODE_SHIFT;
                    if ((m == 4) || (m == 0x0B)) {
                        other_exclusive = -1;           /* 0x00E60158 */
                    }
                }
            }

            /* 0x00E6015C: the original advances `prev` on every iteration,
             * including the one that unlinked `found`. */
            prev_entry = walk;
            walk       = next;
        }
    }

    /*
     * 0x00E60168: we dropped the last exclusive lock and the UID is a real
     * object (its high byte is non-zero) - purify it.
     */
    if (((is_exclusive & ~other_exclusive) < 0) &&
        (((file_uid->high >> 24) & 0xFF) != 0)) {
        /* 0x00E6017A: the longword 0x80000000 pushed at A6+0x0C reaches
         * AST_$PURIFY as flags = 0x8000 and segment = 0. */
        AST_$PURIFY(file_uid, FILE_PURIFY_FLAGS, 0,
                    &file_$nil_cell, 0, &local_status);

        /* 0x00E60198: no other lock survives and the entry was local. */
        if ((saw_other < 0) && (desc.flags >= 0)) {
            attr_val.l = 0;                             /* 0x00E601A4 */
            AST_$SET_ATTRIBUTE(file_uid, FILE_ATTR_LOCK_NODE,
                               &attr_val, &status2);    /* 0x00E601B8 */
        }
    }

    /*
     * 0x00E601C2: `by_key` unlocks of an exclusive lock report the object's
     * data-time-valid back to the remote requester.
     */
    if (((is_exclusive & by_key) < 0) &&
        (((file_uid->high >> 24) & 0xFF) != 0)) {
        dtv_zero = 0;                                   /* 0x00E601D2 */
        AST_$GET_DTV(file_uid, dtv_zero, dtv_out, &status2);    /* 0x00E601E4 */
        if (status2 != 0) {                             /* 0x00E601EE */
            *dtv_out = 0;
        }
    }

    ML_$UNLOCK(FILE_LOT_ML_LOCK);                       /* 0x00E601FA */

    if (desc.flags < 0) {                               /* 0x00E60208 */
        /*
         * ----------------------------------------------------------------
         * The object lives on another node: tell its file server
         * 0x00E60210
         * ----------------------------------------------------------------
         */
        retry_read_lock = 0;                            /* clr.b D2b */

        if (not_pending < 0) {                          /* 0x00E60212 */
            if (saw_other >= 0) {                       /* 0x00E60218 */
                AST_$GET_COMMON_ATTRIBUTES(&desc, FILE_CATTR_LOCK,
                                           &attrs.cattr, &status2); /* 0x00E60230 */
                /* 0x00E60240 `tst.w (-0x2c,A6)`: the object reference count.
                 * Only an unreferenced object is worth asking the lock holder
                 * about. */
                if ((status2 == 0) && (attrs.cattr.refcount == 0)) {
                    REM_FILE_$LOCAL_READ_LOCK(&desc.loc_info, &desc.uid,
                                              &attrs.lock_info,
                                              &status2);        /* 0x00E60256 */
                    if (status2 == file_$object_not_locked_by_this_process) {
                        retry_read_lock = -1;           /* 0x00E6026A */
                    }
                }
            }

            /* 0x00E6026C: re-test, exactly as the original does. */
            if (not_pending < 0) {
                entry_mode = FILE_$LOCK_MAP_TABLE[entry_mode];  /* 0x00E60272 */
            }
        }

        /* 0x00E60298 */
        unlock_result = REM_FILE_$UNLOCK(&desc, entry_mode, entry_ctx,
                                         entry_key, NODE_$ME,
                                         (boolean)dts_result, &status3);

        if (local_status == 0) {                        /* 0x00E602A6 */
            local_status = status3;
        }

        if (status3 == 0) {                             /* 0x00E602B2 */
            if (retry_read_lock < 0) {                  /* 0x00E602B8 */
                /* 0x00E602BC: same AST_$TRUNCATE call as the local path,
                 * but reporting into status2 instead of local_status. */
                AST_$TRUNCATE(file_uid, 0, 1, &unlock_result, &status2);
            } else if ((int8_t)unlock_result < 0) {     /* 0x00E602C4 tst.b D0b */
                dts_scratch = 0;                        /* 0x00E602C8 */
                AST_$COND_FLUSH(file_uid, &dts_scratch, &status2);
            }
        }
    } else if ((saw_other >= 0) &&                      /* 0x00E602E2 */
               (((file_uid->high >> 24) & 0xFF) != 0)) {
        /* 0x00E602F0 */
        AST_$TRUNCATE(file_uid, 0, 1, &unlock_result, &local_status);
    }
    goto loop_test;                                     /* 0x00E6031C */

unlock_and_test:                                        /* 0x00E6030E */
    ML_$UNLOCK(FILE_LOT_ML_LOCK);

loop_test:                                              /* 0x00E6031C */
    /* Mode 0 means "release every lock this process holds on the object", so
     * the whole search runs again until it fails or errors. */
    if ((local_status == 0) && (lock_mode == 0)) {
        goto retry;                                     /* 0x00E60324 */
    }

netlog:                                                 /* 0x00E60328 */
    /*
     * `desc.flags` and `log_side` are only assigned on the paths that got as
     * far as 0x00E5FFF2; the original reads whatever the frame happens to
     * hold on the early-exit paths, and that is preserved here.
     */
    if ((NETLOG_$OK_TO_LOG < 0) && (local_status == 0)) {
        log_remote = (desc.flags < 0) ? 1 : 0;          /* 0x00E60336 */
        log_by_key = (by_key < 0) ? 1 : 0;              /* 0x00E60348 */
        NETLOG_$LOG_IT(FILE_NETLOG_UNLOCK, (uint32_t *)file_uid,
                       0, 0, log_side, lock_mode,
                       log_remote, log_by_key);         /* 0x00E60372 */
    }

    /*
     * 0x00E6037C: "not locked by this process" is not an error once we have
     * actually released something (the mode-0 loop always ends that way).
     * The `seq D0b` at 0x00E6039C reads the Z flag left by the store, so the
     * result is `unlock_result` exactly when the reported status is zero.
     */
    if ((local_status == file_$object_not_locked_by_this_process) &&
        (did_unlock < 0)) {
        *status_ret = 0;
        return (boolean)unlock_result;
    }

    *status_ret = local_status;
    return (local_status == 0) ? (boolean)unlock_result : 0;
}
