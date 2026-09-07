/*
 * AREA_$FREE_ASID - Free all areas owned by an address space
 * AREA_$SHUTDOWN - Shutdown area subsystem
 * AREA_$FREE_FROM - Free areas from specific context
 *
 * Original addresses:
 *   AREA_$FREE_ASID: 0x00E07E80
 *   AREA_$SHUTDOWN: 0x00E07F0E
 *   AREA_$FREE_FROM: 0x00E07FC6
 */

#include "area/area_internal.h"
#include "misc/crash_system.h"
#include "math/math.h"

/* Number of ASIDs to iterate */
#define ASID_COUNT          58

/* Number of UID hash buckets */
#define UID_HASH_BUCKETS    11

/*
 * AREA_$FREE_ASID - Free all areas owned by an address space
 *
 * Called when an address space is being destroyed. Iterates through
 * all areas owned by the specified ASID and deletes them.
 *
 * Parameters:
 *   asid - Address space ID whose areas should be freed
 *
 * Original address: 0x00E07E80
 */
void AREA_$FREE_ASID(int16_t asid)
{
    area_$entry_t *entry;
    area_$entry_t *next;
    status_$t status;

    ML_$LOCK(ML_LOCK_AREA);

    /*
     * Get head of ASID's area list.  AREA_$ASID_LIST is the array at
     * AREA_GLOBALS_BASE + 0x4D8 that the original indexes with
     * `lsl.w #0x2` on the ASID.
     */
    area_$entry_t **asid_list = &AREA_$ASID_LIST[asid];
    entry = *asid_list;

    /* Iterate through all areas for this ASID */
    while (entry != NULL) {
        /* Save next pointer before modifying entry */
        next = entry->next;

        /* Mark BSTE as invalid */
        entry->first_bste = -1;

        /* Delete the area */
        area_$internal_delete(entry, entry->reserved_2a, &status, 0);

        if (status != status_$ok) {
            CRASH_SYSTEM(&status);
        }

        /* Clear prev pointer and add to free list */
        entry->prev = NULL;
        entry->next = AREA_$FREE_LIST;
        AREA_$FREE_LIST = entry;
        AREA_$N_FREE++;

        entry = next;
    }

    /* Clear the ASID list head */
    *asid_list = NULL;

    ML_$UNLOCK(ML_LOCK_AREA);
}

/*
 * AREA_$SHUTDOWN - Shutdown area subsystem
 *
 * Called during system shutdown. Frees all areas from all ASIDs,
 * then cleans up the UID hash table.
 *
 * Original address: 0x00E07F0E
 */
void AREA_$SHUTDOWN(void)
{
    int i;
    area_$uid_hash_t *hash_entry;
    area_$uid_hash_t *next_hash;
    area_$entry_t *entry;
    area_$entry_t *next_entry;
    status_$t status;

    /* First, free all areas from all ASIDs */
    for (i = 0; i < ASID_COUNT; i++) {
        AREA_$FREE_ASID(i);
    }

    ML_$LOCK(ML_LOCK_AREA);

    /* Clean up UID hash table */
    for (i = 0; i < UID_HASH_BUCKETS; i++) {
        /* Get hash bucket (AREA_GLOBALS_BASE + 0x454) */
        hash_entry = AREA_$UID_HASH[i];

        while (hash_entry != NULL) {
            /* Process all entries linked to this hash bucket */
            entry = hash_entry->first_entry;

            while (entry != NULL) {
                next_entry = entry->next;

                /* Mark BSTE as invalid */
                entry->first_bste = -1;

                /* Delete the area */
                area_$internal_delete(entry, entry->reserved_2a, &status, 0);

                if (status != status_$ok) {
                    CRASH_SYSTEM(&status);
                }

                /* Clear prev pointer and add to free list */
                entry->prev = NULL;
                entry->next = AREA_$FREE_LIST;
                AREA_$FREE_LIST = entry;
                AREA_$N_FREE++;

                entry = next_entry;
            }

            /* Clear first_entry and return hash entry to pool */
            hash_entry->first_entry = NULL;
            next_hash = hash_entry->next;

            /* Link hash entry back to free pool (AREA_GLOBALS_BASE + 0x450) */
            hash_entry->next = AREA_$UID_HASH_FREE;
            AREA_$UID_HASH_FREE = hash_entry;

            hash_entry = next_hash;
        }
    }

    ML_$UNLOCK(ML_LOCK_AREA);
}

/*
 * AREA_$FREE_FROM - Free every area created from one remote UID
 *
 * Looks the remote UID up in the area subsystem's UID hash table, deletes
 * every area entry chained off that hash record, returns those entries to
 * the free list, unlinks the hash record from its bucket and returns it to
 * the hash-record pool.
 *
 * Parameters:
 *   remote_uid - the value AREA_$CREATE_FROM stored in
 *                area_$entry_t.remote_uid (+0x20)
 *
 * Original address: 0x00E07FC6 (210 bytes)
 *
 * Full instruction trace:
 *   00e07fc6  link.w A6,-0x18
 *   00e07fca  movem.l {A5 A4 A3 A2 D3 D2},-(SP)
 *   00e07fce  lea (0xe1e118).l,A5      ; AREA_GLOBALS_BASE
 *   00e07fd6  move.l (0x8,A6),D2       ; remote_uid
 *   00e07fda  move.w #0xe,-(SP) / jsr 0x00e20b12.l   ; ML_$LOCK(0x0E)
 *   00e07fe8  move.w #0xb,-(SP)        ; AREA_UID_HASH_BUCKETS
 *   00e07fec  move.l D2,-(SP)
 *   00e07fee  jsr 0x00e0acc0.l         ; M$OIU$WLW -> bucket in D0
 *   00e07ffa  lsl.l #0x2,D1
 *   00e07ffc  lea (0x0,A5,D1),A4
 *   00e08000  movea.l (0x454,A4),A3    ; hash = AREA_$UID_HASH[bucket]
 *   00e08004  clr.l D3                 ; prev = NULL
 *   00e08006  bra.b 0x00e08016
 *   00e08008  movea.l (0x4,A3),A0      ; hash->first_entry (NOT null-checked)
 *   00e0800c  cmp.l (0x20,A0),D2       ; entry->remote_uid
 *   00e08010  beq.b 0x00e0801c
 *   00e08012  move.l A3,D3             ; prev = hash
 *   00e08014  movea.l (A3),A3          ; hash = hash->next
 *   00e08016  cmpa.w #0x0,A3 / bne.b 0x00e08008
 *   00e0801c  cmpa.w #0x0,A3 / beq.b 0x00e08082
 *   00e08022  movea.l (0x4,A3),A2      ; entry = hash->first_entry
 *   00e08026  bra.b 0x00e08062
 *   00e08028  move.l (A2),D2           ; next = entry->next -- D2 IS REUSED,
 *                                      ; the remote_uid is dead from here on
 *   00e0802a  clr.w -(SP)              ; do_unlink = FALSE       (arg 4)
 *   00e0802c  pea (-0x4,A6)            ; &status                 (arg 3)
 *   00e08030  move.w (0x2a,A2),-(SP)   ; entry->reserved_2a      (arg 2)
 *   00e08034  pea (A2)                 ; entry                   (arg 1)
 *   00e08036  bsr.w 0x00e07b50         ; area_$internal_delete
 *   00e0803e  tst.l (-0x4,A6) / beq.b 0x00e08050
 *   00e08044  pea (-0x4,A6) / jsr 0x00e1e700.l       ; CRASH_SYSTEM(&status)
 *   00e08050  clr.l (0x4,A2)           ; entry->prev = NULL
 *   00e08054  move.l (0x5c8,A5),(A2)   ; entry->next = AREA_$FREE_LIST
 *   00e08058  move.l A2,(0x5c8,A5)     ; AREA_$FREE_LIST = entry
 *   00e0805c  movea.l D2,A2            ; entry = next
 *   00e0805e  addq.w #0x1,(0x5e0,A5)   ; AREA_$N_FREE++
 *   00e08062  cmpa.w #0x0,A2 / bne.b 0x00e08028
 *   00e08068  clr.l (0x4,A3)           ; hash->first_entry = NULL
 *   00e0806c  tst.l D3 / bne.b 0x00e08076
 *   00e08070  move.l (A3),(0x454,A4)   ; AREA_$UID_HASH[bucket] = hash->next
 *   00e08074  bra.b 0x00e0807a
 *   00e08076  movea.l D3,A0 / move.l (A3),(A0)       ; prev->next = hash->next
 *   00e0807a  move.l (0x450,A5),(A3)   ; hash->next = AREA_$UID_HASH_FREE
 *   00e0807e  move.l A3,(0x450,A5)     ; AREA_$UID_HASH_FREE = hash
 *   00e08082  move.w #0xe,-(SP) / jsr 0x00e20b62.l   ; ML_$UNLOCK(0x0E)
 *   00e0808e  movem.l (-0x30,A6),{D2 D3 A2 A3 A4 A5} / unlk A6 / rts
 *
 * Note that the bucket walk dereferences hash->first_entry without checking
 * it for NULL (0x00E08008), and that AREA_$N_FREE is bumped AFTER the
 * entry pointer has already advanced -- neither matters, but both are
 * reproduced here as found.
 */
void AREA_$FREE_FROM(uint32_t remote_uid)
{
    area_$uid_hash_t *hash_entry;
    area_$uid_hash_t *prev_hash;
    area_$entry_t *entry;
    area_$entry_t *next_entry;
    status_$t status;
    int16_t bucket;

    /* 0x00E07FDA */
    ML_$LOCK(ML_LOCK_AREA);

    /* 0x00E07FEE */
    bucket = M$OIU$WLW((long)remote_uid, AREA_UID_HASH_BUCKETS);

    /* 0x00E08000 */
    hash_entry = AREA_$UID_HASH[bucket];
    prev_hash = NULL;

    /* 0x00E08008-0x00E0801A */
    while (hash_entry != NULL) {
        if (hash_entry->first_entry->remote_uid == remote_uid) {
            break;
        }
        prev_hash = hash_entry;
        hash_entry = hash_entry->next;
    }

    /* 0x00E0801C */
    if (hash_entry != NULL) {
        /* 0x00E08022 */
        entry = hash_entry->first_entry;

        while (entry != NULL) {
            /* 0x00E08028 */
            next_entry = entry->next;

            /* 0x00E08036: do_unlink is pushed as a zero word, i.e. FALSE */
            area_$internal_delete(entry, entry->reserved_2a, &status, 0);

            /* 0x00E0803E */
            if (status != status_$ok) {
                CRASH_SYSTEM(&status);
            }

            /* 0x00E08050-0x00E08058 */
            entry->prev = NULL;
            entry->next = AREA_$FREE_LIST;
            AREA_$FREE_LIST = entry;

            /* 0x00E0805C / 0x00E0805E */
            entry = next_entry;
            AREA_$N_FREE++;
        }

        /* 0x00E08068 */
        hash_entry->first_entry = NULL;

        /* 0x00E0806C: unlink the hash record from its bucket */
        if (prev_hash == NULL) {
            AREA_$UID_HASH[bucket] = hash_entry->next;
        } else {
            prev_hash->next = hash_entry->next;
        }

        /* 0x00E0807A: and return it to the record pool */
        hash_entry->next = AREA_$UID_HASH_FREE;
        AREA_$UID_HASH_FREE = hash_entry;
    }

    /* 0x00E08082 */
    ML_$UNLOCK(ML_LOCK_AREA);
}
