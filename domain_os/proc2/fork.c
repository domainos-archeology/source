/*
 * PROC2_$FORK - Fork current process
 *
 * Creates a child process by forking the current process. The child
 * gets a copy of the parent's address space (or shares it for vfork).
 *
 * This is the largest PROC2 function (~1814 bytes in original) and
 * performs many steps:
 *
 * 1. Allocate process table entry from free list
 * 2. Set up fork/vfork flags
 * 3. Allocate ASID (with special handling for vfork sharing)
 * 4. Initialize process entry
 * 5. Allocate stack and bind process
 * 6. Copy signal masks and other data from parent
 * 7. Set up parent-child links
 * 8. Initialize eventcounts
 * 9. Fork subsystems (FILE, MSG, MST, NAME, etc.)
 * 10. Handle debug inheritance
 * 11. Resume child and wait for fork completion
 *
 * Original address: 0x00e72bce
 */

#include "proc2/proc2_internal.h"
#include "misc/misc.h"
#include "time/time.h"

/* FIM globals for user FIM address */
#if defined(M68K)
    #define FIM_USER_FIM_ADDR_TABLE ((int32_t*)0xE212A8)
    #define FIM_QUIT_INH_TABLE      ((uint8_t*)0xE2248A)
#else
    extern int32_t *fim_user_fim_addr_table;
    extern uint8_t *fim_quit_inh_table;
    #define FIM_USER_FIM_ADDR_TABLE fim_user_fim_addr_table
    #define FIM_QUIT_INH_TABLE      fim_quit_inh_table
#endif

/* Eventcount arrays */
#if defined(M68K)
    #define EC1_FORK_ARRAY_BASE     0xE2B978
#else
    extern void *ec1_fork_array;
    #define EC1_FORK_ARRAY_BASE     ((uintptr_t)ec1_fork_array)
#endif

#define PROC_FORK_EC(idx)       ((void*)(EC1_FORK_ARRAY_BASE + ((idx) - 1) * 0x18))
#define PROC_CR_REC_EC(idx)     ((void*)(EC1_FORK_ARRAY_BASE + ((idx) - 1) * 0x18 + 0x0C))

/* Startup context structure placed on new process stack */
typedef struct startup_context_t {
    void       *self_ptr;
    int32_t     user_data;
    int32_t     entry_point;
    uint16_t    asid;
} startup_context_t;

void PROC2_$FORK(int32_t *entry_point, int32_t *user_data, int32_t *fork_flags,
                 uid_t *uid_ret, uint32_t reserved, uint16_t *upid_ret,
                 void **ec_ret, status_$t *status_ret)
{
    status_$t status;
    status_$t temp_status;
    clock_t creation_time;
    int16_t new_idx;
    int16_t parent_idx;
    uint16_t new_pid;
    void *stack_ptr = NULL;
    uint16_t new_asid;
    proc2_info_t *new_entry;
    proc2_info_t *parent_entry;
    startup_context_t *ctx;
    void *fork_ec;
    void *registered_ec;
    uint16_t min_priority, max_priority;
    int8_t file_locked = 0;
    int i;

    /* Get parent process index */
    parent_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);
    parent_entry = P2_INFO_ENTRY(parent_idx);

    ML_$LOCK(PROC2_LOCK_ID);

    /* Get current time */
    TIME_$CLOCK(&creation_time);

    /* Check for free slot */
    new_idx = P2_FREE_LIST_HEAD;
    if (new_idx == 0) {
        *status_ret = status_$proc2_table_full;
        ML_$UNLOCK(PROC2_LOCK_ID);
        return;
    }

    new_entry = P2_INFO_ENTRY(new_idx);

    /* Remove from free list and add to allocated list */
    P2_FREE_LIST_HEAD = new_entry->next_index;
    new_entry->next_index = P2_INFO_ALLOC_PTR;
    P2_INFO_ALLOC_PTR = new_idx;

    /* Set up prev pointers */
    if (new_entry->next_index != 0) {
        proc2_info_t *next_entry = P2_INFO_ENTRY(new_entry->next_index);
        next_entry->pad_14 = new_idx;
    }
    new_entry->pad_14 = 0;

    /* Clear parent link field */
    new_entry->first_debug_target_idx = 0;

    /* Set flags based on fork_flags */
    /* Bit 3: set if fork_flags == 0 (vfork semantics) */
    if (*fork_flags == 0) {
        new_entry->flags |= 0x08;  /* vfork flag */
    } else {
        new_entry->flags &= ~0x08;
    }

    /* Set bit 4 (guardian notify flag) */
    new_entry->flags |= 0x10;

    /* Clear bit 7 */
    new_entry->flags &= ~0x80;

    /* Copy code descriptor from parent */
    new_entry->cr_rec = parent_entry->cr_rec;

    /* Store user data */
    new_entry->cr_rec_2 = (uint32_t)*user_data;

    /* Allocate ASID */
    new_asid = MST_$ALLOC_ASID(&status);
    new_entry->asid = new_asid;

    if ((status & 0xFFFF) != 0) {
        *status_ret = status | 0x80000000;
        goto cleanup_entry;
    }

    /* Handle alternate ASID for vfork (flag 0x800) */
    if ((new_entry->flags & PROC2_FLAG_ALT_ASID) != 0) {
        /* vfork - share parent's ASID, store new ASID as alternate */
        new_entry->asid_alt = new_asid;
        new_entry->asid = parent_entry->asid;

        /* Copy TTY UID from parent */
        new_entry->tty_uid = parent_entry->tty_uid;
    } else {
        new_entry->asid_alt = 0;
    }

    /* Initialize entry (generates UID, UPID, etc.) */
    PROC2_$INIT_ENTRY_INTERNAL(new_entry);

    /* Allocate stack */
    stack_ptr = PROC1_$ALLOC_STACK(0x1000, &status);

    if ((status & 0xFFFF) != 0) {
        goto cleanup_asid;
    }

    /* Build startup context on stack */
    ctx = (startup_context_t *)((char*)stack_ptr - FIM_$INITIAL_STACK_SIZE - sizeof(startup_context_t));
    ctx->user_data = *user_data;
    ctx->asid = new_entry->asid;
    ctx->entry_point = *entry_point;
    ctx->self_ptr = &ctx->user_data;

    /* Bind the new process */
    new_pid = PROC1_$BIND(PROC2_$STARTUP, ctx, stack_ptr, 0, &status);
    new_entry->level1_pid = new_pid;

    if ((status & 0xFFFF) != 0) {
        goto cleanup_asid;
    }

    /* Mark process as bound */
    new_entry->flags |= 0x01;

    /* Set up PID-to-index mapping */
    P2_PID_TO_INDEX_TABLE[new_pid] = new_idx;

    /* Copy signal masks from parent */
    new_entry->sig_pending = parent_entry->sig_pending;
    new_entry->sig_blocked_1 = parent_entry->sig_blocked_1;
    new_entry->sig_blocked_2 = parent_entry->sig_blocked_2;
    new_entry->sig_mask_1 = parent_entry->sig_mask_1;
    new_entry->sig_mask_2 = parent_entry->sig_mask_2;
    new_entry->sig_mask_3 = parent_entry->sig_mask_3;

    /* Copy flags bit 10 from parent */
    if ((parent_entry->flags & 0x0400) != 0) {
        new_entry->flags |= 0x04;
    } else {
        new_entry->flags &= ~0x04;
    }

    /* Set up child list links */
    new_entry->pad_20[1] = parent_entry->pad_20[0];
    new_entry->pad_20[0] = new_entry->pad_20[1];
    new_entry->pad_18[1] = parent_entry->pad_18[0];
    parent_entry->pad_18[0] = new_idx;
    new_entry->first_debug_target_idx = parent_idx;

    /* Set creation timestamp */
    *(uint32_t*)((char*)new_entry + 0x56) = creation_time.high;

    /* Set bit 3 of high byte of flags */
    *(uint8_t*)((char*)new_entry + 0x2B) |= 0x08;

    /* Copy 32 bytes of data from parent (signal handlers, etc.) */
    for (i = 0; i < 8; i++) {
        ((uint32_t*)((char*)new_entry + 0x2C))[i] =
            ((uint32_t*)((char*)parent_entry + 0x2C))[i];
    }

    /* Copy more fields from parent */
    new_entry->pgroup_uid_idx = parent_entry->pgroup_uid_idx;
    *(uint32_t*)((char*)new_entry + 0x4C) = *(uint32_t*)((char*)parent_entry + 0x4C);
    *(uint32_t*)((char*)new_entry + 0x50) = *(uint32_t*)((char*)parent_entry + 0x50);
    *(uint32_t*)((char*)new_entry + 0x60) = *(uint32_t*)((char*)parent_entry + 0x60);
    *(uint32_t*)((char*)new_entry + 0x64) = *(uint32_t*)((char*)parent_entry + 0x64);

    /* Initialize eventcounts */
    fork_ec = PROC_FORK_EC(new_entry->first_debug_target_idx);
    EC_$INIT((ec_$eventcount_t *)fork_ec);

    /* Set fork EC value to -1 (waiting) */
    *(int32_t*)fork_ec = -1;

    /* Initialize creation record EC */
    EC_$INIT((ec_$eventcount_t *)PROC_CR_REC_EC(new_entry->first_debug_target_idx));

    /* Register the fork EC */
    registered_ec = EC2_$REGISTER_EC1(fork_ec, &status);

    if ((status & 0xFFFF) != 0) {
        goto cleanup_asid;
    }

    /* Success path - unlock PROC2 */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* Return child UID */
    uid_ret->high = new_entry->uid.high;
    uid_ret->low = new_entry->uid.low;

    /* Return child UPID */
    *upid_ret = new_entry->upid;

    /* Return EC handle */
    *ec_ret = registered_ec;

    /* Initialize ACL for new process */
    ACL_$ALLOC_ASID(new_pid, &status);

    /* Inherit audit settings */
    AUDIT_$INHERIT_AUDIT((int16_t*)&new_entry->level1_pid, (int16_t*)&status);

    /* Copy FIM user address from parent */
    {
        int16_t parent_asid_idx = PROC1_$AS_ID << 2;
        int16_t child_asid_idx = new_entry->asid << 2;
        int32_t fim_addr = FIM_USER_FIM_ADDR_TABLE[parent_asid_idx / 4];
        FIM_USER_FIM_ADDR_TABLE[child_asid_idx / 4] = fim_addr;
        if (fim_addr != 0) {
            FIM_QUIT_INH_TABLE[new_entry->asid] = 0;
        }
    }

    /* Check if this is vfork (alternate ASID mode) */
    if ((new_entry->flags & PROC2_FLAG_ALT_ASID) != 0) {
        /* vfork - skip file/MST fork, go directly to priority setup */
        goto set_priority;
    }

    /* For normal fork (not init process), lock files */
    if (PROC1_$CURRENT != 1) {
        FILE_$FORK_LOCK((void*)&new_entry->asid, &status);
        if (status != status_$ok) {
            goto late_cleanup;
        }
        file_locked = 1;
    }

    /* Handle message fork if parent has MSG flag */
    if ((int8_t)(*(uint8_t*)((char*)parent_entry + 0x9D)) < 0) {
        if (MSG_$FORK(0x060A, (void*)&new_entry->asid) < 0) {
            *(uint8_t*)((char*)new_entry + 0x9D) |= 0x80;
        }
    }

    /* Fork address space */
    MST_$FORK(new_entry->asid, new_entry->level1_pid, *fork_flags, &status);

    if ((status & 0xFFFF) != 0) {
        goto late_cleanup;
    }

    /* Get VA info for code area */
    MST_$GET_VA_INFO(&new_entry->asid, &new_entry->cr_rec,
                     (void*)((char*)new_entry + 0x08), NULL, NULL, NULL, NULL, &status);

    if ((status & 0xFFFF) != 0) {
        goto late_cleanup;
    }

    /* Get VA info for stack area */
    {
        int32_t stack_end = new_entry->cr_rec_2 - 1;
        MST_$GET_VA_INFO(&new_entry->asid, &stack_end,
                         &new_entry->tty_uid, NULL, NULL, NULL, NULL, &status);
    }

    if ((status & 0xFFFF) != 0) {
        goto late_cleanup;
    }

    /* Fork naming */
    NAME_$FORK(&PROC1_$AS_ID, (int16_t*)&new_entry->asid);

    /* Handle profiling fork if parent has profiling flag */
    if ((parent_entry->cleanup_flags & 0x0800) != 0) {
        PCHIST_$UNIX_PROFIL_FORK((void*)&new_entry->level1_pid, (void*)&new_entry->asid);
        *(uint8_t*)((char*)new_entry + 0x9C) |= 0x08;
    }

set_priority:
    /* Set priority */
    if (PROC1_$CURRENT == 1) {
        min_priority = 3;
        max_priority = 14;
    } else {
        PROC1_$SET_PRIORITY(PROC1_$CURRENT, 0, &min_priority, &max_priority);
    }

    /* Handle debug inheritance */
    if (parent_entry->debugger_idx != 0) {
        if (XPD_$INHERIT_PTRACE_OPTIONS((int16_t)((char*)parent_entry -
                                        (char*)P2_INFO_ENTRY(0) + 0xCE)) < 0) {
            ML_$LOCK(PROC2_LOCK_ID);

            DEBUG_SETUP_INTERNAL(new_entry->first_debug_target_idx,
                                 parent_entry->debugger_idx, 0);

            /* Copy ptrace options from parent */
            *(uint32_t*)((char*)new_entry + 0xCE) = *(uint32_t*)((char*)parent_entry + 0xCE);
            *(uint32_t*)((char*)new_entry + 0xD2) = *(uint32_t*)((char*)parent_entry + 0xD2);
            *(uint32_t*)((char*)new_entry + 0xD6) = *(uint32_t*)((char*)parent_entry + 0xD6);
            *(uint16_t*)((char*)new_entry + 0xDA) = *(uint16_t*)((char*)parent_entry + 0xDA);

            ML_$UNLOCK(PROC2_LOCK_ID);
        }
    }

    /* Set child priority */
    PROC1_$SET_PRIORITY(new_entry->level1_pid, 0xFF00, &min_priority, &max_priority);

    /* Read fork EC value and compute wait value */
    {
        int32_t ec_val = EC_$READ((ec_$eventcount_t *)fork_ec) + 1;
        ec_$eventcount_t *ec_list[1] = { (ec_$eventcount_t *)fork_ec };
        int32_t val_list[1] = { ec_val };

        /* Set process type */
        PROC1_$SET_TYPE(new_entry->level1_pid, 2);

        /* Resume child process */
        PROC1_$RESUME(new_entry->level1_pid, &status);
        if ((status & 0xFFFF) != 0) {
            CRASH_SYSTEM(&status);
        }

        /* Wait for child to complete fork */
        EC_$WAITN(ec_list, val_list, 1);
    }

    /* Check if child completed successfully (high bit of flags byte) */
    if ((int8_t)(*(uint8_t*)((char*)new_entry + 0x2B)) >= 0) {
        *ec_ret = NULL;  /* Child failed */
    }

    *status_ret = status;
    return;

late_cleanup:
    /* Failure after unlock - reacquire lock if needed */
    if (PROC1_$TST_LOCK(PROC2_LOCK_ID) >= 0) {
        ML_$LOCK(PROC2_LOCK_ID);
    }

cleanup_asid:
    /* Clean up parent link if set */
    if (new_entry->first_debug_target_idx != 0) {
        proc2_info_t *parent = P2_INFO_ENTRY(new_entry->first_debug_target_idx);
        parent->pad_18[0] = new_entry->pad_18[1];
    }

    /* Unbind or free stack */
    if ((new_entry->flags & 0x0100) != 0) {
        PROC1_$UNBIND(new_entry->level1_pid, &temp_status);
    } else if (stack_ptr != NULL) {
        PROC1_$FREE_STACK(stack_ptr);
    }

    /* Set error flag if not a PROC2 error */
    if ((status >> 16) != 0x19) {
        status |= 0x80000000;
    }

    /* Unlock files if locked */
    if (file_locked) {
        FILE_$PRIV_UNLOCK_ALL((void*)&new_entry->asid);
    }

    /* Free ASID (different handling for vfork vs fork) */
    if ((new_entry->flags & PROC2_FLAG_ALT_ASID) != 0) {
        /* vfork - free alternate ASID, restore parent UID */
        MST_$FREE_ASID(new_entry->asid_alt, &temp_status);
        {
            uintptr_t uid_addr = (uintptr_t)0xE7BE94 + (new_entry->asid << 3);
            *(uint32_t*)uid_addr = parent_entry->uid.high;
            *(uint32_t*)(uid_addr + 4) = parent_entry->uid.low;
        }
    } else {
        /* Normal fork - free ASID, restore system UID */
        MST_$FREE_ASID(new_entry->asid, &temp_status);
        {
            uintptr_t uid_addr = (uintptr_t)0xE7BE94 + (new_entry->asid << 3);
            *(uint32_t*)uid_addr = PROC2_UID.high;
            *(uint32_t*)(uid_addr + 4) = PROC2_UID.low;
        }
    }

    /* Call cleanup handlers if any */
    if (new_entry->cleanup_flags != 0) {
        PROC2_$CLEANUP_HANDLERS_INTERNAL(new_entry);
    }

cleanup_entry:
    /* Clean up process group */
    PGROUP_CLEANUP_INTERNAL(new_entry, 2);

    /* Remove from allocated list and return to free list */
    if (new_entry->pad_14 == 0) {
        P2_INFO_ALLOC_PTR = new_entry->next_index;
    } else {
        proc2_info_t *prev = P2_INFO_ENTRY(new_entry->pad_14);
        prev->next_index = new_entry->next_index;
    }

    if (new_entry->next_index != 0) {
        proc2_info_t *next = P2_INFO_ENTRY(new_entry->next_index);
        next->pad_14 = new_entry->pad_14;
    }

    /* Add back to free list */
    new_entry->next_index = P2_FREE_LIST_HEAD;
    P2_FREE_LIST_HEAD = new_idx;

    /* Clear UID and bound flag */
    *(uint32_t*)((char*)new_entry + 0x08) = UID_$NIL.high;
    *(uint32_t*)((char*)new_entry + 0x0C) = UID_$NIL.low;
    new_entry->flags &= ~0x01;

    /* Set UID from global */
    new_entry->uid = PROC2_UID;

    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}
