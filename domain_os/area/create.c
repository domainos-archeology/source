/*
 * AREA_$CREATE - Create a new area in the current address space
 * AREA_$CREATE_FROM - Create area from a remote UID (deduplicating)
 * area_$internal_create - Internal area creation helper
 *
 * Original addresses:
 *   area_$internal_create: 0x00E077DA
 *   AREA_$CREATE:          0x00E079C0
 *   AREA_$CREATE_FROM:     0x00E07A02
 *
 * A5 (the Pascal module data base) is 0xE1E118 = AREA_GLOBALS_BASE for all
 * three entry points; module globals appear below under their recovered
 * names rather than as (offset,A5) arithmetic.
 */

#include "area/area_internal.h"
#include "math/math.h"
#include "misc/crash_system.h"

/* Virtual size is rounded up to 32KB: addi.l #0x7fff / andi.l #-0x8000
 * at 0x00E077FE. */
#define VIRT_SIZE_ALIGN     0x8000

/* Backing-store overhead is charged in 1KB units: the lsl.l #0x8 + lsl.l #0x2
 * pair at 0x00E0790C. */
#define COMMIT_SIZE_ALIGN   0x400

/* Number of entries area_$internal_create asks for when the free list runs
 * dry (move.w #0x60,-(SP) at 0x00E07824). */
#define AREA_ALLOC_BATCH    0x60

/*
 * area_$internal_create - Internal area creation
 *
 * Takes an entry off the free list, initializes it, optionally allocates
 * remote backing store on the diskless partner, and sizes the area.
 *
 * Parameters (stack offsets in the original frame):
 *   virt_size    (0x08) Virtual size, rounded up to 32KB here
 *   commit_size  (0x0C) Committed size
 *   remote_uid   (0x10) Remote UID; 0 means "local create, take the lock"
 *   owner_asid   (0x14) Owner address space ID
 *   alloc_remote (0x16) Non-zero: allocate remote backing store
 *   shared       (0x18) Domain boolean; true (0xFF) sets AREA_FLAG_REVERSED
 *   status_p     (0x1A) Output: status code
 *
 * Returns: area handle (generation << 16 | area_id), or 0 on failure.
 *
 * Original address: 0x00E077DA
 */
uint32_t area_$internal_create(uint32_t virt_size, uint32_t commit_size,
                               uint32_t remote_uid, int16_t owner_asid,
                               int16_t alloc_remote, boolean shared,
                               status_$t *status_p)
{
    area_$entry_t *entry;
    area_$handle_t handle;              /* (-0x8,A6) */
    area_$handle_t result = 0;          /* (-0x20,A6), cleared at 0x00E077FA */
    int16_t area_id;
    int16_t remote_volx = 0;            /* D6, cleared at 0x00E07886 */
    uint16_t local_volx = 0;            /* (-0x22,A6) */
    status_$t temp_status[2];           /* (-0x14,A6) */
    uint32_t total_size;                /* D4 */
    uint32_t total_commit;              /* D1 / (-0xc,A6) */

    /* 0x00E077FE: virt_size := (virt_size + 0x7FFF) and not 0x7FFF */
    virt_size = (virt_size + (VIRT_SIZE_ALIGN - 1)) & ~(uint32_t)(VIRT_SIZE_ALIGN - 1);

    /* 0x00E0780A: only a local create takes the area lock. */
    if (remote_uid == 0) {
        ML_$LOCK(ML_LOCK_AREA);
    }

    /*
     * 0x00E0781C: if the free list is empty, try to extend the area table.
     * area_$alloc_resources returns a Domain boolean: true (0xFF, i.e. < 0)
     * means the table grew.  `tst.b D0b / bmi` at 0x00E0782E.
     */
    if (AREA_$FREE_LIST == NULL) {
        boolean grew = area_$alloc_resources(AREA_ALLOC_BATCH);
        if (grew >= 0) {
            *status_p = status_$area_none_free;
            /*
             * 0x00E07838: the remote path returns while still holding
             * nothing; only the local path unlocks.
             */
            if (remote_uid == 0) {
                ML_$UNLOCK(ML_LOCK_AREA);
            }
            return result;
        }
    }

    /* 0x00E0784E: unlink the head of the free list. */
    entry = AREA_$FREE_LIST;
    AREA_$FREE_LIST = entry->next;

    /*
     * 0x00E07856: a local create is threaded onto the owning ASID's list.
     * The original indexes with a *word* shift (`move.w D4w,D0w /
     * lsl.w #0x2,D0w / lea (0x0,A5,D0w*0x1),A0`), so the index is the
     * sign-extended word owner_asid * 4.
     */
    if (remote_uid == 0) {
        area_$entry_t **asid_head = &AREA_$ASID_LIST[(int16_t)(owner_asid)];

        entry->next = *asid_head;
        if (entry->next != NULL) {
            entry->next->prev = entry;      /* 0x00E07868 */
        }
        entry->prev = NULL;                 /* 0x00E0786E */
        *asid_head = entry;                 /* 0x00E07872 */
    }

    /* 0x00E07876 onward: initialize the entry. */
    entry->virt_size = 0;
    entry->commit_size = 0;
    entry->remote_uid = remote_uid;
    entry->remote_volx = 0;
    entry->owner_asid = owner_asid;
    entry->generation++;                    /* addq.w #0x1,(0x2c,A3) */

    /* 0x00E07890: the whole flags word is stored, not or-ed. */
    entry->flags = AREA_FLAG_ACTIVE | AREA_FLAG_SHARED;
    if (shared < 0) {
        /*
         * 0x00E0789A `bset.b #0x1,(0x2f,A3)`: 0x2F is the *low* byte of the
         * flags word at 0x2E, so this is bit 1 of the word.
         */
        entry->flags |= AREA_FLAG_REVERSED;
    }

    entry->first_bste = -1;                 /* 0x00E078A0 */

    /* 0x00E078A6: hand out and bump the module-wide caller id. */
    entry->caller_id = AREA_$NEXT_CALLER_ID;
    AREA_$NEXT_CALLER_ID++;

    AREA_$N_FREE--;                         /* 0x00E078B0 */

    if (remote_uid == 0) {
        ML_$UNLOCK(ML_LOCK_AREA);           /* 0x00E078B8 */
    }

    /*
     * 0x00E078C6-0x00E078DA: build the handle.  The generation is read
     * *after* the increment above and lands in the high word; the area id
     * is (entry - 0xD94C00) / 0x30 + 1 (divu.w, so a 1-based index).
     */
    area_id = AREA_ENTRY_TO_ID(entry);
    handle = AREA_MAKE_HANDLE(entry->generation, area_id);

    entry->volx = 0;                        /* 0x00E078DE (D6 is still 0) */

    if (AREA_$PARTNER.low == 0) {
        /*
         * 0x00E078E2: no diskless partner, so this node backs the area on
         * its own boot volume.
         */
        entry->volx = CAL_$BOOT_VOLX;
    } else if (alloc_remote != 0) {         /* 0x00E078F2 tst.w (0x16,A6) */
        int32_t overhead;                   /* D4 */

        /*
         * 0x00E078F8-0x00E07910.  The sign test is on the *signed 32-bit*
         * value virt_size-1 and happens BEFORE the shift, so virt_size == 0
         * wraps to -1, gets biased by 0x3FFFF and yields one 1KB unit:
         *
         *   D4 := virt_size - 1
         *   if D4 < 0 then D4 := D4 + 0x3FFFF          ; bpl / addi.l
         *   D4 := sign_extend_word(high_word(D4))      ; swap / ext.l
         *   D4 := D4 asr 2                             ; asr.l #0x2
         *   D4 := (D4 + 1) shl 10                      ; addq / lsl #8 / lsl #2
         *   D4 := virt_size + D4
         *
         * For virt_size == 0 this is 0x400, not 0x1000000.
         */
        overhead = (int32_t)virt_size - 1;
        if (overhead < 0) {
            overhead += 0x3FFFF;
        }
        overhead = (int32_t)(int16_t)((uint32_t)overhead >> 16);
        overhead >>= 2;
        total_size = virt_size + (uint32_t)(overhead + 1) * COMMIT_SIZE_ALIGN;

        /* 0x00E07912: the extra backing store is charged to the commit. */
        total_commit = commit_size + (total_size - virt_size);

        /*
         * 0x00E0791C: AREA_$PARTNER is passed BY ADDRESS (`pea (0x5cc,A5)`).
         * It is an 8-byte node address, not a scalar.
         */
        remote_volx = (int16_t)REM_FILE_$CREATE_AREA(
            &AREA_$PARTNER,
            total_size,
            total_commit,
            entry->caller_id,
            shared,
            &local_volx,
            status_p);

        if (*status_p != status_$ok) {
            /* 0x00E07942: throw the half-built area away. */
            AREA_$DELETE(handle, temp_status);
            return result;
        }

        if (AREA_$PARTNER_PKT_SIZE == 0) {  /* 0x00E07950 */
            /* network.h types the node address as uint32_t*; the callee
             * reads (A0) and (0x4,A0), i.e. the same 8-byte record. */
            AREA_$PARTNER_PKT_SIZE =
                (int16_t)NETWORK_$GET_PKT_SIZE((uint32_t *)&AREA_$PARTNER,
                                               local_volx);
        }
    }

    entry->remote_volx = remote_volx;       /* 0x00E0796C */

    if (virt_size != 0) {                   /* 0x00E07970 tst.l D2 */
        area_$resize(area_id, entry, virt_size, commit_size, 0, status_p);
    } else {
        *status_p = status_$ok;             /* 0x00E0798C clr.l (A2) */
    }

    if (*status_p == status_$ok) {
        result = handle;                    /* 0x00E07992 */
    } else {
        /*
         * 0x00E0799A: `st -(SP)` (true) for a local create, `clr.w -(SP)`
         * (false) for a remote one - only a local create is threaded onto
         * an ASID list and therefore needs unlinking.
         */
        boolean do_unlink = (remote_uid == 0) ? true : false;
        area_$internal_delete(entry, area_id, temp_status, do_unlink);
    }

    return result;
}

/*
 * AREA_$CREATE - Create a new area in the current address space
 *
 * A thin wrapper: always a local create (remote_uid 0) for the running
 * process's ASID, with remote backing allowed.
 *
 * The handle returned by area_$internal_create is stored into a dead local
 * at 0x00E079EE and never read, so this is a Pascal *procedure*: it reports
 * only the status.
 *
 * Original address: 0x00E079C0
 */
void AREA_$CREATE(uint32_t virt_size, uint32_t commit_size,
                  boolean shared, status_$t *status_p)
{
    status_$t local_status;             /* (-0x4,A6) */

    (void)area_$internal_create(virt_size,
                                commit_size,
                                0,              /* remote_uid: local create */
                                PROC1_$AS_ID,   /* 0x00E079DA */
                                1,              /* alloc_remote */
                                shared,
                                &local_status);

    *status_p = local_status;           /* 0x00E079F2 */
}

/*
 * AREA_$CREATE_FROM - Create an area from a remote UID (deduplicating)
 *
 * Looks the remote UID up in the module hash table.  If an area already
 * exists for the same UID *and* caller id, that area's id is returned and
 * AREA_$CR_DUP is bumped; otherwise a new area is created and threaded onto
 * the hash chain.
 *
 * Returns: an area id (word).
 *
 * Original address: 0x00E07A02
 */
uint16_t AREA_$CREATE_FROM(uint32_t remote_uid, uint32_t virt_size,
                           uint32_t commit_size, int32_t caller_id,
                           status_$t *status_p)
{
    /*
     * D2 holds remote_uid on entry (0x00E07A10) and is the value moved to
     * D0w by the common exit at 0x00E07B44.  Both success paths overwrite
     * it with the area id first; the "hash pool exhausted" path at
     * 0x00E07AC4 does not.  See the comment there.
     */
    uint32_t d2_result = remote_uid;
    area_$uid_hash_t *hash_entry;       /* A2 */
    area_$entry_t *entry;               /* A1 / A0 */
    area_$handle_t handle;              /* D0 */
    uint16_t area_id;                   /* D0w */
    uint16_t hash_bucket;

    ML_$LOCK(ML_LOCK_AREA);

    /* 0x00E07A2A: bucket := remote_uid mod 11 */
    hash_bucket = (uint16_t)M$OIU$WLW((long)remote_uid, AREA_UID_HASH_BUCKETS);

    /*
     * 0x00E07A4C-0x00E07A5C.  Note there is no null check on first_entry:
     * the original loads (0x4,A2) into A0 and immediately reads (0x20,A0).
     * An empty bucket record would fault; AREA_$INIT and the unlink paths
     * never leave one on a chain.
     */
    hash_entry = AREA_$UID_HASH[hash_bucket];
    while (hash_entry != NULL) {
        if (hash_entry->first_entry->remote_uid == remote_uid) {
            break;
        }
        hash_entry = hash_entry->next;
    }

    if (hash_entry != NULL) {           /* 0x00E07A5E */
        /* 0x00E07A64-0x00E07A8E: match on caller id within the chain. */
        for (entry = hash_entry->first_entry; entry != NULL; entry = entry->next) {
            if ((int32_t)entry->caller_id == caller_id) {
                d2_result = (uint32_t)(uint16_t)AREA_ENTRY_TO_ID(entry);
                AREA_$CR_DUP++;         /* 0x00E07A7E */
                *status_p = status_$ok;
                ML_$UNLOCK(ML_LOCK_AREA);
                return (uint16_t)d2_result;
            }
        }
    }

    /* 0x00E07A90: no match - create the area. */
    handle = area_$internal_create(virt_size,
                                   commit_size,
                                   remote_uid,
                                   0,       /* owner_asid */
                                   0,       /* alloc_remote */
                                   false,   /* shared */
                                   status_p);
    /* 0x00E07AAA stores the handle into a dead local; D0 stays live and only
     * its low word (the area id) is used from here on. */
    area_id = (uint16_t)AREA_HANDLE_TO_ID(handle);

    if (*status_p == status_$ok) {      /* 0x00E07AAE */
        if (hash_entry == NULL) {       /* 0x00E07AB4 */
            /* 0x00E07ABA: take a chain record off the pool free list. */
            hash_entry = AREA_$UID_HASH_FREE;

            if (hash_entry == NULL) {
                /*
                 * 0x00E07AC4: the pool is exhausted.  The area just created
                 * is deleted (do_unlink false, `clr.w -(SP)`) and
                 * status_$area_no_free_resources is reported.
                 *
                 * ORIGINAL BUG (0x00E07AEC -> 0x00E07B38 -> 0x00E07B44):
                 * this path branches straight to the epilogue without doing
                 * the `move.w D0w,D2w` at 0x00E07B36, so D2 still holds the
                 * remote UID loaded at 0x00E07A10 and the function returns
                 * the LOW WORD OF remote_uid instead of the area id.  The
                 * caller therefore gets a garbage area id alongside a
                 * non-zero status.  Reproduced here deliberately.
                 */
                area_$internal_delete(AREA_ID_TO_ENTRY(area_id), (int16_t)area_id,
                                      status_p, false);
                *status_p = status_$area_no_free_resources;
                ML_$UNLOCK(ML_LOCK_AREA);
                return (uint16_t)d2_result;
            }

            /* 0x00E07AEE: pop it and push it onto this bucket. */
            AREA_$UID_HASH_FREE = hash_entry->next;
            hash_entry->next = AREA_$UID_HASH[hash_bucket];
            AREA_$UID_HASH[hash_bucket] = hash_entry;
            hash_entry->first_entry = NULL;
        }

        /* 0x00E07B00: thread the new entry onto the head of the chain. */
        entry = AREA_ID_TO_ENTRY(area_id);
        entry->caller_id = (uint32_t)caller_id;

        if (hash_entry->first_entry != NULL) {
            hash_entry->first_entry->prev = entry;
        }
        entry->next = hash_entry->first_entry;
        hash_entry->first_entry = entry;
        entry->prev = NULL;
    }

    d2_result = area_id;                /* 0x00E07B36 move.w D0w,D2w */

    ML_$UNLOCK(ML_LOCK_AREA);
    return (uint16_t)d2_result;
}
