/*
 * AREA_$CREATE - Create a new area in current address space
 * AREA_$CREATE_FROM - Create area from remote UID (deduplicating)
 * area_$internal_create - Internal area creation helper
 *
 * Original addresses:
 *   AREA_$CREATE: 0x00E079C0
 *   AREA_$CREATE_FROM: 0x00E07A02
 *   area_$internal_create: 0x00E077DA
 */

#include "area/area_internal.h"
#include "misc/crash_system.h"

/* Size alignment constants */
#define VIRT_SIZE_ALIGN     0x8000      /* 32KB alignment for virtual size */
#define COMMIT_SIZE_ALIGN   0x400       /* 1KB alignment for commit size */

/*
 * area_$internal_create - Internal area creation
 *
 * This is the core area creation routine called by AREA_$CREATE and
 * AREA_$CREATE_FROM. It allocates an area entry from the free list,
 * initializes it, and optionally sets up remote backing store.
 *
 * Parameters:
 *   virt_size     - Virtual size (will be rounded up to 32KB)
 *   commit_size   - Committed size (will be rounded up to 1KB)
 *   remote_uid    - Remote UID (0 for local areas)
 *   owner_asid    - Owner address space ID
 *   alloc_remote  - If non-zero, allocate remote backing
 *   shared        - If negative, area is shared
 *   status_p      - Output: status code
 *
 * Returns: Area handle (generation << 16 | area_id), or 0 on failure
 *
 * Original address: 0x00E077DA
 */
uint32_t area_$internal_create(uint32_t virt_size, uint32_t commit_size,
                               uint32_t remote_uid, int16_t owner_asid,
                               int16_t alloc_remote, int8_t shared,
                               status_$t *status_p)
{
    area_$entry_t *entry;
    uint32_t handle = 0;
    int16_t area_id;
    int16_t remote_volx = 0;
    int16_t local_volx;
    uint32_t *globals = (uint32_t *)AREA_GLOBALS_BASE;
    status_$t temp_status[2];
    uint32_t total_size;

    /* Round up sizes to alignment boundaries */
    virt_size = (virt_size + VIRT_SIZE_ALIGN - 1) & ~(VIRT_SIZE_ALIGN - 1);

    /* Lock if this is not a remote-created area */
    if (remote_uid == 0) {
        ML_$LOCK(ML_LOCK_AREA);
    }

    /*
     * Check if we have free area entries.
     * If free list is empty, try to allocate more resources.
     */
    if (AREA_$FREE_LIST == NULL) {
        int8_t result = area_$alloc_resources(0x60);
        if (result >= 0) {
            /* Allocation failed */
            *status_p = status_$area_none_free;
            if (remote_uid == 0) {
                ML_$UNLOCK(ML_LOCK_AREA);
            }
            return 0;
        }
    }

    /* Take entry from free list */
    entry = AREA_$FREE_LIST;
    AREA_$FREE_LIST = entry->next;

    /* If this is a local creation, link into per-ASID list */
    if (remote_uid == 0) {
        int asid_offset = owner_asid * sizeof(uint32_t);
        area_$entry_t **asid_list = (area_$entry_t **)((char *)globals + 0x4D8 + asid_offset);

        entry->next = *asid_list;
        if (entry->next != NULL) {
            entry->next->prev = entry;
        }
        entry->prev = NULL;
        *asid_list = entry;
    }

    /* Initialize entry fields */
    entry->virt_size = 0;
    entry->commit_size = 0;
    entry->remote_uid = remote_uid;
    entry->remote_volx = 0;
    entry->owner_asid = owner_asid;

    /* Increment generation number */
    entry->generation++;

    /* Set initial flags: ACTIVE | SHARED (0x09) */
    entry->flags = AREA_FLAG_ACTIVE | AREA_FLAG_SHARED;
    if (shared < 0) {
        entry->flags |= AREA_FLAG_REVERSED;
    }

    /* Set first BSTE to unset */
    entry->first_bste = -1;

    /* Assign unique caller ID from global counter */
    entry->caller_id = *(uint32_t *)((char *)globals + 0x5C4);
    (*(uint32_t *)((char *)globals + 0x5C4))++;

    /* Decrement free count */
    AREA_$N_FREE--;

    /* Unlock if local creation */
    if (remote_uid == 0) {
        ML_$UNLOCK(ML_LOCK_AREA);
    }

    /* Calculate area ID and build handle */
    area_id = AREA_ENTRY_TO_ID(entry);
    handle = AREA_MAKE_HANDLE(entry->generation, area_id);

    /* Initialize local volume index */
    entry->volx = 0;

    /* Determine volume index */
    uint32_t *mother_node = (uint32_t *)((char *)globals + 0x5D0);
    if (*mother_node == 0) {
        /* Local node - use boot volume */
        entry->volx = CAL_$BOOT_VOLX;
    } else if (alloc_remote != 0) {
        /* Remote/diskless node - allocate remote backing */

        /* Calculate total size including overhead */
        int overhead_pages = (virt_size - 1) >> 16;
        if (overhead_pages < 0) {
            overhead_pages = ((virt_size + 0x3FFFE) >> 16);
        }
        total_size = virt_size + ((overhead_pages >> 2) + 1) * COMMIT_SIZE_ALIGN;

        /* Create remote area */
        remote_volx = REM_FILE_$CREATE_AREA(
            AREA_$PARTNER,
            total_size,
            commit_size + (total_size - virt_size),
            entry->caller_id,
            shared,
            &local_volx,
            status_p
        );

        if (*status_p != status_$ok) {
            /* Remote creation failed - delete the local entry */
            AREA_$DELETE(handle, temp_status);
            return 0;
        }

        /* Get packet size if not yet set */
        if (AREA_$PARTNER_PKT_SIZE == 0) {
            AREA_$PARTNER_PKT_SIZE = NETWORK_$GET_PKT_SIZE(AREA_$PARTNER, local_volx);
        }
    }

    /* Store remote volume index */
    entry->remote_volx = remote_volx;

    /* Resize the area to requested size */
    if (virt_size == 0) {
        *status_p = status_$ok;
    } else {
        area_$resize(area_id, entry, virt_size, commit_size, 0, status_p);
    }

    /* Check for resize failure */
    if (*status_p == status_$ok) {
        return handle;
    } else {
        /* Resize failed - delete the area */
        int8_t do_unlink = (remote_uid == 0) ? (int8_t)-1 : 0;
        area_$internal_delete(entry, area_id, temp_status, do_unlink);
        return 0;
    }
}

/*
 * AREA_$CREATE - Create a new area in current address space
 *
 * This is the public API for creating a new local area. It delegates
 * to area_$internal_create with the current process's ASID.
 *
 * Original address: 0x00E079C0
 */
void AREA_$CREATE(uint32_t virt_size, uint32_t commit_size,
                  int8_t shared, status_$t *status_p)
{
    status_$t local_status;
    area_$handle_t handle;

    handle = area_$internal_create(
        virt_size,
        commit_size,
        0,              /* remote_uid = 0 for local */
        PROC1_$AS_ID,   /* current ASID */
        1,              /* alloc_remote = 1 */
        shared,
        &local_status
    );

    *status_p = local_status;

    /* Note: The handle is returned via register D0 in the original code.
     * In C, the caller would need to retrieve it separately or we'd need
     * to change the function signature. For now, we match the original
     * signature which only returns status.
     */
}

/*
 * AREA_$CREATE_FROM - Create area from remote UID (deduplicating)
 *
 * Creates an area backed by a remote UID. If an area already exists
 * with the same UID and caller_id, returns a reference to that area
 * instead of creating a new one (deduplication).
 *
 * Original address: 0x00E07A02
 */
uint16_t AREA_$CREATE_FROM(uint32_t remote_uid, uint32_t virt_size,
                           uint32_t commit_size, int32_t caller_id,
                           int32_t *status_p)
{
    uint16_t area_id;
    area_$uid_hash_t *hash_entry;
    area_$entry_t *entry;
    uint16_t hash_bucket;
    uint32_t *globals = (uint32_t *)AREA_GLOBALS_BASE;

    ML_$LOCK(ML_LOCK_AREA);

    /* Hash the remote UID to find bucket */
    hash_bucket = M_OIU_WLW(remote_uid, UID_HASH_BUCKETS);

    /* Search for existing entry with same UID */
    hash_entry = (area_$uid_hash_t *)((uint32_t *)((char *)globals + 0x454))[hash_bucket];

    while (hash_entry != NULL) {
        entry = hash_entry->first_entry;
        if (entry != NULL && entry->remote_uid == remote_uid) {
            break;
        }
        hash_entry = hash_entry->next;
    }

    /* If found, check for matching caller_id (deduplication) */
    if (hash_entry != NULL) {
        for (entry = hash_entry->first_entry; entry != NULL; entry = entry->next) {
            if ((int32_t)entry->caller_id == caller_id) {
                /* Found existing area - return it */
                area_id = AREA_ENTRY_TO_ID(entry);
                AREA_$CR_DUP++;
                *status_p = status_$ok;
                ML_$UNLOCK(ML_LOCK_AREA);
                return area_id;
            }
        }
    }

    /* No existing area found - create a new one */
    area_$handle_t handle = area_$internal_create(
        virt_size,
        commit_size,
        remote_uid,
        0,              /* owner_asid = 0 for remote */
        0,              /* alloc_remote = 0 */
        0,              /* shared = 0 */
        status_p
    );

    area_$uid_hash_t *pool_entry = *(area_$uid_hash_t **)((char *)globals + 0x450);

    if (*status_p == status_$ok) {
        area_id = AREA_HANDLE_TO_ID(handle);

        /* If no existing hash entry, allocate one from pool */
        if (hash_entry == NULL) {
            if (pool_entry == NULL) {
                /* No pool entries - delete the area and fail */
                int entry_offset = (area_id & 0xFFFF) * AREA_ENTRY_SIZE;
                area_$internal_delete((area_$entry_t *)(AREA_TABLE_BASE + entry_offset - AREA_ENTRY_SIZE),
                                      area_id, status_p, 0);
                *status_p = status_$area_no_uid;
                ML_$UNLOCK(ML_LOCK_AREA);
                return area_id;
            }

            /* Take entry from pool */
            *(area_$uid_hash_t **)((char *)globals + 0x450) = pool_entry->next;

            /* Link into hash bucket */
            pool_entry->next = (area_$uid_hash_t *)((uint32_t *)((char *)globals + 0x454))[hash_bucket];
            ((uint32_t *)((char *)globals + 0x454))[hash_bucket] = (uint32_t)pool_entry;
            pool_entry->first_entry = NULL;

            hash_entry = pool_entry;
        }

        /* Link area entry into hash chain */
        int entry_offset = (area_id & 0xFFFF) * AREA_ENTRY_SIZE;
        entry = (area_$entry_t *)(AREA_TABLE_BASE + entry_offset - AREA_ENTRY_SIZE);

        entry->caller_id = caller_id;

        if (hash_entry->first_entry != NULL) {
            hash_entry->first_entry->prev = entry;
        }
        entry->next = hash_entry->first_entry;
        hash_entry->first_entry = entry;
        entry->prev = NULL;
    }

    area_id = AREA_HANDLE_TO_ID(handle);
    ML_$UNLOCK(ML_LOCK_AREA);
    return area_id;
}

/* Number of UID hash buckets - must match area_internal.h if defined there */
#ifndef UID_HASH_BUCKETS
#define UID_HASH_BUCKETS    11
#endif
