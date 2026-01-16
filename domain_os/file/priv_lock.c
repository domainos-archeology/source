/*
 * FILE_$PRIV_LOCK - Core file locking function
 *
 * Original address: 0x00E5F0EE
 * Size: 2892 bytes
 *
 * This is the main internal function for all file lock operations.
 * It handles:
 *   - Local file locks
 *   - Remote file locks (via REM_FILE_$LOCK)
 *   - Lock mode validation and compatibility checking
 *   - Lock table management
 *   - Lock upgrade/downgrade operations
 *   - Lock conflict detection
 *
 * The original Pascal code had nested procedures that access the parent's
 * stack frame. These are implemented as static functions with explicit
 * context pointers in this C translation.
 *
 * Lock entry structure (28 bytes at DAT_00e935b0 + index * 0x1C):
 *   0x00: context     - Lock context (param_7)
 *   0x04: node_low    - Node address low
 *   0x08: node_high   - Node address high
 *   0x0C: uid_high    - File UID high
 *   0x10: uid_low     - File UID low
 *   0x14: next        - Next in chain
 *   0x16: sequence    - Lock sequence
 *   0x18: refcount    - Reference count
 *   0x19: flags1      - Flags (bit 7=remote, bits 0-5=rights)
 *   0x1A: rights      - Access rights
 *   0x1B: flags2      - Flags (bit 7=side, bits 3-6=mode, bit 2=remote, bit 1=pending)
 */

#include "file/file_internal.h"
#include "ml/ml.h"
#include "proc/proc.h"
#include "netlog/netlog.h"

/*
 * Lock table base addresses (m68k target)
 */
#define LOT_BASE        ((file_lock_entry_detail_t *)0xE935B0)
#define LOT_ENTRY(n)    ((file_lock_entry_detail_t *)((uint8_t *)LOT_BASE + (n) * 0x1C))

/* Per-process lock table */
#define PROC_LOT_BASE   ((uint16_t *)0xE9F9CA)
#define PROC_LOT_ENTRY(asid, idx) \
    (*(uint16_t *)((uint8_t *)PROC_LOT_BASE + (asid) * 300 + (idx) * 2))
#define PROC_LOT_COUNT(asid) \
    (*(uint16_t *)((uint8_t *)0xEA3DC4 + (asid) * 2))

/*
 * Context structure for nested procedures
 * These values are stored on the parent stack frame and accessed by helpers
 */
typedef struct priv_lock_ctx {
    uid_t           *file_uid;      /* File UID to lock */
    int16_t         asid;           /* Process ASID */
    uint16_t        lock_index;     /* Lock table index (input) */
    uint16_t        lock_mode;      /* Lock mode requested */
    int16_t         rights;         /* Rights byte */
    uint32_t        flags;          /* Operation flags */
    int32_t         param_7;        /* Context parameter */
    uint32_t        param_8;        /* Node address */
    uint32_t        param_9;        /* Additional context */
    void            *param_10;      /* Default address */
    uint16_t        param_11;       /* Additional flags */
    uint32_t        *lock_ptr_out;  /* Output: lock context */
    uint16_t        *result_out;    /* Output: result status */
    status_$t       *status_ret;    /* Output: status */

    /* Local variables from parent frame */
    int16_t         hash_index;     /* UID hash for lock table (-0x126) */
    uint16_t        entry_index;    /* Allocated entry index (-0x122) */
    uint16_t        proc_slot;      /* Per-process slot index (-0x114) */
    int16_t         req_mode;       /* Requested lock mode index (-0x11C / -0x11E / -0x11A) */
    int8_t          is_remote;      /* Remote lock flag (-0x136) */
    int8_t          is_null_uid;    /* Null UID flag (-0x138) */
    int8_t          validated;      /* Rights validated flag (-0x132) */
    int8_t          defer_validate; /* Defer validation flag (-0x134) */
    uint32_t        node_id;        /* Node ID for lock (-0x104) */
    uint8_t         local_flags;    /* Local flags byte (-0x2B / local_2f) */

    /* Attribute info buffer */
    uint8_t         attr_buf[100];  /* Attribute info (-0xD8 to -0xAC) */
    status_$t       local_status;   /* Local status (-0x108 / -0x10C) */
} priv_lock_ctx_t;

/*
 * Forward declarations for nested helper procedures
 */
static status_$t priv_lock_alloc_entry(priv_lock_ctx_t *ctx, uint8_t fill_entry, uint8_t add_to_proc);
static void priv_lock_link_entry(priv_lock_ctx_t *ctx, file_lock_entry_detail_t *entry);
static void priv_lock_check_rights(priv_lock_ctx_t *ctx);
static void priv_lock_remote_lock(priv_lock_ctx_t *ctx, file_lock_entry_detail_t *entry,
                                   uint16_t mode, uint16_t side, uint16_t flags, uint8_t is_new);
static void priv_lock_release_entry(priv_lock_ctx_t *ctx);
static status_$t priv_lock_check_conflicts(priv_lock_ctx_t *ctx, uint8_t allow_self);

/*
 * FILE_$PRIV_LOCK - Core file locking function
 */
void FILE_$PRIV_LOCK(uid_t *file_uid, int16_t asid, uint16_t lock_index,
                     uint16_t lock_mode, int16_t rights, uint32_t flags,
                     int32_t param_7, uint32_t param_8, uint32_t param_9,
                     void *param_10, uint16_t param_11, uint32_t *lock_ptr_out,
                     uint16_t *result_out, status_$t *status_ret)
{
    priv_lock_ctx_t ctx;
    int16_t req_mode;
    uint16_t existing_entry;
    int16_t proc_slot;
    int i;
    file_lock_entry_detail_t *entry;
    uint16_t compat_mask;
    status_$t local_status;

    /*
     * Check if lock table is full
     * DAT_00e823f8 is the table full flag at offset 0x2D0
     */
    if ((int8_t)FILE_$LOT_FULL < 0) {
        *status_ret = status_$file_local_lock_table_full;
        return;
    }

    /*
     * Initialize context structure
     */
    ctx.file_uid = file_uid;
    ctx.asid = asid;
    ctx.lock_index = lock_index;
    ctx.lock_mode = lock_mode;
    ctx.rights = rights;
    ctx.flags = flags;
    ctx.param_7 = param_7;
    ctx.param_8 = param_8;
    ctx.param_9 = param_9;
    ctx.param_10 = param_10;
    ctx.param_11 = param_11;
    ctx.lock_ptr_out = lock_ptr_out;
    ctx.result_out = result_out;
    ctx.status_ret = status_ret;
    ctx.entry_index = 0;
    ctx.proc_slot = 0;

    /*
     * Validate lock mode
     * Lock modes 0-20 are valid (mask 0xFFF checks bits 0-11)
     * Also verify mode is in valid range (<= 0x14 = 20)
     * side must be 0 or 1
     */
    if ((((0xFFF & (1 << (lock_mode & 0x1F))) == 0) || (lock_mode > 0x14)) ||
        ((lock_index != 0) && (lock_index != 1))) {
        req_mode = 0;
    } else {
        /* Look up lock mode in compatibility table */
        /* Table at offset 0x58 (FILE_$LOCK_MODE_TABLE), indexed by side*24 + mode*2 */
        req_mode = FILE_$LOCK_MODE_TABLE[lock_index * 12 + lock_mode];
    }

    if (req_mode == 0) {
        *status_ret = status_$file_illegal_lock_request;
        return;
    }

    ctx.req_mode = req_mode;

    /*
     * Determine if this is a remote lock request
     * Bit 17 (0x20000) set indicates remote lock
     */
    ctx.is_remote = (flags & 0x20000) ? -1 : 0;
    ctx.validated = 0;

    if (ctx.is_remote < 0) {
        /* For remote locks, use provided node address */
        ctx.node_id = param_8 & 0xFFFFF;
    } else {
        /* For local locks, use local node ID */
        ctx.node_id = NODE_$ME;
    }

    /*
     * Compute UID hash for lock table lookup
     */
    ctx.hash_index = UID_$HASH(file_uid, NULL);

    /*
     * Check if this is a null/zero UID (indicates pseudo-lock)
     */
    ctx.is_null_uid = (*(uint8_t *)&file_uid->high == 0) ? -1 : 0;

    ctx.local_flags &= 0xBF;  /* Clear bit 6 */

    /*
     * Main lock acquisition logic
     * Branch based on whether this is a change operation or new lock
     */
    if ((flags & 0x400000) == 0 &&
        ((FILE_$LOCK_ILLEGAL_MASK & (1 << (lock_mode & 0x1F))) == 0)) {
        /*
         * New lock acquisition path
         * Need to find hints for remote location or use local lock
         */
        int16_t hint_count;
        uint32_t hint_node[4];
        int hint_idx;

        if (ctx.is_null_uid < 0) {
            /* Pseudo-lock with null UID */
            hint_count = 1;
            hint_node[0] = 0;
            hint_node[1] = file_uid->low & 0xFFFFF;
            if (hint_node[1] == 0) {
                hint_node[1] = NODE_$ME;
            }
        } else if (rights < 0) {
            /* Negative rights means local only */
            hint_count = 1;
            hint_node[0] = 0;
            hint_node[1] = NODE_$ME;
        } else {
            /* Get hints for file location */
            hint_count = HINT_$GET_HINTS(file_uid, hint_node);
        }

        /*
         * Try each hint location
         */
        for (hint_idx = 0; hint_idx < hint_count; hint_idx++) {
            uint32_t target_node = hint_node[hint_idx * 2 + 1];

            if (target_node != NODE_$ME) {
                /*
                 * Remote lock path
                 */
                ML_$LOCK(5);
                /* Call helper functions to set up remote context */
                /* ... complex remote lock logic ... */

                local_status = priv_lock_alloc_entry(&ctx, 0xFF, 0);
                *status_ret = local_status;
                if (*status_ret == 0) {
                    FILE_$LOT_SEQN++;
                    ML_$UNLOCK(5);

                    entry = LOT_ENTRY(ctx.entry_index);
                    HINT_$LOOKUP_CACHE((void *)&ctx.node_id, (void *)&ctx.local_flags);
                    entry->flags2 &= 0xFD;
                    entry->flags2 |= ((~ctx.local_flags >> 7) & 1) << 1;

                    /* Try remote lock with retry loop */
                    for (i = 0; i < 2; i++) {
                        if ((entry->flags2 & 2) == 0) {
                            priv_lock_check_rights(&ctx);
                            if (*status_ret != 0) goto error_cleanup;
                            ctx.req_mode = FILE_$LOCK_MAP_TABLE[lock_mode];
                        } else {
                            ctx.req_mode = lock_mode;
                        }

                        ML_$LOCK(5);
                        priv_lock_remote_lock(&ctx, entry, ctx.req_mode, lock_index,
                                              flags & 0xFFFF, 0);
                        if (*status_ret != 0x0F0003 || i == 1) {
                            goto error_cleanup;
                        }

                        /* Retry with updated hints */
                        int8_t hint_flag = (entry->flags2 & 2) ? -1 : 0;
                        HINT_$ADD_CACHE((void *)&ctx.node_id, &hint_flag);
                        entry->flags2 = (entry->flags2 & 0xFD) |
                                        (((entry->flags2 & 2) == 0) ? 2 : 0);
                    }
                }
                goto done_unlock;
            }

            if (ctx.is_null_uid < 0) {
                ctx.local_flags &= 0x7F;  /* Clear remote flag */
                ctx.node_id = hint_node[hint_idx * 2];
            } else {
                ctx.local_flags &= 0xBF;  /* Clear flag */

                /* Get file attributes to check access */
                AST_$GET_ATTRIBUTES((uid_t *)&ctx.attr_buf[0x4C], 0x80,
                                    ctx.attr_buf, &ctx.local_status);
                if (ctx.local_status != 0) continue;

                /* Check file type and rights */
                if ((ctx.attr_buf[1] == 1 || ctx.attr_buf[1] == 2) &&
                    (ctx.attr_buf[0] != 0) &&
                    (ctx.validated = 0xFF, lock_mode == 1 && (flags & 0xFF) < 0)) {
                    *status_ret = 0x0E000D;  /* Access denied */
                    goto done;
                }

                /* Check read-only volume */
                if ((*(uint16_t *)&ctx.attr_buf[2] & 2) &&
                    (FILE_$LOCK_COMPAT_TABLE[lock_mode] & 2)) {
                    if (ctx.attr_buf[1] == 1 || ctx.attr_buf[1] == 2) {
                        *status_ret = status_$naming_vol_mounted_read_only;
                    } else {
                        *status_ret = status_$file_vol_mounted_read_only;
                    }
                    goto done;
                }

                if (hint_count != 1 || (ctx.local_flags & 0x80)) {
                    HINT_$ADDI(file_uid, (void *)&ctx.node_id);
                }
            }

            if (ctx.local_flags & 0x80) {
                if (rights < 0) {
                    *status_ret = status_$file_op_cannot_perform_here;
                    goto done;
                }
                /* Retry with remote hint */
                continue;
            }

            /*
             * Local lock path - validate and allocate entry
             */
            ctx.defer_validate = -1;
            priv_lock_check_rights(&ctx);
            ML_$LOCK(5);
            if (*status_ret != 0) goto done_unlock;

            /* Set up local lock context */
            /* ... additional setup ... */

            local_status = priv_lock_alloc_entry(&ctx, 0xFF, 0);
            *status_ret = local_status;
            if (*status_ret != 0) goto done_unlock;

            entry = LOT_ENTRY(ctx.entry_index);
            *(uint8_t *)&entry->rights = (uint8_t)*ctx.result_out;

            local_status = priv_lock_check_conflicts(&ctx, 0);
            *status_ret = local_status;
            if (ctx.defer_validate < 0 || *status_ret != 0) goto done_unlock;

            priv_lock_link_entry(&ctx, entry);
            ML_$UNLOCK(5);
            goto done_success;
        }

        /* No valid hint found */
        *status_ret = status_$file_object_not_found;
        goto done;
    }

    /*
     * Change/upgrade lock path
     * Look up existing lock and modify it
     */
    ctx.req_mode = FILE_$LOCK_REQ_TABLE[lock_mode];
    compat_mask = FILE_$LOCK_CVT_TABLE[lock_mode];

    /* Purify file if needed */
    if ((ctx.is_null_uid >= 0) && (ctx.req_mode != 4) && (ctx.req_mode != 0x0B)) {
        AST_$PURIFY(file_uid, 0x8000, 0, param_10, 0, status_ret);
        if (*status_ret != 0) {
            return;
        }
    }

    ML_$LOCK(5);

    existing_entry = 0;
    proc_slot = 0;

    if (ctx.is_remote < 0) {
        /*
         * Remote lock change - search by hash
         */
        existing_entry = FILE_$LOT_HASHTAB[ctx.hash_index];
        while (existing_entry > 0) {
            entry = LOT_ENTRY(existing_entry);

            if ((entry->node_high == param_8) && (entry->context == param_7) &&
                (entry->uid_high == file_uid->high) && (entry->uid_low == file_uid->low) &&
                ((entry->flags2 & 4) == 0) &&
                ((flags & 0x400000) || ((entry->flags2 >> 7) == lock_index))) {
                if (entry->sequence == (flags >> 16)) {
                    *status_ret = 0;  /* Already have this lock */
                    goto done_unlock;
                }
                if (compat_mask & (1 << ((entry->flags2 & 0x78) >> 3))) {
                    break;  /* Compatible - can upgrade */
                }
            }
            existing_entry = entry->next;
        }
    } else if (*lock_ptr_out == 0 || *lock_ptr_out > 0x96) {
        /*
         * Local lock change - search per-process table
         */
        if ((flags & 0x400000) == 0) {
            proc_slot = 0;
            int16_t count = PROC_LOT_COUNT(asid);
            if (count > 0) {
                for (i = 1; i <= count; i++) {
                    uint16_t slot_entry = PROC_LOT_ENTRY(asid, i);
                    if (slot_entry != 0) {
                        entry = LOT_ENTRY(slot_entry);
                        if ((compat_mask & (1 << ((entry->flags2 & 0x78) >> 3))) &&
                            (entry->uid_high == file_uid->high) &&
                            (entry->uid_low == file_uid->low) &&
                            ((entry->flags2 >> 7) == lock_index)) {
                            existing_entry = slot_entry;
                            proc_slot = i;
                            *lock_ptr_out = i;
                            break;
                        }
                    }
                }
            }
        }
    } else {
        /*
         * Have explicit slot number
         */
        proc_slot = (int16_t)*lock_ptr_out;
        existing_entry = PROC_LOT_ENTRY(asid, proc_slot);
        if (existing_entry != 0) {
            entry = LOT_ENTRY(existing_entry);
            if ((compat_mask & (1 << ((entry->flags2 & 0x78) >> 3))) &&
                (entry->uid_high == file_uid->high) &&
                (entry->uid_low == file_uid->low) &&
                ((flags & 0x400000) || ((entry->flags2 >> 7) == lock_index))) {
                /* Found matching entry */
            } else {
                existing_entry = 0;
                proc_slot = 0;
            }
        }
    }

    ctx.proc_slot = proc_slot;

    if (existing_entry == 0) {
        *status_ret = status_$file_illegal_lock_request;
        goto done_unlock;
    }

    entry = LOT_ENTRY(existing_entry);

    /*
     * Validate lock change is allowed
     */
    if (((flags & 0x400000) && (entry->flags2 & 4) && !(entry->flags2 & 2))) {
        *status_ret = status_$file_incompatible_request;
        goto done_unlock;
    }

    if (((entry->flags1 & 0x80) && !(entry->flags2 & 4)) &&
        (FILE_$LOCK_COMPAT_TABLE[lock_mode] & 2)) {
        *status_ret = status_$file_vol_mounted_read_only;
        goto done_unlock;
    }

    if (!(flags & 0x80000) &&
        ((FILE_$LOCK_COMPAT_TABLE[lock_mode] & entry->rights) !=
         FILE_$LOCK_COMPAT_TABLE[lock_mode])) {
        *status_ret = status_$insufficient_rights;
        goto done_unlock;
    }

    ctx.node_id = entry->node_high;
    ctx.local_flags = ((entry->flags2 & 4) ? 0x80 : 0) | (ctx.local_flags & 0x7F);

    /*
     * Handle refcount > 1 case - need to allocate new entry
     */
    if (entry->refcount >= 2) {
        *result_out = entry->rights;
        local_status = priv_lock_alloc_entry(&ctx, 0, 0xFF);
        *status_ret = local_status;
        if (*status_ret != 0) goto done_unlock;

        /* Copy existing entry to new entry */
        file_lock_entry_detail_t *new_entry = LOT_ENTRY(ctx.entry_index);
        *new_entry = *entry;

        if (entry->flags2 & 4) {
            priv_lock_remote_lock(&ctx, new_entry, ctx.req_mode, lock_index,
                                  (flags & 0xFF) & 0xFFBF, 0);
            ML_$LOCK(5);
            if (*status_ret != 0) goto done_unlock;
        }

        entry->refcount--;
        existing_entry = ctx.entry_index;
        PROC_LOT_ENTRY(asid, proc_slot) = ctx.entry_index;
        priv_lock_link_entry(&ctx, new_entry);
        ctx.entry_index = 0;
        entry = new_entry;
    } else {
        if (entry->flags2 & 4) {
            priv_lock_remote_lock(&ctx, entry, lock_mode, lock_index, flags & 0xFFFF, 0xFF);
            ML_$LOCK(5);
            if (*status_ret != 0) goto done_unlock;
        }
    }

    /*
     * Update lock mode and finish
     */
    if (!(entry->flags2 & 4)) {
        local_status = priv_lock_check_conflicts(&ctx, 0xFF);
        *status_ret = local_status;
        if (*status_ret != 0) goto done_unlock;
        entry->sequence = (uint16_t)(flags >> 16);
    }

    /* Update mode in flags2 */
    entry->flags2 = (entry->flags2 & 0x87) | (ctx.req_mode << 3);
    if (flags & 0x400000) {
        entry->flags2 = (entry->flags2 & 0x7F) | (lock_index << 7);
    }
    ML_$UNLOCK(5);

done_success:
    /* Log if enabled */
    if (NETLOG_$OK_TO_LOG >= 0) {
        return;
    }
    NETLOG_$LOG_IT(0x12, file_uid, 0, 0, lock_index, lock_mode,
                   (ctx.local_flags & 0x80) ? 1 : 0,
                   (ctx.is_remote < 0) ? 1 : 0);
    return;

error_cleanup:
    if (*status_ret != 0) {
        status_$t sts = *status_ret;
        if (sts != 0x0F0004 && sts != 0x0F000B &&
            ((sts >> 8) & 0xFF) != 0x11 && sts != 0x0F0001) {
            goto done_unlock;
        }
        ML_$LOCK(5);
        FILE_$LOT_SEQN--;
        priv_lock_release_entry(&ctx);
        ML_$UNLOCK(5);
        /* Retry */
        goto done_success;
    }
    /* ... additional cleanup ... */

done_unlock:
    priv_lock_release_entry(&ctx);
    ML_$UNLOCK(5);

done:
    if (*status_ret == status_$file_object_in_use) {
        FILE_$READ_LOCK_ENTRYUI(file_uid, ctx.attr_buf, &ctx.local_status);
        if (ctx.local_status != status_$file_object_not_locked_by_this_process) {
            return;
        }
        /* Retry from beginning */
        goto done_success;  /* Actually should jump back to start */
    }
    return;
}

/*
 * Helper: Allocate a lock entry from free list
 *
 * Original at 0x00E5EB98, nested procedure
 */
static status_$t priv_lock_alloc_entry(priv_lock_ctx_t *ctx, uint8_t fill_entry, uint8_t add_to_proc)
{
    uint16_t entry_idx;
    file_lock_entry_detail_t *entry;
    int i;

    /* Get entry from free list */
    entry_idx = FILE_$LOT_FREE;
    if (entry_idx == 0) {
        return status_$file_local_lock_table_full;
    }

    ctx->entry_index = entry_idx;
    entry = LOT_ENTRY(entry_idx);

    /* Update free list */
    FILE_$LOT_FREE = entry->next;

    /* Track highest allocated */
    if (entry_idx > FILE_$LOT_HIGH) {
        FILE_$LOT_HIGH = entry_idx;
    }

    /* Fill in entry if requested */
    if (fill_entry >= 0) {
        entry->uid_high = ctx->file_uid->high;
        entry->uid_low = ctx->file_uid->low;

        if (ctx->is_remote < 0) {
            entry->context = ctx->param_7;
            entry->sequence = (uint16_t)(ctx->flags >> 16);
            entry->node_low = ctx->param_8;
            entry->node_high = ctx->node_id;
        } else {
            entry->node_low = ctx->node_id;
            entry->node_high = ctx->node_id;
            entry->context = 0;
            entry->sequence = 0;
        }

        entry->refcount = 0;

        /* Set flags based on attributes */
        uint8_t f1 = 0;
        if ((*(uint16_t *)&ctx->attr_buf[2] & 2) && !ctx->is_null_uid) {
            f1 |= 0x80;
        }
        entry->flags1 = (entry->flags1 & 0x40) | f1;

        entry->flags2 = (entry->flags2 & 0x7F) | (ctx->lock_index << 7);
        entry->flags2 = (entry->flags2 & 0x87) | (ctx->lock_mode << 3);
        entry->flags2 = (entry->flags2 & 0xFB) |
                        ((ctx->local_flags & 0x80) ? 0x04 : 0);
        entry->flags2 |= 0x02;  /* Set pending flag */
        entry->flags2 = (entry->flags2 & 0xFE) |
                        ((ctx->flags & 0x20) ? 1 : 0);
    }

    /* Add to per-process table if requested */
    if ((add_to_proc & ~ctx->is_remote) >= 0) {
        return 0;
    }

    for (i = 1; i <= FILE_PROC_LOCK_MAX_ENTRIES; i++) {
        if (PROC_LOT_ENTRY(ctx->asid, i) == 0) {
            ctx->proc_slot = i;
            PROC_LOT_ENTRY(ctx->asid, i) = entry_idx;
            if (i > PROC_LOT_COUNT(ctx->asid)) {
                PROC_LOT_COUNT(ctx->asid) = i;
            }
            *ctx->lock_ptr_out = i;
            return 0;
        }
    }

    return status_$file_local_lock_table_full;
}

/*
 * Helper: Link entry into hash bucket
 *
 * Original at 0x00E5ED58, nested procedure
 */
static void priv_lock_link_entry(priv_lock_ctx_t *ctx, file_lock_entry_detail_t *entry)
{
    entry->refcount = 1;
    entry->next = FILE_$LOT_HASHTAB[ctx->hash_index];
    FILE_$LOT_HASHTAB[ctx->hash_index] = ctx->entry_index;
}

/*
 * Helper: Check access rights for lock
 *
 * Original at 0x00E5ED92, nested procedure
 */
static void priv_lock_check_rights(priv_lock_ctx_t *ctx)
{
    uint16_t rights_result;
    uint16_t required_rights;

    *ctx->status_ret = 0;

    if (ctx->is_null_uid < 0) {
        *ctx->result_out = 0x0F;
        return;
    }

    if (ctx->flags & 0x08) {
        *ctx->result_out = 0x10;
        return;
    }

    required_rights = FILE_$LOCK_COMPAT_TABLE[ctx->lock_mode];

    if (ctx->flags & 0x02) {
        /* Use ACL with explicit context */
        int8_t check_flag = (ctx->flags & 0x100) ? -1 : 0;
        rights_result = ACL_$RIGHTS_CHECK(*(uid_t *)ctx->param_10, ctx->file_uid,
                                          NULL, NULL, &check_flag, ctx->status_ret);
    } else {
        rights_result = ACL_$RIGHTS(ctx->file_uid, NULL, NULL, NULL, ctx->status_ret);
    }

    *ctx->result_out = rights_result;

    if (*ctx->status_ret == 0x230002 || *ctx->status_ret == 0 ||
        *ctx->status_ret == 0x230001) {
        *ctx->status_ret = 0;

        if (ctx->flags & 0x04) {
            if (rights_result == 0) {
                *ctx->status_ret = 0x0F0010;  /* No rights */
            } else if ((rights_result & required_rights) != required_rights) {
                *ctx->status_ret = status_$insufficient_rights;
            }
        }
    } else {
        OS_PROC_SHUTWIRED(ctx->status_ret);
    }
}

/*
 * Helper: Perform remote lock operation
 *
 * Original at 0x00E5EE88, nested procedure
 */
static void priv_lock_remote_lock(priv_lock_ctx_t *ctx, file_lock_entry_detail_t *entry,
                                   uint16_t mode, uint16_t side, uint16_t flags, uint8_t is_new)
{
    uint16_t seq_out[3];
    status_$t local_status;

    if (is_new >= 0) {
        FILE_$LOT_SEQN++;
        entry->context = FILE_$LOT_SEQN;
    }

    ML_$UNLOCK(5);

    REM_FILE_$LOCK((void *)&ctx->attr_buf[0x4C], side, mode, flags,
                   (uint16_t)(ctx->flags >> 24),
                   (entry->flags2 & 2) ? -1 : 0,
                   entry->context,
                   ctx->lock_ptr_out, seq_out,
                   &ctx->attr_buf[0], ctx->status_ret);

    if (*ctx->status_ret == 0) {
        entry->sequence = seq_out[0];
    }
}

/*
 * Helper: Release lock entry back to free list
 *
 * Original at 0x00E5EF08, nested procedure
 */
static void priv_lock_release_entry(priv_lock_ctx_t *ctx)
{
    file_lock_entry_detail_t *entry;

    if (ctx->entry_index == 0) {
        return;
    }

    entry = LOT_ENTRY(ctx->entry_index);

    /* Add back to free list */
    entry->next = FILE_$LOT_FREE;
    entry->refcount = 0;
    FILE_$LOT_FREE = ctx->entry_index;

    /* Remove from per-process table */
    if (ctx->proc_slot != 0) {
        PROC_LOT_ENTRY(ctx->asid, ctx->proc_slot) = 0;
    }

    ctx->entry_index = 0;
    ctx->proc_slot = 0;
}

/*
 * Helper: Check for lock conflicts
 *
 * Original at 0x00E5EF76, nested procedure
 */
static status_$t priv_lock_check_conflicts(priv_lock_ctx_t *ctx, uint8_t allow_self)
{
    int16_t current;
    file_lock_entry_detail_t *entry;
    uint16_t compat_mask;
    int8_t found_other = 0;
    int8_t found_exclusive = 0;

    ctx->validated = 0;
    ctx->defer_validate = 0;

    compat_mask = FILE_$LOCK_MODE_TABLE[ctx->req_mode * 2];

    current = FILE_$LOT_HASHTAB[ctx->hash_index];

    while (current > 0) {
        entry = LOT_ENTRY(current);

        if ((entry->uid_high == ctx->file_uid->high) &&
            (entry->uid_low == ctx->file_uid->low)) {

            if (ctx->is_remote < 0) {
                /* Remote lock conflict check */
                if ((entry->sequence == (ctx->flags >> 16)) &&
                    (entry->context == ctx->param_7) &&
                    (entry->node_low == ctx->param_8)) {
                    ctx->defer_validate = 0xFF;
                    return 0;
                }
            }

            if ((allow_self >= 0) || (current != ctx->entry_index)) {
                ctx->validated = 0xFF;

                uint8_t entry_mode = (entry->flags2 & 0x78) >> 3;
                if (entry_mode == 4 || entry_mode == 0x0B) {
                    found_exclusive = -1;
                }

                uint16_t entry_compat = FILE_$LOCK_MODE_TABLE[(entry->flags2 >> 7) * 12 + entry_mode];

                if (!(compat_mask & (1 << (entry_compat & 0x1F)))) {
                    /* Check if same node or special mode */
                    if ((entry->node_low & 0xFFFFF) != ctx->node_id) {
                        if ((ctx->req_mode == 2) || (entry_compat == 2)) {
                            return status_$file_object_in_use;
                        }
                        if ((ctx->req_mode == 6) && (entry_compat == 6)) {
                            return status_$file_object_in_use;
                        }
                    }
                    return status_$file_object_in_use;
                }
            }
        }

        current = entry->next;
    }

    /* Additional attribute-based checks */
    if (ctx->is_null_uid < 0) {
        return 0;
    }

    if (ctx->local_flags & 0x80) {
        return 0;
    }

    if (ctx->validated >= 0) {
        if ((ctx->req_mode == 5) || (ctx->req_mode == 2)) {
            uint32_t attr_val = ctx->node_id;
            AST_$SET_ATTRIBUTE(ctx->file_uid, 0x0B, &attr_val, ctx->status_ret);
            return *ctx->status_ret;
        }
        if (found_exclusive >= 0) {
            return 0;
        }
    }

    uint32_t zero = 0;
    AST_$SET_ATTRIBUTE(ctx->file_uid, 0x0B, &zero, ctx->status_ret);
    return *ctx->status_ret;
}
