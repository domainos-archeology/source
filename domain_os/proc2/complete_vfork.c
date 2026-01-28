/*
 * PROC2_$COMPLETE_VFORK - Complete vfork in child
 *
 * Called by the child process after vfork to separate from the parent's
 * address space. The child has been running in the parent's address space
 * (sharing ASID), and this function:
 *
 * 1. Verifies the process was vforked (has ALT_ASID flag)
 * 2. Swaps ASIDs - child takes alternate ASID, parent keeps original
 * 3. Updates UID tables for both ASIDs
 * 4. Initializes floating point state for new ASID
 * 5. Copies user FIM address table entries
 * 6. Maps initial memory area for child's new address space
 * 7. Initializes naming subsystem for new ASID
 * 8. Advances fork eventcount to wake parent
 * 9. Switches to new ASID
 * 10. Maps stack area
 * 11. Calls startup to begin execution at entry point
 *
 * On failure at any step, calls PROC2_$DELETE to terminate.
 *
 * Original address: 0x00e73638
 */

#include "proc2/proc2_internal.h"

/* Global storage addresses */
#if defined(ARCH_M68K)
    #define AS_$STACK_FILE_LOW          (*(uint32_t*)0xE2B92C)
    #define AS_$INIT_STACK_FILE_SIZE    (*(uint32_t*)0xE2B960)
    #define FIM_USER_FIM_ADDR_TABLE     ((int32_t*)0xE212A8)
    #define FIM_QUIT_INH_TABLE          ((uint8_t*)0xE2248A)
    #define UID_TABLE_BASE              0xE7BE94
#else
    extern uint32_t as_stack_file_low;
    extern uint32_t as_init_stack_file_size;
    extern int32_t *fim_user_fim_addr_table;
    extern uint8_t *fim_quit_inh_table;
    extern uid_t uid_table[];
    #define AS_$STACK_FILE_LOW          as_stack_file_low
    #define AS_$INIT_STACK_FILE_SIZE    as_init_stack_file_size
    #define FIM_USER_FIM_ADDR_TABLE     fim_user_fim_addr_table
    #define FIM_QUIT_INH_TABLE          fim_quit_inh_table
    #define UID_TABLE_BASE              ((uintptr_t)uid_table)
#endif

/* Eventcount array base */
#if defined(ARCH_M68K)
    #define EC1_FORK_ARRAY_BASE         0xE2B978
#else
    extern void *ec1_fork_array;
    #define EC1_FORK_ARRAY_BASE         ((uintptr_t)ec1_fork_array)
#endif

#define PROC_FORK_EC(idx)       ((ec_$eventcount_t*)(EC1_FORK_ARRAY_BASE + ((idx) - 1) * 0x18))

/*
 * Creation record structure for startup
 * Located in the process entry at offset 0x6C (cr_rec_2)
 */
typedef struct cr_rec_t {
    /* Fields at various offsets used by startup */
    uint8_t     pad_00[0x94];
    status_$t   status;         /* 0x94: Status from map operation */
    uint8_t     pad_98[0x10];
    uid_t      cr_uid;         /* 0xA8: Creation record UID */
    uint32_t    stack_low;      /* 0xB0: Stack file low address */
    uint32_t    stack_size;     /* 0xB4: Stack file size */
} cr_rec_t;

void PROC2_$COMPLETE_VFORK(uid_t *proc_uid, uint32_t *code_desc, uint32_t *map_param,
                           int32_t *entry_point, int32_t *user_data,
                           uint32_t reserved1, uint32_t reserved2,
                           status_$t *status_ret)
{
    uid_t local_uid;
    uint32_t local_code_desc;
    uint32_t local_map_param;
    int32_t local_entry_point;
    int32_t local_user_data;
    status_$t status;
    int16_t current_idx;
    proc2_info_t *current_entry;
    uint16_t old_asid;
    uint16_t new_asid;
    int16_t parent_idx;
    proc2_info_t *parent_entry;
    int32_t user_fim_addr;
    cr_rec_t *cr_rec;

    /* Copy input parameters to locals */
    local_uid.high = proc_uid->high;
    local_uid.low = proc_uid->low;
    local_code_desc = *code_desc;
    local_map_param = *map_param;
    local_entry_point = *entry_point;
    local_user_data = *user_data;

    ML_$LOCK(PROC2_LOCK_ID);

    /* Get current process entry */
    current_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);
    current_entry = P2_INFO_ENTRY(current_idx);

    /* Verify process was vforked (has ALT_ASID flag) */
    if ((current_entry->flags & PROC2_FLAG_ALT_ASID) == 0) {
        *status_ret = status_$proc2_process_wasnt_vforked;
        ML_$UNLOCK(PROC2_LOCK_ID);
        return;
    }

    /*
     * Swap ASIDs: child takes alternate ASID, parent keeps original.
     * old_asid = current asid (will go back to parent)
     * new_asid = alternate asid (child will use)
     */
    old_asid = current_entry->asid;
    new_asid = current_entry->asid_alt;
    current_entry->asid = new_asid;
    current_entry->asid_alt = 0;

    /* Store user data in creation record field */
    current_entry->cr_rec_2 = local_user_data;

    /* Update UID in entry (stored at offset 0x08 in original code) */
    *(uint32_t*)((char*)current_entry + 0x08) = local_uid.high;
    *(uint32_t*)((char*)current_entry + 0x0C) = local_uid.low;

    /* Clear flag bit 3 (0x08) */
    current_entry->flags &= ~0x0008;

    /* Clear pad_18[0] (used for sibling list) */
    current_entry->pad_18[0] = 0;

    /*
     * Update UID table for child's new ASID.
     * Copy child's UID to the UID table slot for new_asid.
     */
    {
        uintptr_t uid_addr = UID_TABLE_BASE + (new_asid << 3);
        *(uint32_t*)uid_addr = current_entry->uid.high;
        *(uint32_t*)(uid_addr + 4) = current_entry->uid.low;
    }

    /*
     * Update UID table for parent's ASID.
     * Copy parent's UID to the UID table slot for old_asid.
     */
    parent_idx = current_entry->parent_pgroup_idx;
    parent_entry = P2_INFO_ENTRY(parent_idx);
    {
        uintptr_t uid_addr = UID_TABLE_BASE + (old_asid << 3);
        *(uint32_t*)uid_addr = parent_entry->uid.high;
        *(uint32_t*)(uid_addr + 4) = parent_entry->uid.low;
    }

    /* Initialize floating point state for new ASID */
    FIM_$FP_INIT(new_asid);

    /*
     * Copy user FIM address from old ASID slot to new ASID slot.
     * This preserves any user-defined FIM handler.
     */
    user_fim_addr = FIM_USER_FIM_ADDR_TABLE[old_asid];
    FIM_USER_FIM_ADDR_TABLE[new_asid] = user_fim_addr;
    if (user_fim_addr != 0) {
        FIM_QUIT_INH_TABLE[new_asid] = 0;
    }

    /* Map initial memory area for child's new address space */
    MST_$MAP_INITIAL_AREA(local_code_desc, new_asid, &local_uid,
                          local_map_param, 0x7FF00E7, &status);

    if ((status & 0xFFFF) != 0) {
        goto error_cleanup;
    }

    /* Set TTY UID to nil (child starts with no controlling terminal) */
    /* Stored at offset 0xDC in the structure (within pad_bf) */
    *(uint32_t*)((char*)current_entry + 0xDC) = UID_$NIL.high;
    *(uint32_t*)((char*)current_entry + 0xE0) = UID_$NIL.low;

    /* Initialize naming subsystem for new ASID */
    NAME_$INIT_ASID((int16_t*)&current_entry->asid, &status);

    if ((status & 0xFFFF) != 0) {
        goto error_cleanup;
    }

    /*
     * Advance fork eventcount to wake parent.
     * Parent is waiting on this EC in PROC2_$FORK.
     */
    EC_$ADVANCE(PROC_FORK_EC(current_entry->owner_session));

    /* Switch to new ASID */
    PROC1_$SET_ASID(new_asid);

    ML_$UNLOCK(PROC2_LOCK_ID);

    /*
     * Map stack area for the new address space.
     * The creation record (cr_rec_2) contains the stack parameters.
     */
    cr_rec = (cr_rec_t*)current_entry->cr_rec_2;
    cr_rec->stack_low = AS_$STACK_FILE_LOW;
    cr_rec->stack_size = AS_$INIT_STACK_FILE_SIZE;

    /* Map the stack area */
    MST_$MAP_AREA_AT(&cr_rec->stack_low, &cr_rec->stack_size,
                     (void*)0x00e735f4, (void*)0x00e73860,  /* Placeholder addresses from original */
                     (void*)((char*)current_entry + 0xDC),
                     &cr_rec->status);

    /* Copy mapped UID to creation record */
    cr_rec->cr_uid.high = *(uint32_t*)((char*)current_entry + 0xDC);
    cr_rec->cr_uid.low = *(uint32_t*)((char*)current_entry + 0xE0);

    /* Check if stack mapping failed */
    if (cr_rec->status != status_$ok) {
        PROC2_$DELETE();
        /* Does not return */
    }

    /*
     * Call FIM startup to begin execution at entry point.
     * This does not return - it jumps to the new process's entry point.
     */
    {
        struct {
            int32_t user_data;
            int32_t entry_point;
        } startup_context;
        startup_context.user_data = local_user_data;
        startup_context.entry_point = local_entry_point;
        FIM_$PROC2_STARTUP(&startup_context);
    }

    /* Should not reach here */
    return;

error_cleanup:
    ML_$UNLOCK(PROC2_LOCK_ID);
    PROC2_$DELETE();
    /* Does not return */
}
