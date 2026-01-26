/*
 * PROC2_$CREATE - Create a new process
 *
 * Creates a new process with the specified parameters. This is the main
 * process creation function in Domain/OS.
 *
 * Steps:
 * 1. Lock PROC2 and get current time
 * 2. Allocate a process table entry from free list
 * 3. Allocate an address space ID (ASID)
 * 4. Initialize floating point state
 * 5. Initialize process entry (generate UID, UPID, etc.)
 * 6. Map initial memory area
 * 7. Allocate stack
 * 8. Bind process via PROC1
 * 9. Set up process group and parent relationships
 * 10. Handle debug inheritance
 * 11. Initialize eventcounts
 * 12. Initialize ACL, audit, and naming subsystems
 * 13. Set priority and process type
 *
 * On error, all resources are cleaned up and entry returned to free list.
 *
 * Original address: 0x00e726ec
 */

#include "proc2/proc2_internal.h"
#include "time/time.h"

/* Eventcount arrays - base addresses for process eventcounts */
#if defined(M68K)
    #define EC1_FORK_ARRAY_BASE     0xE2B978
    #define EC1_CR_REC_OFFSET       0x0C     /* Offset from fork EC to creation record EC */
#else
    extern void *ec1_fork_array;
    #define EC1_FORK_ARRAY_BASE     ((uintptr_t)ec1_fork_array)
    #define EC1_CR_REC_OFFSET       0x0C
#endif

/* Process fork EC: index * 0x18 + base - 0x18 */
#define PROC_FORK_EC(idx)       ((void*)(EC1_FORK_ARRAY_BASE + ((idx) - 1) * 0x18))
#define PROC_CR_REC_EC(idx)     ((void*)(EC1_FORK_ARRAY_BASE + ((idx) - 1) * 0x18 + EC1_CR_REC_OFFSET))

/* Startup context structure placed on new process stack */
typedef struct startup_context_t {
    void       *self_ptr;       /* 0x00: Pointer to context+4 (for stack frame) */
    int32_t     user_data;      /* 0x04: User data passed to startup */
    int32_t     entry_point;    /* 0x08: Entry point address */
    uint16_t    asid;           /* 0x0C: ASID for new process */
} startup_context_t;

void PROC2_$CREATE(uid_t *parent_uid, uint32_t *code_desc, uint32_t *map_param,
                   int32_t *entry_point, int32_t *user_data,
                   uint32_t reserved1, uint32_t reserved2,
                   uint8_t *flags, uid_t *uid_ret, void **ec_ret,
                   status_$t *status_ret)
{
    uid_t local_parent_uid;
    uint32_t local_code_desc;
    uint32_t local_map_param;
    int32_t local_entry_point;
    int32_t local_user_data;
    uint8_t local_flags;
    status_$t status;
    status_$t temp_status;
    clock_t creation_time;
    int16_t new_idx;
    int16_t current_idx;
    uint16_t new_pid;
    void *stack_ptr = NULL;
    uint16_t new_asid;
    proc2_info_t *new_entry;
    proc2_info_t *current_entry;
    startup_context_t *ctx;
    void *fork_ec;
    void *registered_ec;
    uint16_t min_priority, max_priority;

    /* Copy input parameters to locals */
    local_parent_uid.high = parent_uid->high;
    local_parent_uid.low = parent_uid->low;
    local_code_desc = *code_desc;
    local_map_param = *map_param;
    local_entry_point = *entry_point;
    local_user_data = *user_data;
    local_flags = *flags;

    ML_$LOCK(PROC2_LOCK_ID);

    /* Get current time for creation timestamp */
    TIME_$CLOCK(&creation_time);

    /* Check for free slot */
    new_idx = P2_FREE_LIST_HEAD;
    if (new_idx == 0) {
        *status_ret = status_$proc2_table_full;
        ML_$UNLOCK(PROC2_LOCK_ID);
        return;
    }

    new_entry = P2_INFO_ENTRY(new_idx);
    current_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);
    current_entry = P2_INFO_ENTRY(current_idx);

    /* Remove from free list and add to allocated list */
    P2_FREE_LIST_HEAD = new_entry->next_index;
    new_entry->next_index = P2_INFO_ALLOC_PTR;
    P2_INFO_ALLOC_PTR = new_idx;

    /* Set up prev pointers */
    if (new_entry->next_index != 0) {
        proc2_info_t *next_entry = P2_INFO_ENTRY(new_entry->next_index);
        next_entry->pad_14 = new_idx;  /* prev pointer */
    }
    new_entry->pad_14 = 0;  /* No prev (we're at head) */

    /* Clear parent link field */
    new_entry->first_debug_target_idx = 0;

    /* Allocate ASID */
    new_asid = MST_$ALLOC_ASID(&status);
    new_entry->asid = new_asid;

    if ((status & 0xFFFF) != 0) {
        /* ASID allocation failed */
        *status_ret = status | 0x80000000;
        goto cleanup_entry;
    }

    /* Initialize floating point state */
    FIM_$FP_INIT(new_asid);

    /* Set server flag from input flags */
    new_entry->flags = (new_entry->flags & 0x7F00) | (local_flags & 0x80);

    /* Initialize entry (generates UID, UPID, etc.) */
    PROC2_$INIT_ENTRY_INTERNAL(new_entry);

    /* Store parent UID */
    *(uint32_t*)((char*)new_entry + 0x08) = local_parent_uid.high;
    *(uint32_t*)((char*)new_entry + 0x0C) = local_parent_uid.low;

    /* Store code descriptor and user data */
    new_entry->cr_rec = (uint32_t)local_code_desc;
    new_entry->cr_rec_2 = (uint32_t)local_user_data;

    /* Map initial memory area */
    MST_$MAP_INITIAL_AREA(local_code_desc, new_asid, &local_parent_uid,
                          local_map_param, 0x70000, &status);

    if ((status & 0xFFFF) != 0) {
        goto cleanup_asid;
    }

    /* Set TTY UID to nil initially */
    new_entry->tty_uid = UID_$NIL;

    /* Allocate stack */
    stack_ptr = PROC1_$ALLOC_STACK(0x1000, &status);

    if ((status & 0xFFFF) != 0) {
        goto cleanup_asid;
    }

    /* Build startup context on stack */
    ctx = (startup_context_t *)((char*)stack_ptr - FIM_$INITIAL_STACK_SIZE - sizeof(startup_context_t));
    ctx->user_data = local_user_data;
    ctx->asid = new_asid;
    ctx->entry_point = local_entry_point;
    ctx->self_ptr = &ctx->user_data;

    /* Bind the new process */
    new_pid = PROC1_$BIND(PROC2_$STARTUP, ctx, stack_ptr, 0, &status);
    new_entry->level1_pid = new_pid;

    if ((status & 0xFFFF) != 0) {
        goto cleanup_asid;
    }

    /* Mark process as bound (bit 0 of flags byte) */
    new_entry->flags |= 0x01;

    /* Set up PID to index mapping */
    P2_PID_TO_INDEX_TABLE[new_pid] = new_idx;

    /* Clear signal masks */
    new_entry->sig_pending = 0;
    new_entry->sig_blocked_1 = 0;
    new_entry->sig_blocked_2 = 0;
    new_entry->sig_mask_1 = 0;
    new_entry->sig_mask_2 = 0;

    /* Clear various flags */
    new_entry->flags &= 0xE3FB;  /* Clear certain bits */

    /* Set up child list links
     * first_child_idx = 0 (no children yet)
     * next_child_sibling = parent's first_child_idx (insert at head of parent's child list) */
    new_entry->first_child_idx = 0;
    new_entry->next_child_sibling = current_entry->first_child_idx;

    /* Clear bit 3 of flags byte at 0x2B */
    /* new_entry->flags &= ~0x0008; -- already done above */

    /* Copy parent UID to another location */
    *(uint32_t*)((char*)new_entry + 0x4C) = local_parent_uid.high;
    *(uint32_t*)((char*)new_entry + 0x50) = local_parent_uid.low;

    /* Set creation timestamp */
    new_entry->pgroup_uid_idx = 0;
    *(uint32_t*)((char*)new_entry + 0x56) = creation_time.high;

    /* Copy accounting info from parent */
    *(uint32_t*)((char*)new_entry + 0x60) = *(uint32_t*)((char*)current_entry + 0x60);
    *(uint32_t*)((char*)new_entry + 0x64) = *(uint32_t*)((char*)current_entry + 0x64);

    /* Set up process group relationship */
    if ((int8_t)local_flags < 0) {
        /* Create in new process group */
        new_entry->first_debug_target_idx = 0;
        new_entry->pad_18[1] = 0;
    } else {
        /* Inherit parent's process group */
        new_entry->first_debug_target_idx = current_entry->first_debug_target_idx;
        new_entry->pad_18[1] = current_entry->pad_18[0];
        current_entry->pad_18[0] = new_idx;
    }

    /* Handle debug inheritance */
    if (current_entry->debugger_idx != 0) {
        /* Parent is being debugged - check if child should inherit */
        if (XPD_$INHERIT_PTRACE_OPTIONS((int16_t)((char*)current_entry -
                                        (char*)P2_INFO_ENTRY(0) + 0xCE)) < 0) {
            /* Set up debug relationship for child */
            DEBUG_SETUP_INTERNAL(new_entry->first_debug_target_idx,
                                 current_entry->debugger_idx, 0);

            /* Copy ptrace options from parent */
            *(uint32_t*)((char*)new_entry + 0xCE) = *(uint32_t*)((char*)current_entry + 0xCE);
            *(uint32_t*)((char*)new_entry + 0xD2) = *(uint32_t*)((char*)current_entry + 0xD2);
            *(uint32_t*)((char*)new_entry + 0xD6) = *(uint32_t*)((char*)current_entry + 0xD6);
            *(uint16_t*)((char*)new_entry + 0xDA) = *(uint16_t*)((char*)current_entry + 0xDA);
        }
    }

    /* Initialize eventcounts for the new process */
    fork_ec = PROC_FORK_EC(new_entry->first_debug_target_idx);
    EC_$INIT(fork_ec);
    EC_$INIT((void*)((char*)fork_ec + EC1_CR_REC_OFFSET));

    /* Register the fork EC */
    registered_ec = EC2_$REGISTER_EC1(fork_ec, &status);
    *ec_ret = registered_ec;

    if ((status & 0xFFFF) != 0) {
        goto cleanup_asid;
    }

    /* Success path - unlock and continue initialization */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* Return new process UID */
    uid_ret->high = new_entry->uid.high;
    uid_ret->low = new_entry->uid.low;

    /* Initialize ACL for new process */
    ACL_$ALLOC_ASID(new_pid, &status);

    /* Inherit audit settings */
    AUDIT_$INHERIT_AUDIT((int16_t*)&new_entry->level1_pid, (int16_t*)&status);

    /* Initialize naming for new ASID */
    NAME_$INIT_ASID((int16_t*)&new_entry->asid, (int16_t*)&status);

    if ((status & 0xFFFF) != 0) {
        /* Late failure - need to clean up with lock */
        goto late_cleanup;
    }

    /* Set priority */
    if (PROC1_$CURRENT == 1) {
        /* Init process - use defaults */
        min_priority = 3;
        max_priority = 14;
    } else {
        /* Get current process priority */
        uint8_t priority_arg = 0;
        PROC1_$SET_PRIORITY(PROC1_$CURRENT, 0, &min_priority, &max_priority);
        priority_arg = 10;
    }

    /* Set new process priority */
    PROC1_$SET_PRIORITY(new_pid, 0xFF0A, &min_priority, &max_priority);

    /* Set process type to 2 (user process) */
    PROC1_$SET_TYPE(new_pid, 2);

    *status_ret = status;
    return;

late_cleanup:
    /* Failure after unlock - reacquire lock */
    if (PROC1_$TST_LOCK(PROC2_LOCK_ID) >= 0) {
        ML_$LOCK(PROC2_LOCK_ID);
    }
    /* Fall through to cleanup_asid */

cleanup_asid:
    /* Clean up parent link if set */
    if (new_entry->first_debug_target_idx != 0) {
        proc2_info_t *parent = P2_INFO_ENTRY(new_entry->first_debug_target_idx);
        parent->pad_18[0] = new_entry->pad_18[1];
    }

    /* Unbind or free stack */
    if ((new_entry->flags & 0x0100) != 0) {
        /* Process was bound - unbind it */
        PROC1_$UNBIND(new_entry->level1_pid, &temp_status);
    } else if (stack_ptr != NULL) {
        /* Stack was allocated but process not bound - free stack */
        PROC1_$FREE_STACK(stack_ptr);
    }

    /* Set error flag if not a PROC2 error */
    if ((status >> 16) != 0x19) {
        status |= 0x80000000;
    }

    /* Free ASID */
    MST_$FREE_ASID(new_entry->asid, &temp_status);

    /* Clear UID in global table */
    /* TODO: UID table cleanup */

    /* Call cleanup handlers if any were registered */
    if (new_entry->level1_pid != 0) {
        PROC2_$CLEANUP_HANDLERS_INTERNAL(new_entry);
    }

cleanup_entry:
    /* Clean up process group */
    PGROUP_CLEANUP_INTERNAL(new_entry, 2);

    /* Remove from allocated list and return to free list */
    if (new_entry->pad_14 == 0) {
        /* At head of allocated list */
        P2_INFO_ALLOC_PTR = new_entry->next_index;
    } else {
        /* In middle of list */
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

    /* Clear UID */
    new_entry->uid = UID_$NIL;

    /* Clear bound flag */
    new_entry->flags &= ~0x01;

    /* Set UID from global */
    new_entry->uid = PROC2_UID;

    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}
