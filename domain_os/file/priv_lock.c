/*
 * FILE_$PRIV_LOCK - core file locking primitive
 *
 * Original address: 0x00E5F0EE, 2892 bytes.
 * Module base A5 = 0x00E82128 (`lea (0xe82128).l,A5` at 0x00E5F0F6), so an
 * A5 displacement D in the listing is the global at 0xE82128 + D.
 *
 * The Pascal source declared six nested procedures which reach up into this
 * frame through the static link (`movea.l (A6),A0`).  They are flattened here
 * into static helpers that take an explicit `priv_lock_frame_t *`:
 *
 *   0x00E5EB98  FILE_$PRIV_LOCK_$ALLOC_ENTRY
 *   0x00E5ED58  FILE_$PRIV_LOCK_$LINK_ENTRY
 *   0x00E5ED92  FILE_$PRIV_LOCK_$CHECK_RIGHTS
 *   0x00E5EE88  FILE_$PRIV_LOCK_$REMOTE_LOCK
 *   0x00E5EF08  FILE_$PRIV_LOCK_$FREE_ENTRY
 *   0x00E5EF76  FILE_$PRIV_LOCK_$CHECK_CONFLICTS
 *
 * Frame map (link.w A6,-0x164), reconstructed from every access in the body
 * and in the six helpers:
 *
 *   A6-0x162  add_cache_flag      byte handed to HINT_$ADD_CACHE
 *   A6-0x138  uid_is_null         boolean, uid.high's first byte == 0
 *   A6-0x136  from_remote         boolean, flags bit 1
 *   A6-0x134  own_remote_lock     set by CHECK_CONFLICTS
 *   A6-0x132  saw_other_lock      set by CHECK_CONFLICTS
 *   A6-0x12E  dir_not_empty       set at 0x00E5F784
 *   A6-0x12C  cache_flag          byte returned by HINT_$LOOKUP_CACHE
 *   A6-0x126  hash                UID_$HASH remainder
 *   A6-0x124  found_entry         lock table index of the entry we operate on
 *   A6-0x122  new_entry           lock table index allocated by ALLOC_ENTRY
 *   A6-0x11C  mapped_mode         FILE_$LOCK_MODE_TABLE[side][mode]
 *   A6-0x11A  req_mode            FILE_$LOCK_REQ_TABLE[mode]
 *   A6-0x118  remote_mode         mode handed to REM_FILE_$LOCK
 *   A6-0x114  new_slot            per-process slot claimed by ALLOC_ENTRY
 *   A6-0x112  found_slot          per-process slot of found_entry
 *   A6-0x110  log_remote          NETLOG argument
 *   A6-0x10E  log_from_remote     NETLOG argument
 *   A6-0x108  local_status
 *   A6-0x100  lock_info[40]       FILE_$READ_LOCK_ENTRYUI output
 *   A6-0x0D8  attrs[0x90]         AST_$GET_ATTRIBUTES output
 *   A6-0x048  desc                file_$obj_loc_t
 *   A6-0x028  hints[1]            file_$lock_hint_t, 1-based (base A6-0x30)
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
 * reference with `pea (d,PC)`.
 */

/* 0x00E5EA28: word 0x00FB = 251, the lock hash table modulus handed to
 * UID_$HASH at 0x00E5F18C. */
static uint16_t file_$lot_hash_modulus = 251;

/* 0x00E5E61E: longword 0, the "no segment list" argument AST_$PURIFY gets at
 * 0x00E5F21E. */
static uint32_t file_$purify_nil_list = 0;

/* 0x00E5EE84: longword 0xFFFFFFFF, the required-rights mask ACL_$RIGHTS and
 * ACL_$RIGHTS_CHECK get (0x00E5EDFC / 0x00E5EE18). */
static uint32_t file_$acl_rights_all = 0xFFFFFFFFu;

/* 0x00E5EE82: word 0, the option-flags word for the same two calls
 * (0x00E5EDF8 / 0x00E5EE14). */
static int16_t file_$acl_zero_word = 0;

/* 0x00E5D380: byte 0, ACL_$RIGHTS' `ignore_super` argument (0x00E5EE1C).
 * FALSE, so the super-user bypass applies. */
static const boolean file_$acl_ignore_super = false;

/* Attribute id 0x0B, the "lock holder node" attribute CHECK_CONFLICTS writes
 * through AST_$SET_ATTRIBUTE at 0x00E5F0CA. */
#define FILE_ATTR_LOCK_NODE     0x0B

/*
 * The Pascal frame FILE_$PRIV_LOCK's nested procedures read through the
 * static link.  Parameters keep their A6 displacement in the comment.
 */
typedef struct priv_lock_frame_t {
    /* ---- parameters ---- */
    uid_t      *file_uid;           /* A6+0x08 */
    int16_t     asid;               /* A6+0x0C */
    uint16_t    side;               /* A6+0x0E */
    uint16_t    lock_mode;          /* A6+0x10 */
    boolean     local_only;         /* A6+0x12 (byte, high half of the word) */
    uint16_t    flags;              /* A6+0x14 */
    uint16_t    key;                /* A6+0x16 */
    uint32_t    rem_key;            /* A6+0x18 */
    uint32_t    rem_node;           /* A6+0x1C */
    uint32_t    rem_extra;          /* A6+0x20 */
    void      **acl_ctx;            /* A6+0x24 */
    uint16_t    rem_wait;           /* A6+0x28 */
    uint32_t   *slot_io;            /* A6+0x2A */
    uint16_t   *rights_out;         /* A6+0x2E */
    status_$t  *status_ret;         /* A6+0x32 */

    /* ---- locals reached by the nested procedures ---- */
    int8_t      add_cache_flag;     /* A6-0x162 */
    boolean     uid_is_null;        /* A6-0x138 */
    boolean     from_remote;        /* A6-0x136 */
    boolean     own_remote_lock;    /* A6-0x134 */
    boolean     saw_other_lock;     /* A6-0x132 */
    boolean     dir_not_empty;      /* A6-0x12E */
    uint8_t     cache_flag;         /* A6-0x12C */
    int16_t     hash;               /* A6-0x126 */
    int16_t     found_entry;        /* A6-0x124 */
    int16_t     new_entry;          /* A6-0x122 */
    int16_t     mapped_mode;        /* A6-0x11C */
    int16_t     req_mode;           /* A6-0x11A */
    int16_t     remote_mode;        /* A6-0x118 */
    int16_t     new_slot;           /* A6-0x114 */
    int16_t     found_slot;         /* A6-0x112 */
    uint16_t    log_remote;         /* A6-0x110 */
    uint16_t    log_from_remote;    /* A6-0x10E */
    uint32_t    node_id;            /* A6-0x104 */
    status_$t   local_status;       /* A6-0x108 */
    uint8_t     lock_info[40];      /* A6-0x100 */
    uint8_t     attrs[FILE_ATTR_FULL_SIZE];     /* A6-0x0D8, 0x90 bytes */
    file_$obj_loc_t     desc;                   /* A6-0x48 */
    file_$lock_hint_t   hints[FILE_LOCK_MAX_HINTS + 1];  /* A6-0x30, 1-based */
} priv_lock_frame_t;

static status_$t priv_lock_alloc_entry(priv_lock_frame_t *f,
                                       boolean add_to_proc, boolean skip_fill);
static void      priv_lock_link_entry(priv_lock_frame_t *f,
                                      file_lock_entry_detail_t *entry);
static void      priv_lock_check_rights(priv_lock_frame_t *f,
                                        status_$t *status, uint16_t *rights_out);
static void      priv_lock_remote_lock(priv_lock_frame_t *f,
                                       file_lock_entry_detail_t *entry,
                                       uint16_t mode, uint16_t side,
                                       uint16_t flags, boolean is_new);
static void      priv_lock_free_entry(priv_lock_frame_t *f);
static status_$t priv_lock_check_conflicts(priv_lock_frame_t *f, boolean skip_found);

/*
 * ============================================================================
 * FILE_$PRIV_LOCK (0x00E5F0EE)
 * ============================================================================
 */
void FILE_$PRIV_LOCK(uid_t *file_uid, int16_t asid, uint16_t side,
                     uint16_t lock_mode, boolean local_only,
                     uint16_t flags, uint16_t key,
                     uint32_t rem_key, uint32_t rem_node, uint32_t rem_extra,
                     void **acl_ctx, uint16_t rem_wait,
                     uint32_t *slot_io, uint16_t *rights_out,
                     status_$t *status_ret)
{
    priv_lock_frame_t f;
    file_lock_entry_detail_t *entry;
    file_lock_entry_detail_t *new_entry;
    uint16_t compat_mask;            /* D4 */
    int16_t  hint_count;             /* D5 */
    int16_t  hint_idx;               /* D4 in the hint loop */
    boolean  claimed;                /* D6 */
    int16_t  attempt;                /* D2 in the remote retry loop */
    int16_t  i;

    /* 0x00E5F0FC: the table-full flag is a Domain boolean. */
    if (FILE_$LOT_FULL < 0) {
        *status_ret = file_$local_lock_table_full;
        return;
    }

    f.file_uid   = file_uid;
    f.asid       = asid;
    f.side       = side;
    f.lock_mode  = lock_mode;
    f.local_only = local_only;
    f.flags      = flags;
    f.key        = key;
    f.rem_key    = rem_key;
    f.rem_node   = rem_node;
    f.rem_extra  = rem_extra;
    f.acl_ctx    = acl_ctx;
    f.rem_wait   = rem_wait;
    f.slot_io    = slot_io;
    f.rights_out = rights_out;
    f.status_ret = status_ret;

    /*
     * 0x00E5F110-0x00E5F148: map (side, mode) through FILE_$LOCK_MODE_TABLE.
     * `move.w #0xfff,D1w` only sets D1's low half, so the original's
     * `btst.l D0,D1` consults an undefined bit for modes 16..20; the
     * `cmpi.w #0x14 / bhi` that follows is redundant but preserved.  Only
     * modes 0..11 can survive both tests.
     */
    if (((0x0FFF & (1u << (lock_mode & 0x1F))) == 0) || (lock_mode > 0x14) ||
        ((side != 0) && (side != 1))) {
        f.mapped_mode = 0;
    } else {
        f.mapped_mode = (int16_t)FILE_$LOCK_MODE_TABLE[side * 12 + lock_mode];
    }

    if (f.mapped_mode == 0) {
        *status_ret = file_$illegal_lock_request;      /* 0x00E5F154 */
        return;
    }

    /* 0x00E5F15E: btst.b #1,(0x15,A6) - flags bit 1. */
    f.from_remote   = (flags & FILE_LOCK_FLAG_REMOTE) ? -1 : 0;
    f.dir_not_empty = 0;                                /* 0x00E5F16A */

    if (f.from_remote < 0) {
        f.node_id = rem_node & 0xFFFFF;                 /* 0x00E5F174 */
    } else {
        f.node_id = NODE_$ME;                           /* 0x00E5F184 */
    }

    /* 0x00E5F18C: only the remainder (D0's low word) is kept. */
    f.hash = (int16_t)(UID_$HASH(file_uid, &file_$lot_hash_modulus) & 0xFFFF);

retry_from_top:                                         /* 0x00E5F1A0 */
    f.new_entry = 0;
    f.new_slot  = 0;
    claimed     = 0;                                    /* clr.b D6b */

    f.desc.uid = *file_uid;                             /* 0x00E5F1AE */
    f.desc.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;      /* 0x00E5F1B6 bclr #6 */

    /* 0x00E5F1BC: tst.b of the first byte of uid.high (big-endian). */
    f.uid_is_null = (((file_uid->high >> 24) & 0xFF) == 0) ? -1 : 0;

    /* 0x00E5F1CC: the new-lock path needs both the CHANGE flag clear and the
     * mode's bit clear in FILE_$LOCK_ILLEGAL_MASK (again a word loaded into
     * the low half of D1 before a `btst.l`). */
    if (((flags & FILE_LOCK_FLAG_CHANGE) == 0) &&
        ((FILE_$LOCK_ILLEGAL_MASK & (1u << (lock_mode & 0x1F))) == 0)) {
        /*
         * --------------------------------------------------------------
         * New-lock path (0x00E5F6B8)
         * --------------------------------------------------------------
         */
        if (f.uid_is_null < 0) {
            /* 0x00E5F6BC: pseudo-lock, the node is encoded in the UID. */
            hint_count = 1;
            f.hints[1].loc_info = 0;
            f.hints[1].node = file_uid->low & 0xFFFFF;
            if ((file_uid->low & 0xFFFFF) == 0) {
                f.hints[1].node = NODE_$ME;
            }
        } else if (local_only < 0) {
            /* 0x00E5F6F4 */
            hint_count = 1;
            f.hints[1].loc_info = 0;
            f.hints[1].node = NODE_$ME;
        } else {
            /* 0x00E5F704: HINT_$GET_HINTS fills hints[1..n]. */
            hint_count = HINT_$GET_HINTS(file_uid, (uint32_t *)&f.hints[1]);
        }

        hint_idx = 0;                                   /* 0x00E5F71C clr.w D4w */

        /* 0x00E5FB88: while (hint_idx < hint_count) */
        while (hint_idx < hint_count) {
            hint_idx++;                                 /* 0x00E5F724 */

            if (f.hints[hint_idx].node != NODE_$ME) {
                /*
                 * ---- remote hint (0x00E5F900) ----
                 */
                ML_$LOCK(FILE_LOT_ML_LOCK);
                FILE_$UID_LOCK_ACQUIRE(file_uid);       /* 0x00E5F912 */
                FILE_$UID_LOCK_RELEASE(file_uid);       /* 0x00E5F91C */

                /* 0x00E5F922 */
                f.desc.loc_info = f.hints[hint_idx].loc_info;
                f.desc.node     = f.hints[hint_idx].node;
                f.desc.flags   &= (int8_t)~FILE_OBJ_LOC_SCRATCH;
                f.desc.flags   |= (int8_t)FILE_OBJ_LOC_REMOTE;

                /* 0x00E5F93A: add_to_proc = TRUE, skip_fill = FALSE */
                *status_ret = priv_lock_alloc_entry(&f, -1, 0);
                if (*status_ret != 0) {
                    goto free_and_unlock;               /* 0x00E5FBF2 */
                }

                FILE_$LOT_PENDING++;                    /* 0x00E5F954 */
                ML_$UNLOCK(FILE_LOT_ML_LOCK);

                entry = FILE_$LOT_ENTRY(f.new_entry);   /* 0x00E5F966 */

                /* 0x00E5F980 */
                HINT_$LOOKUP_CACHE(&f.desc.node, &f.cache_flag);
                entry->flags2 &= (uint8_t)~FILE_LOCK_F2_PENDING;
                entry->flags2 |= (uint8_t)(((uint8_t)(~f.cache_flag) >> 7) << 1);

                /* 0x00E5F9A6: moveq #1,D3 + dbf => two attempts. */
                attempt = 1;
                for (i = 1; i >= 0; i--) {
                    if ((entry->flags2 & FILE_LOCK_F2_PENDING) == 0) {
                        /* 0x00E5F9B2 */
                        priv_lock_check_rights(&f, status_ret, rights_out);
                        if (*status_ret != 0) {
                            goto remote_done;           /* 0x00E5FA50 */
                        }
                        f.remote_mode =
                            (int16_t)FILE_$LOCK_MAP_TABLE[lock_mode];
                    } else {
                        f.remote_mode = (int16_t)lock_mode;  /* 0x00E5F9D8 */
                    }

                    ML_$LOCK(FILE_LOT_ML_LOCK);         /* 0x00E5F9DE */
                    priv_lock_remote_lock(&f, entry, (uint16_t)f.remote_mode,
                                          side, flags, 0);

                    /* 0x00E5FA06 */
                    if (*status_ret != file_$bad_reply_received_from_remote_node) {
                        goto remote_done;
                    }
                    if (attempt == 2) {
                        goto remote_done;
                    }

                    /* 0x00E5FA18: tell the hint cache which flavour worked. */
                    f.add_cache_flag =
                        (entry->flags2 & FILE_LOCK_F2_PENDING) ? -1 : 0;
                    HINT_$ADD_CACHE(&f.desc.node, (uint8_t *)&f.add_cache_flag);

                    /* 0x00E5FA34: flip the PENDING bit for the retry. */
                    {
                        uint8_t inverted =
                            (uint8_t)(((entry->flags2 & FILE_LOCK_F2_PENDING)
                                       ? 0x00u : 0xFFu) >> 7);
                        entry->flags2 &= (uint8_t)~FILE_LOCK_F2_PENDING;
                        entry->flags2 |= (uint8_t)(inverted << 1);
                    }
                    attempt++;
                }

remote_done:                                            /* 0x00E5FA50 */
                if (*status_ret != 0) {
                    status_$t sts = *status_ret;

                    if ((sts == file_$comms_problem_with_remote_node) ||
                        (sts == file_$cannot_create_on_remote_with_uid) ||
                        (((sts >> 16) & 0xFF) == 0x11) ||
                        (sts == file_$object_not_found)) {
                        /* 0x00E5FA7E: drop the entry and try the next hint. */
                        ML_$LOCK(FILE_LOT_ML_LOCK);
                        FILE_$LOT_PENDING--;
                        priv_lock_free_entry(&f);
                        ML_$UNLOCK(FILE_LOT_ML_LOCK);
                        continue;                       /* -> 0x00E5FB88 */
                    }

                    /* 0x00E5FAA6 */
                    ML_$LOCK(FILE_LOT_ML_LOCK);
                    FILE_$LOT_PENDING--;
                    goto free_and_unlock;               /* 0x00E5FBF2 */
                }

                /*
                 * ---- remote lock granted (0x00E5FABC) ----
                 */
                if (hint_idx != 1) {
                    HINT_$ADDI(file_uid, &f.desc.loc_info);   /* 0x00E5FAC2 */
                }

                claimed = -1;                           /* 0x00E5FAD4 st D6b */
                ML_$LOCK(FILE_LOT_ML_LOCK);
                FILE_$LOT_PENDING--;                    /* 0x00E5FAE2 */

                /* 0x00E5FAE6: the result is deliberately discarded here. */
                (void)priv_lock_check_conflicts(&f, 0);

                entry = FILE_$LOT_ENTRY(f.new_entry);    /* 0x00E5FAF0 */
                entry->rights = (uint8_t)(*rights_out & 0xFF);  /* 0x00E5FB0E */
                priv_lock_link_entry(&f, entry);         /* 0x00E5FB14 */
                ML_$UNLOCK(FILE_LOT_ML_LOCK);

                if (f.saw_other_lock < 0) {              /* 0x00E5FB2C */
                    goto check_claimed;
                }
                if ((flags & FILE_LOCK_FLAG_LOCAL_ONLY) == 0) {
                    /* 0x00E5FB3A: attrs+0x2C is the DTM the flush compares. */
                    AST_$COND_FLUSH(file_uid,
                                    (uint32_t *)&f.attrs[0x2C],
                                    &f.local_status);
                }
                if (f.uid_is_null < 0) {                 /* 0x00E5FB50 */
                    goto check_claimed;
                }
                /* 0x00E5FB56: word at entry+0x1A; bit 1 of the word is bit 1
                 * of the low byte, i.e. flags2's PENDING bit. */
                if ((entry->flags2 & FILE_LOCK_F2_PENDING) == 0) {
                    goto check_claimed;
                }
                AST_$LOAD_AOTE((uint32_t *)f.attrs, (uint32_t *)&f.desc);
                goto check_claimed;                      /* 0x00E5FB86 */
            }

            /*
             * ---- local hint (0x00E5F736) ----
             */
            if (f.uid_is_null < 0) {
                /* 0x00E5F80A */
                f.desc.flags &= (int8_t)~FILE_OBJ_LOC_REMOTE;
                f.desc.loc_info = f.hints[1].loc_info;
                f.desc.node     = f.hints[1].node;
            } else {
                f.desc.flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;   /* 0x00E5F73E */

                /* 0x00E5F744: AST_$GET_ATTRIBUTES takes the descriptor, whose
                 * UID sits at +8, and refills it from the AOTE. */
                AST_$GET_ATTRIBUTES(&f.desc, 0x80, f.attrs,
                                    &f.local_status);
                if (f.local_status != 0) {
                    continue;                            /* 0x00E5F764 */
                }

                /* 0x00E5F768: attrs[1] is the object type byte. */
                if ((FILE_ATTR_OBJ_TYPE(f.attrs) == 1) ||
                    (FILE_ATTR_OBJ_TYPE(f.attrs) == 2)) {
                    if (FILE_ATTR_NOT_EMPTY(f.attrs) != 0) {
                        f.dir_not_empty = -1;            /* 0x00E5F784 */
                        if ((lock_mode == 1) &&
                            ((flags & FILE_LOCK_FLAG_FOR_DELETE) != 0)) {
                            *status_ret = status_$naming_bad_directory;
                            goto retry_on_in_use;        /* 0x00E5FC04 */
                        }
                    }
                }

                /* 0x00E5F7A4: attrs+2 is the volume flags word. */
                if ((FILE_ATTR_VOL_FLAGS(f.attrs) & 0x0002) != 0) {
                    if ((FILE_$LOCK_COMPAT_TABLE[lock_mode] & 0x0002) != 0) {
                        if ((FILE_ATTR_OBJ_TYPE(f.attrs) == 1) ||
                            (FILE_ATTR_OBJ_TYPE(f.attrs) == 2)) {
                            *status_ret = status_$naming_vol_mounted_read_only;
                        } else {
                            *status_ret = status_$file_volume_has_been_mounted_read_only;
                        }
                        goto retry_on_in_use;
                    }
                }

                /* 0x00E5F7EC */
                if ((hint_idx != 1) || (f.desc.flags < 0)) {
                    HINT_$ADDI(file_uid, &f.desc.loc_info);
                }
            }

            /* 0x00E5F81C: the AOTE says the object lives elsewhere. */
            if (f.desc.flags < 0) {
                if (local_only < 0) {
                    *status_ret = file_$cannot_create_on_remote_with_uid;
                    goto retry_on_in_use;                /* 0x00E5F82C */
                }
                /* 0x00E5F836: restart the scan with the AOTE's location. */
                f.hints[1].loc_info = f.desc.loc_info;
                f.hints[1].node     = f.desc.node;
                hint_idx = 0;
                continue;
            }

            /*
             * ---- local lock (0x00E5F848) ----
             */
            claimed = -1;
            priv_lock_check_rights(&f, status_ret, rights_out);
            ML_$LOCK(FILE_LOT_ML_LOCK);
            if (*status_ret != 0) {
                goto free_and_unlock;
            }

            FILE_$UID_LOCK_ACQUIRE(file_uid);            /* 0x00E5F870 */
            FILE_$UID_LOCK_RELEASE(file_uid);            /* 0x00E5F87A */

            /* 0x00E5F884: add_to_proc = claimed (TRUE here), skip_fill = FALSE */
            *status_ret = priv_lock_alloc_entry(&f, claimed, 0);
            if (*status_ret != 0) {
                goto free_and_unlock;
            }

            entry = FILE_$LOT_ENTRY(f.new_entry);        /* 0x00E5F89E */
            entry->rights = (uint8_t)(*rights_out & 0xFF);   /* 0x00E5F8BC */

            /* 0x00E5F8C2: skip_found = FALSE */
            *status_ret = priv_lock_check_conflicts(&f, 0);
            if (f.own_remote_lock < 0) {                 /* 0x00E5F8D2 */
                goto free_and_unlock;
            }
            if (*status_ret != 0) {                      /* 0x00E5F8DA */
                goto free_and_unlock;
            }

            priv_lock_link_entry(&f, entry);             /* 0x00E5F8E4 */
            ML_$UNLOCK(FILE_LOT_ML_LOCK);
            goto check_claimed;                          /* 0x00E5F8FC */
        }

        goto check_claimed;                              /* fell out of 0x00E5FB88 */
    }

    /*
     * --------------------------------------------------------------
     * Change-lock path (0x00E5F1E2)
     * --------------------------------------------------------------
     */
    f.req_mode  = (int16_t)FILE_$LOCK_REQ_TABLE[lock_mode];      /* 0x00E5F1EC */
    compat_mask = FILE_$LOCK_CVT_TABLE[lock_mode];               /* 0x00E5F1FC */

    if ((f.uid_is_null >= 0) && (f.req_mode != 4) && (f.req_mode != 0x0B)) {
        /* 0x00E5F216 */
        (void)AST_$PURIFY(file_uid, 0x8000, 0, &file_$purify_nil_list, 0,
                          status_ret);
        if (*status_ret != 0) {
            return;
        }
    }

    ML_$LOCK(FILE_LOT_ML_LOCK);                          /* 0x00E5F240 */
    f.found_entry = 0;
    f.found_slot  = 0;

    if (f.from_remote < 0) {
        /*
         * 0x00E5F25A: walk the hash chain looking for the remote requester's
         * own entry.
         */
        f.found_entry = (int16_t)FILE_$LOT_HASHTAB[f.hash];
        while (f.found_entry > 0) {
            entry = FILE_$LOT_ENTRY(f.found_entry);

            if ((entry->node_low == rem_node) &&          /* 0x00E5F28A */
                (entry->context == rem_key) &&            /* 0x00E5F294 */
                (entry->uid_high == file_uid->high) &&
                (entry->uid_low == file_uid->low) &&
                ((entry->flags2 & FILE_LOCK_F2_REMOTE) == 0) &&
                (((flags & FILE_LOCK_FLAG_CHANGE) != 0) ||
                 (((entry->flags2 & FILE_LOCK_F2_SIDE) >> 7) == side))) {

                if (entry->sequence == key) {             /* 0x00E5F2D0 */
                    *status_ret = 0;
                    goto free_and_unlock;
                }
                if ((compat_mask &
                     (1u << ((entry->flags2 & FILE_LOCK_F2_MODE_MASK) >> 3))) != 0) {
                    break;                                /* 0x00E5F2EE */
                }
            }
            f.found_entry = (int16_t)entry->next;         /* 0x00E5F2F2 */
        }
    } else if ((*slot_io == 0) || (*slot_io > 0x96)) {
        /*
         * 0x00E5F318: no usable slot hint, scan this process's slot vector.
         */
        if ((flags & FILE_LOCK_FLAG_CHANGE) == 0) {
            int16_t count = (int16_t)FILE_$PROC_LOT_COUNT(asid);

            f.found_slot = 0;
            if (count != 0) {
                /* 0x00E5F342: subq.w #1 + dbf => `count` iterations. */
                for (i = 1; i <= count; i++) {
                    uint16_t slot_entry = FILE_$PROC_LOT_SLOT(asid, i);

                    if (slot_entry == 0) {
                        continue;
                    }
                    entry = FILE_$LOT_ENTRY(slot_entry);
                    if ((compat_mask &
                         (1u << ((entry->flags2 & FILE_LOCK_F2_MODE_MASK) >> 3))) == 0) {
                        continue;
                    }
                    if ((entry->uid_high != file_uid->high) ||
                        (entry->uid_low != file_uid->low)) {
                        continue;
                    }
                    if (((entry->flags2 & FILE_LOCK_F2_SIDE) >> 7) != side) {
                        continue;
                    }
                    f.found_entry = (int16_t)slot_entry;  /* 0x00E5F3B8 */
                    f.found_slot  = i;
                    *slot_io      = (uint32_t)(uint16_t)i;
                    break;
                }
            }
        }
    } else {
        /*
         * 0x00E5F3DC: the caller named a slot.  `move.w (0x2,A0)` reads the
         * LOW half of the longword *slot_io.
         */
        f.found_slot  = (int16_t)(*slot_io & 0xFFFF);
        f.found_entry = (int16_t)FILE_$PROC_LOT_SLOT(asid, f.found_slot);

        /* The original does not check found_entry for 0 here: it forms
         * 0xE935CC + 0*0x1C and reads the flags2 byte that precedes the
         * table.  Preserved verbatim - FILE_$LOT_ENTRY(0) is the element
         * before the array. */
        entry = FILE_$LOT_ENTRY(f.found_entry);
        if (((compat_mask &
              (1u << ((entry->flags2 & FILE_LOCK_F2_MODE_MASK) >> 3))) != 0) &&
            (entry->uid_high == file_uid->high) &&
            (entry->uid_low == file_uid->low) &&
            (((flags & FILE_LOCK_FLAG_CHANGE) != 0) ||
             (((entry->flags2 & FILE_LOCK_F2_SIDE) >> 7) == side))) {
            /* keep found_entry / found_slot */
        } else {
            f.found_entry = 0;                            /* 0x00E5F454 */
            f.found_slot  = 0;
        }
    }

    /* 0x00E5F45C */
    if (f.found_entry == 0) {
        *status_ret = file_$illegal_lock_request;
        goto free_and_unlock;
    }

    entry = FILE_$LOT_ENTRY(f.found_entry);               /* 0x00E5F470 */

    /*
     * 0x00E5F48A: all three of these tests must hold before the request is
     * rejected - they are the consequent of the btst, not its alternative.
     */
    if (((flags & FILE_LOCK_FLAG_CHANGE) != 0) &&
        ((entry->flags2 & FILE_LOCK_F2_REMOTE) != 0) &&
        ((entry->flags2 & FILE_LOCK_F2_PENDING) == 0)) {
        *status_ret = file_$incompatible_request;
        goto free_and_unlock;
    }

    /* 0x00E5F4B0 */
    if (((int8_t)entry->flags1 < 0) &&
        ((entry->flags2 & FILE_LOCK_F2_REMOTE) == 0) &&
        ((FILE_$LOCK_COMPAT_TABLE[lock_mode] & 0x0002) != 0)) {
        *status_ret = status_$file_volume_has_been_mounted_read_only;
        goto free_and_unlock;
    }

    /* 0x00E5F4DC */
    if ((flags & FILE_LOCK_FLAG_NO_RIGHTS) == 0) {
        uint16_t need = FILE_$LOCK_COMPAT_TABLE[lock_mode];

        if ((uint16_t)(entry->rights & need) != need) {
            *status_ret = status_$insufficient_rights;
            goto free_and_unlock;
        }
    }

    /* 0x00E5F508 */
    f.desc.loc_info = entry->node_high;
    f.desc.node     = entry->node_low;
    f.desc.flags    = (int8_t)((f.desc.flags & 0x7F) |
                               ((entry->flags2 & FILE_LOCK_F2_REMOTE)
                                ? (int8_t)FILE_OBJ_LOC_REMOTE : 0));

    if (entry->refcount > 1) {
        /*
         * 0x00E5F538: the entry is shared, split off a private copy.
         */
        *rights_out = entry->rights;

        /* 0x00E5F544: add_to_proc = FALSE, skip_fill = TRUE */
        *status_ret = priv_lock_alloc_entry(&f, 0, -1);
        if (*status_ret != 0) {
            goto free_and_unlock;
        }

        new_entry = FILE_$LOT_ENTRY(f.new_entry);         /* 0x00E5F55E */
        *new_entry = *entry;                              /* 0x00E5F578, 7 longs */

        if ((entry->flags2 & FILE_LOCK_F2_REMOTE) != 0) {
            /* 0x00E5F590: flags with bit 6 (CHANGE) cleared. */
            priv_lock_remote_lock(&f, new_entry, (uint16_t)f.req_mode, side,
                                  (uint16_t)(flags & ~FILE_LOCK_FLAG_CHANGE), 0);
            ML_$LOCK(FILE_LOT_ML_LOCK);
            if (*status_ret != 0) {
                goto free_and_unlock;
            }
        }

        entry->refcount--;                                /* 0x00E5F5C6 */
        f.found_entry = f.new_entry;
        FILE_$PROC_LOT_SLOT(asid, f.found_slot) = (uint16_t)f.found_entry;
        priv_lock_link_entry(&f, new_entry);              /* 0x00E5F5F2 */
        f.new_entry = 0;
    } else {
        /* 0x00E5F602 */
        if ((entry->flags2 & FILE_LOCK_F2_REMOTE) != 0) {
            priv_lock_remote_lock(&f, entry, lock_mode, side, flags, -1);
            ML_$LOCK(FILE_LOT_ML_LOCK);
            if (*status_ret != 0) {
                goto free_and_unlock;
            }
        }
    }

    /* 0x00E5F63C */
    entry = FILE_$LOT_ENTRY(f.found_entry);

    if ((entry->flags2 & FILE_LOCK_F2_REMOTE) == 0) {
        /* 0x00E5F65E: skip_found = TRUE */
        *status_ret = priv_lock_check_conflicts(&f, -1);
        if (*status_ret != 0) {
            goto free_and_unlock;
        }
        entry->sequence = key;                            /* 0x00E5F678 */
    }

    /* 0x00E5F67E */
    entry->flags2 = (uint8_t)((entry->flags2 & 0x87) |
                              (uint8_t)(f.req_mode << 3));
    if ((flags & FILE_LOCK_FLAG_CHANGE) != 0) {
        entry->flags2 = (uint8_t)((entry->flags2 & 0x7F) |
                                  (uint8_t)(side << 7));
    }
    ML_$UNLOCK(FILE_LOT_ML_LOCK);                         /* 0x00E5F6A6 */
    goto netlog;                                          /* 0x00E5F6B4 */

check_claimed:                                            /* 0x00E5FB8E */
    if (claimed >= 0) {
        *status_ret = file_$object_not_found;
        return;
    }
    /* fall through */

netlog:                                                   /* 0x00E5FBA0 */
    if (NETLOG_$OK_TO_LOG >= 0) {
        return;
    }
    f.log_remote      = (f.desc.flags < 0) ? 1 : 0;
    f.log_from_remote = (f.from_remote < 0) ? 1 : 0;
    NETLOG_$LOG_IT(0x12, (uint32_t *)file_uid, 0, 0, side, lock_mode,
                   f.log_remote, f.log_from_remote);
    return;

free_and_unlock:                                          /* 0x00E5FBF2 */
    priv_lock_free_entry(&f);
    ML_$UNLOCK(FILE_LOT_ML_LOCK);
    /* fall through */

retry_on_in_use:                                          /* 0x00E5FC04 */
    if (*status_ret != file_$object_in_use) {
        return;
    }
    FILE_$READ_LOCK_ENTRYUI(file_uid, f.lock_info, &f.local_status);
    if (f.local_status != file_$object_not_locked_by_this_process) {
        return;
    }
    goto retry_from_top;                                  /* 0x00E5FC2C */
}

/*
 * ============================================================================
 * FILE_$PRIV_LOCK_$ALLOC_ENTRY (0x00E5EB98)
 *
 * Pops an entry off the free list, optionally fills it in from the frame, and
 * optionally claims a per-process slot for it.
 *
 *   add_to_proc  byte at A6+0x08 - claim a per-process slot
 *   skip_fill    byte at A6+0x0A - leave the entry contents alone
 * ============================================================================
 */
static status_$t priv_lock_alloc_entry(priv_lock_frame_t *f,
                                       boolean add_to_proc, boolean skip_fill)
{
    status_$t result = 0;                                 /* A6-0x08 */
    file_lock_entry_detail_t *entry;
    int16_t i;

    f->new_entry = (int16_t)FILE_$LOT_FREE;               /* 0x00E5EBAE */
    if (f->new_entry == 0) {
        return file_$local_lock_table_full;               /* 0x00E5ED42 */
    }

    entry = FILE_$LOT_ENTRY(f->new_entry);
    FILE_$LOT_FREE = entry->next;                         /* 0x00E5EBD2 */

    if (f->new_entry > (int16_t)FILE_$LOT_HIGH) {         /* 0x00E5EBD8 */
        FILE_$LOT_HIGH = (uint16_t)f->new_entry;
    }

    if (skip_fill >= 0) {                                 /* 0x00E5EBE8 */
        entry->uid_high = f->file_uid->high;              /* 0x00E5EC06 */
        entry->uid_low  = f->file_uid->low;

        if (f->from_remote < 0) {
            entry->context   = f->rem_key;                /* 0x00E5EC14 */
            entry->sequence  = f->key;
            entry->node_low  = f->rem_node;
            entry->node_high = f->rem_extra;
        } else {
            entry->node_low  = f->desc.node;              /* 0x00E5EC2E */
            entry->node_high = f->desc.loc_info;
            entry->context   = 0;
            entry->sequence  = 0;
        }

        entry->refcount = 0;                              /* 0x00E5EC42 */

        /* 0x00E5EC46: attrs+2 bit 1 (read-only volume) and not a null UID. */
        {
            uint8_t ro = (uint8_t)((((FILE_ATTR_VOL_FLAGS(f->attrs) & 0x0002)
                                     ? 0xFFu : 0x00u)
                                    & (uint8_t)~(uint8_t)f->uid_is_null) & 0x80u);

            entry->flags1 = (uint8_t)((entry->flags1 & 0x7F) | ro);
            entry->flags1 = (uint8_t)((entry->flags1 & 0xC0) |
                                      (uint8_t)f->desc.rights_bits);
        }

        /* 0x00E5EC74: side is the low byte of the word at A6+0x0E. */
        entry->flags2 = (uint8_t)((entry->flags2 & 0x7F) |
                                  (uint8_t)(f->side << 7));
        entry->flags2 = (uint8_t)((entry->flags2 & 0x87) |
                                  (uint8_t)(f->lock_mode << 3));
        entry->flags2 = (uint8_t)((entry->flags2 & 0xFB) |
                                  (uint8_t)(((f->desc.flags < 0) ? 1u : 0u) << 2));
        entry->flags2 |= FILE_LOCK_F2_PENDING;            /* 0x00E5ECA8 */
        entry->flags2 = (uint8_t)((entry->flags2 & 0xFE) |
                                  ((f->flags & FILE_LOCK_FLAG_ENTRY_BIT0) ? 1u : 0u));
    }

    /* 0x00E5ECC2: only register in the per-process table for local locks. */
    if ((int8_t)(add_to_proc & (int8_t)~(int8_t)f->from_remote) >= 0) {
        return result;
    }

    /* 0x00E5ECCC: moveq #0x95 + dbf => 150 slots. */
    for (i = 1; i <= FILE_PROC_LOCK_MAX_ENTRIES; i++) {
        if (FILE_$PROC_LOT_SLOT(f->asid, i) != 0) {
            continue;
        }
        f->new_slot = i;                                  /* 0x00E5ECF0 */
        FILE_$PROC_LOT_SLOT(f->asid, i) = (uint16_t)f->new_entry;
        if ((uint16_t)i > FILE_$PROC_LOT_COUNT(f->asid)) {
            FILE_$PROC_LOT_COUNT(f->asid) = (uint16_t)i;
        }
        *f->slot_io = (uint32_t)(uint16_t)i;              /* 0x00E5ED32 */
        return result;
    }

    return file_$local_lock_table_full;                   /* 0x00E5ED42 */
}

/*
 * ============================================================================
 * FILE_$PRIV_LOCK_$LINK_ENTRY (0x00E5ED58)
 *
 * Makes the freshly-built entry visible: refcount 1 and pushed onto its hash
 * bucket.
 * ============================================================================
 */
static void priv_lock_link_entry(priv_lock_frame_t *f,
                                 file_lock_entry_detail_t *entry)
{
    entry->refcount = 1;                                  /* 0x00E5ED64 */
    entry->next     = FILE_$LOT_HASHTAB[f->hash];         /* 0x00E5ED74 */
    FILE_$LOT_HASHTAB[f->hash] = (uint16_t)f->new_entry;  /* 0x00E5ED84 */
}

/*
 * ============================================================================
 * FILE_$PRIV_LOCK_$CHECK_RIGHTS (0x00E5ED92)
 *
 *   status      A6+0x08 (always the caller's status_ret)
 *   rights_out  A6+0x0C (always the caller's rights_out)
 * ============================================================================
 */
static void priv_lock_check_rights(priv_lock_frame_t *f,
                                   status_$t *status, uint16_t *rights_out)
{
    uint16_t need;
    int16_t  granted;

    *status = 0;                                          /* 0x00E5EDA0 */

    if (f->uid_is_null < 0) {
        *rights_out = 0x000F;                             /* 0x00E5EDAC */
        return;
    }

    if ((f->flags & FILE_LOCK_FLAG_NO_RIGHTS) != 0) {
        *rights_out = 0x0010;                             /* 0x00E5EDC2 */
        return;
    }

    need = FILE_$LOCK_COMPAT_TABLE[f->lock_mode];         /* 0x00E5EDCA */

    if ((f->flags & FILE_LOCK_FLAG_REMOTE) != 0) {
        /* 0x00E5EDDE: the ACL context is reached through one indirection. */
        void *acl_ctx = *f->acl_ctx;
        int8_t check_flag =
            (f->flags & FILE_LOCK_FLAG_ACL_CHECK) ? (int8_t)-1 : (int8_t)0;

        granted = ACL_$RIGHTS_CHECK(acl_ctx, f->file_uid,
                                    &file_$acl_rights_all,
                                    &file_$acl_zero_word,
                                    &check_flag, status);
    } else {
        /* 0x00E5EE12 */
        granted = ACL_$RIGHTS(f->file_uid, (boolean *)&file_$acl_ignore_super,
                              &file_$acl_rights_all,
                              &file_$acl_zero_word, status);
    }

    *rights_out = (uint16_t)granted;                      /* 0x00E5EE2E */

    if ((*status == 0x00230002) || (*status == 0) || (*status == 0x00230001)) {
        *status = 0;                                      /* 0x00E5EE52 */

        if ((f->flags & FILE_LOCK_FLAG_CHECK_RIGHTS) != 0) {
            if (*rights_out == 0) {
                *status = status_$no_rights;              /* 0x00E5EE62 */
            } else if ((uint16_t)(need & *rights_out) != need) {
                *status = status_$insufficient_rights;    /* 0x00E5EE72 */
            }
        }
    } else {
        OS_PROC_SHUTWIRED(status);                        /* 0x00E5EE4A */
    }
}

/*
 * ============================================================================
 * FILE_$PRIV_LOCK_$REMOTE_LOCK (0x00E5EE88)
 *
 *   entry   A6+0x08
 *   mode    A6+0x0C
 *   side    A6+0x0E
 *   flags   A6+0x10
 *   is_new  A6+0x12 (byte)
 *
 * Releases the lock-table lock across the network round trip; every caller
 * re-takes it afterwards.
 * ============================================================================
 */
static void priv_lock_remote_lock(priv_lock_frame_t *f,
                                  file_lock_entry_detail_t *entry,
                                  uint16_t mode, uint16_t side,
                                  uint16_t flags, boolean is_new)
{
    uint16_t key_out = 0;                                 /* A6-0x06 */

    if (is_new >= 0) {                                    /* 0x00E5EE9A */
        FILE_$LOT_SEQN++;
        entry->context = FILE_$LOT_SEQN;
    }

    ML_$UNLOCK(FILE_LOT_ML_LOCK);                         /* 0x00E5EEA4 */

    REM_FILE_$LOCK(&f->desc, side, mode, flags, f->rem_wait,
                   (entry->flags2 & FILE_LOCK_F2_PENDING) ? (int8_t)-1 : (int8_t)0,
                   entry->context,
                   (uint16_t *)f->slot_io, &key_out,
                   f->attrs, f->status_ret);

    if (*f->status_ret == 0) {                            /* 0x00E5EEF2 */
        entry->sequence = key_out;
    }
}

/*
 * ============================================================================
 * FILE_$PRIV_LOCK_$FREE_ENTRY (0x00E5EF08)
 *
 * Returns the entry allocated by ALLOC_ENTRY (if any) to the free list and
 * drops the per-process slot that went with it.
 * ============================================================================
 */
static void priv_lock_free_entry(priv_lock_frame_t *f)
{
    file_lock_entry_detail_t *entry;

    if (f->new_entry == 0) {                              /* 0x00E5EF10 */
        return;
    }

    entry = FILE_$LOT_ENTRY(f->new_entry);
    entry->next     = FILE_$LOT_FREE;                     /* 0x00E5EF30 */
    entry->refcount = 0;
    FILE_$LOT_FREE  = (uint16_t)f->new_entry;             /* 0x00E5EF3A */

    if (f->new_slot != 0) {                               /* 0x00E5EF40 */
        FILE_$PROC_LOT_SLOT(f->asid, f->new_slot) = 0;
    }

    f->new_entry = 0;                                     /* 0x00E5EF66 */
    f->new_slot  = 0;
}

/*
 * ============================================================================
 * FILE_$PRIV_LOCK_$CHECK_CONFLICTS (0x00E5EF76)
 *
 *   skip_found  byte at A6+0x08 - ignore the entry named by found_entry
 *
 * Walks the hash bucket for the object.  Returns file_$object_in_use when an
 * incompatible lock is already held, otherwise the status of the
 * AST_$SET_ATTRIBUTE that stamps the holder node onto the object.
 * ============================================================================
 */
static status_$t priv_lock_check_conflicts(priv_lock_frame_t *f, boolean skip_found)
{
    status_$t result = 0;                                 /* A6-0x3C */
    uint32_t  holder = 0;                                 /* A6-0x38 */
    uint16_t  allowed;                                    /* D0 */
    int16_t   idx;                                        /* D3 */
    boolean   saw_exclusive = 0;                          /* D2 */
    file_lock_entry_detail_t *entry;

    f->saw_other_lock  = 0;                               /* 0x00E5EF84 */
    f->own_remote_lock = 0;                               /* 0x00E5EF8A */

    /* 0x00E5EF8E: conflict matrix row for the request's mapped mode. */
    allowed = FILE_$LOCK_CONFLICT_TABLE[f->mapped_mode];

    idx = (int16_t)FILE_$LOT_HASHTAB[f->hash];            /* 0x00E5EF9C */

    while (idx > 0) {
        int16_t held_mode;                                /* D3 after the mask */
        int16_t held_side;

        entry = FILE_$LOT_ENTRY(idx);

        if ((entry->uid_high != f->file_uid->high) ||     /* 0x00E5EFD0 */
            (entry->uid_low != f->file_uid->low)) {
            idx = (int16_t)entry->next;                   /* 0x00E5F086 */
            continue;
        }

        if (f->from_remote < 0) {
            /* 0x00E5EFE2: this is the requester's own entry - nothing to do. */
            if ((entry->sequence == f->key) &&
                (entry->context == f->rem_key) &&
                (entry->node_low == f->rem_node)) {
                f->own_remote_lock = -1;                  /* 0x00E5F000 */
                return result;
            }
        }

        /* 0x00E5F008 */
        if ((skip_found < 0) && (idx == f->found_entry)) {
            idx = (int16_t)entry->next;
            continue;
        }

        f->saw_other_lock = -1;                           /* 0x00E5F012 */

        held_mode = (int16_t)((entry->flags2 & FILE_LOCK_F2_MODE_MASK) >> 3);
        if ((held_mode == 4) || (held_mode == 0x0B)) {
            saw_exclusive = -1;                           /* 0x00E5F02A */
        }

        /* 0x00E5F02C: map the held lock through FILE_$LOCK_MODE_TABLE too. */
        held_side = (int16_t)((entry->flags2 & FILE_LOCK_F2_SIDE) >> 7);
        held_mode = (int16_t)FILE_$LOCK_MODE_TABLE[held_side * 12 + held_mode];

        if ((allowed & (1u << (held_mode & 0x1F))) == 0) {
            result = file_$object_in_use;                 /* 0x00E5F07C */
            return result;
        }

        /* 0x00E5F04E: compatible in the abstract, but modes 2 and 6 are not
         * shareable across nodes. */
        if ((entry->node_low & 0xFFFFF) != f->node_id) {
            if ((f->mapped_mode == 2) || (held_mode == 2)) {
                result = file_$object_in_use;
                return result;
            }
            if ((f->mapped_mode == 6) && (held_mode == 6)) {
                result = file_$object_in_use;
                return result;
            }
        }

        idx = (int16_t)entry->next;                       /* 0x00E5F086 */
    }

    /* 0x00E5F08E */
    if (f->uid_is_null < 0) {
        return result;
    }
    if (f->desc.flags < 0) {
        return result;
    }

    if (f->dir_not_empty >= 0) {                          /* 0x00E5F09A */
        if ((f->mapped_mode == 5) || (f->mapped_mode == 2)) {
            holder = f->node_id;                          /* 0x00E5F0B0 */
        } else if (saw_exclusive < 0) {
            return result;                                /* 0x00E5F0BA */
        } else {
            holder = 0;                                   /* 0x00E5F0BC */
        }
    } else {
        holder = 0;                                       /* 0x00E5F0BC */
    }

    /* 0x00E5F0C0 */
    AST_$SET_ATTRIBUTE(f->file_uid, FILE_ATTR_LOCK_NODE, &holder, f->status_ret);
    result = *f->status_ret;
    return result;
}
