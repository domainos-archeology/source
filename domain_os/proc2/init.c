/*
 * PROC2_$INIT - Initialize PROC2 subsystem
 *
 * This is called during system boot to initialize the PROC2 high-level
 * process management subsystem. It sets up:
 *
 * 1. Global UIDs for the system
 * 2. PID-to-index mapping table (cleared)
 * 3. Process group table (cleared)
 * 4. Free list of process table entries (entries 2-69)
 * 5. Entry 1 as the init/system process
 * 6. Memory mappings for creation records and initial stack
 * 7. Boot device initialization (tape/floppy if requested)
 * 8. Resolution and mapping of boot shell
 *
 * Parameters:
 *   boot_flags - Boot option flags (bit 0 = tape boot, bit 1 = floppy boot)
 *   status_ret - Pointer to receive status
 *
 * Returns:
 *   Status code (also stored in status_ret)
 *
 * Original address: 0x00e303d8
 */

#include "proc2/proc2_internal.h"

/* Number of process table entries (indices 1-69, 0 unused) */
#define P2_MAX_ENTRIES          70
#define P2_FIRST_FREE_ENTRY     2
#define P2_LAST_ENTRY           69

/*
 * Globals used here (declared in subsystem headers):
 *   proc2_boot_flags (0xE7C068), proc2_proc_dir_uid (0xE7BE84),
 *   proc2_system_uid (0xE7BE8C), PROC2_UID table (0xE7BE94),
 *   PROC2_$EC / PROC_FORK_EC / PROC_CR_REC_EC (0xE2B978)  - proc2 headers
 *   AS_$CR_REC (0xE2B930), AS_$CR_REC_FILE_SIZE (0xE2B96C),
 *   AS_$STACK_FILE_LOW (0xE2B92C), AS_$INIT_STACK_FILE_SIZE (0xE2B960),
 *   AS_$STACK_HIGH (0xE2B950)                              - as/as.h
 */

/* Boot file parameters - these would be in read-only data */
static const char boot_shell_path[] = "/sys/boot_shell";
static const char proc_dir_path[] = "/node_data/proc_dir";

/* Error message strings for boot error checking */
static const char msg_unable_to_map[] = "unable to map ";
static const char msg_unable_to_resolve[] = "unable to resolve ";
static const char msg_unable_to_lock[] = "unable to lock ";
static const char msg_unable_to_unmap[] = "unable to unmap ";
static const char msg_creation_record_area[] = "creation record area";
static const char msg_initial_area[] = "initial area";

/*
 * boot_flags points at the caller's 16-bit boot option word (OS_$INIT pushes
 * the address of its local ws_mode: pea (-0x1ca,A6)).  Bits 0 and 1 of the
 * low byte select tape / floppy boot (btst.b #0/#1,(0x1,A1)).
 */
status_$t PROC2_$INIT(uint16_t *boot_flags, status_$t *status_ret)
{
    status_$t status;
    int16_t i;
    proc2_info_t *entry;
    proc2_info_t *init_entry;
    uint16_t min_pri, max_pri;
    uint8_t mmu_mode;
    uid_t boot_shell_uid;
    int16_t path_len;

    /*
     * Step 1: Generate system UIDs
     *   DAT_00e7be8c - system process UID (proc2_system_uid)
     *   DAT_00e7be9c - PROC2_UID[1]
     */
    UID_$GEN(&proc2_system_uid);
    UID_$GEN(&PROC2_UID[1]);

    /*
     * Step 2: Set priority for init process
     */
    min_pri = 0x10;
    max_pri = 0x10;
    PROC1_$SET_PRIORITY(PROC1_$CURRENT, 0xFF00, &min_pri, &max_pri);

    /*
     * Step 3: Initialize UID table with the system UID.
     * Entry 0 is stored directly, then the loop (dbf with count 0x37)
     * fills entries 2..57, skipping entry 1 generated above.
     */
    PROC2_UID[0] = proc2_system_uid;
    for (i = 2; i <= 57; i++) {
        PROC2_UID[i] = proc2_system_uid;
    }

    /*
     * Step 4: Clear PID-to-index mapping table
     * (63 entries)
     */
    for (i = 0; i < 63; i++) {
        P2_PID_TO_INDEX_TABLE[i] = 0;
    }

    /*
     * Step 5: Clear process group table
     * (70 entries)
     */
    for (i = 0; i < PGROUP_TABLE_SIZE; i++) {
        pgroup_entry_t *pg = PGROUP_ENTRY(i);
        pg->ref_count = 0;
    }

    /*
     * Step 6: Initialize free list (entries 2-69)
     * Each entry points to next, with UID_NIL and cleared flags
     */
    P2_FREE_LIST_HEAD = P2_FIRST_FREE_ENTRY;

    for (i = P2_FIRST_FREE_ENTRY; i <= P2_LAST_ENTRY; i++) {
        entry = P2_INFO_ENTRY(i);

        /* Link to next entry (or 0 for last) */
        entry->next_index = (i < P2_LAST_ENTRY) ? (i + 1) : 0;

        /* Set parent UID to nil */
        entry->parent_uid.high = UID_$NIL.high;
        entry->parent_uid.low = UID_$NIL.low;

        /* Clear valid/bound flags */
        entry->flags &= ~(PROC2_FLAG_VALID | 0x01);

        /* Store index for prev link (used in free list traversal) */
        entry->first_debug_target_idx = i;
    }

    /*
     * Step 7: Initialize entry 1 as the init/system process
     */
    P2_INFO_ALLOC_PTR = 1;
    init_entry = P2_INFO_ENTRY(1);

    /* Clear allocation list links */
    init_entry->next_index = 0;
    init_entry->pad_14 = 0;

    /* Set ASID = 1 */
    init_entry->asid = 1;

    /* Set owner_session = 1 */
    init_entry->owner_session = 1;

    /* Copy the UID generated for table entry 1 (DAT_00e7be9c) to the entry */
    init_entry->uid = PROC2_UID[1];

    /* Set PROC1 PID */
    init_entry->level1_pid = PROC1_$CURRENT;

    /* Clear cleanup flags */
    init_entry->cleanup_flags = 0;

    /* Clear child/sibling list links */
    init_entry->first_child_idx = 0;
    init_entry->next_child_sibling = 0;
    init_entry->parent_pgroup_idx = 0;
    init_entry->first_debug_target_idx = 0;
    init_entry->next_debug_target_idx = 0;

    /* Set UPID = 1 */
    init_entry->upid = 1;

    /* Clear session ID */
    init_entry->session_id = 0;

    /* Clear pgroup table index */
    init_entry->pgroup_table_idx = 0;

    /* Clear signal masks */
    init_entry->sig_pending = 0;
    init_entry->sig_blocked_1 = 0;
    init_entry->sig_blocked_2 = 0;
    init_entry->sig_mask_1 = 0;
    init_entry->sig_mask_2 = 0;
    init_entry->sig_mask_3 = 0;

    /* Set initial flags: clear most bits, set bit 7 (0x80) */
    init_entry->flags &= 0x01AF;
    init_entry->flags |= 0x8000;

    /* Clear padding/reserved */
    init_entry->pad_18[0] = 0;
    init_entry->pad_18[1] = 0;
    init_entry->acct_info_len = 0;

    /* Set name_len to 0x21 (indicates no name) */
    init_entry->name_len = 0x21;

    /* Set creation record pointer from address space global */
    init_entry->cr_rec = AS_$CR_REC;

    /* Set TTY UID to nil */
    init_entry->tty_uid = UID_$NIL;

    /* Set accounting UID to nil (entry+0x4C) */
    init_entry->acct_uid = UID_$NIL;

    /* Clear the accounting-info length (entry+0x54) */
    init_entry->acct_info_len = 0;

    /*
     * Step 8: Initialize eventcounts for init process
     */
    EC_$INIT(PROC_FORK_EC(init_entry->owner_session));
    EC_$INIT(PROC_CR_REC_EC(init_entry->owner_session));

    /*
     * Step 9: Map creation record area
     *
     * Maps the creation record file into the init process's address space.
     * The destination is at entry + 0x08 (the pad_08 area which holds cr_rec data).
     */
    {
        uint32_t cr_rec_addr = AS_$CR_REC;
        uint32_t cr_rec_size = AS_$CR_REC_FILE_SIZE;
        /* Parameters from original: 0xe308c4 contains mapping flags, 0xe30892 contains mode */
        uint32_t map_flags = 0x00010003;  /* Read/write, copy-on-write */
        uint32_t map_mode = 0x00000001;   /* Normal mode */

        MST_$MAP_AREA_AT(&cr_rec_addr, &cr_rec_size, &map_flags, &map_mode,
                         &init_entry->parent_uid, status_ret);

        status = OS_$BOOT_ERRCHK((char*)msg_unable_to_map, (char*)msg_creation_record_area,
                                  &path_len, status_ret);
        if ((int8_t)status >= 0) {
            return status;
        }
    }

    /*
     * Step 10: Map initial stack area
     *
     * Maps the initial stack file into the init process's address space.
     * The destination is at entry + 0xDC (within pad_bf, used for stack info).
     */
    {
        uint32_t stack_low = AS_$STACK_FILE_LOW;
        uint32_t stack_size = AS_$INIT_STACK_FILE_SIZE;
        uint32_t map_flags = 0x00010003;  /* Read/write, copy-on-write */
        uint32_t map_mode = 0x00000002;   /* Stack mode */

        MST_$MAP_AREA_AT(&stack_low, &stack_size, &map_flags, &map_mode,
                         &init_entry->stack_uid, status_ret);

        status = OS_$BOOT_ERRCHK((char*)msg_unable_to_map, (char*)msg_initial_area,
                                  &path_len, status_ret);
        if ((int8_t)status >= 0) {
            return status;
        }
    }

    /* Mark init entry as valid */
    init_entry->flags |= PROC2_FLAG_VALID;

    /* Set stack high pointer from address space global */
    init_entry->cr_rec_2 = AS_$STACK_HIGH;

    /*
     * Store boot flags at top of stack.
     * The original code writes boot flags 6 bytes below stack high,
     * with a 4-byte zero value at stack high - 4.
     */
    {
        uint32_t stack_top = init_entry->cr_rec_2;
        *(uint32_t*)(stack_top - 4) = 0;
        *(uint16_t*)(stack_top - 6) = (uint16_t)proc2_boot_flags;
    }

    /*
     * Step 11: Initialize boot flags
     */
    proc2_boot_flags &= 0xC000;  /* Clear all but top 2 bits */

    mmu_mode = MMU_$NORMAL_MODE();
    proc2_boot_flags &= 0x7FFF;  /* Clear bit 15 */
    proc2_boot_flags |= (mmu_mode & 0x80) << 8;  /* Set bit 15 from MMU mode */

    proc2_boot_flags &= 0xBFFF;  /* Clear bit 14 */
    proc2_boot_flags |= ((~DTTY_$USE_DTTY >> 7) & 1) << 14;  /* Set bit 14 from DTTY flag */

    /*
     * Step 12: Handle tape/floppy boot if requested.
     * The original tests bits 0/1 of the byte at offset 1 of the caller's
     * 16-bit boot flags word, i.e. the low-order byte of the big-endian
     * word (bits 0 and 1 of the 16-bit value).
     */
    if ((*boot_flags & 0x0001) != 0) {
        /* Tape boot requested */
        if (TAPE_$BOOT(&status) >= 0) {
            *status_ret = status;
            return status;
        }
    }

    if ((*boot_flags & 0x0002) != 0) {
        /* Floppy boot requested */
        if (FLOP_$BOOT(&status, status_ret) >= 0) {
            return status;
        }
    }

    /*
     * Step 13: Resolve /node_data/proc_dir
     */
    path_len = sizeof(proc_dir_path) - 1;
    NAME_$RESOLVE((char*)proc_dir_path, &path_len, &proc2_proc_dir_uid, status_ret);
    if (*status_ret != status_$ok) {
        proc2_proc_dir_uid = UID_$NIL;
    }

    /*
     * Step 14: Resolve and map /sys/boot_shell
     *
     * This sequence:
     * 1. Resolves the boot shell path to a UID
     * 2. Locks the file
     * 3. Maps it to get its base address and size
     * 4. Unmaps it
     * 5. Remaps it at a fixed address for execution
     */
    path_len = sizeof(boot_shell_path) - 1;
    NAME_$RESOLVE((char*)boot_shell_path, &path_len, &boot_shell_uid, status_ret);

    status = OS_$BOOT_ERRCHK((char*)msg_unable_to_resolve, (char*)boot_shell_path,
                              (uint16_t*)&path_len, status_ret);
    if ((int8_t)status >= 0) {
        return status;
    }

    /* Lock the boot shell file */
    {
        uint8_t lock_result[8];
        uint32_t lock_param1 = 0x00000001;  /* Lock mode */
        uint32_t lock_param2 = 0x00000000;  /* Offset */
        uint32_t lock_param3 = 0x00000001;  /* Length/mode */

        FILE_$LOCK(&boot_shell_uid, &lock_param1, &lock_param2, &lock_param3,
                   lock_result, status_ret);

        status = OS_$BOOT_ERRCHK((char*)msg_unable_to_lock, (char*)boot_shell_path,
                                  (uint16_t*)&path_len, status_ret);
        if ((int8_t)status >= 0) {
            return status;
        }
    }

    /* Map the boot shell to get its info */
    {
        uint8_t map_result[8];
        uint32_t map_info[3];  /* Receives base address, size, flags */
        uint32_t map_param1 = 0x00000000;  /* Start offset */
        uint32_t map_param2 = 0xFFFFFFFF;  /* Map entire file */
        uint32_t map_param3 = 0x00000001;  /* Read-only */
        uint32_t map_param4 = 0x00000000;  /* No fixed address */
        uint32_t map_param5 = 0x00000001;  /* Normal mode */

        MST_$MAP(&boot_shell_uid, &map_param1, &map_param2, (uint16_t*)&map_param3,
                 &map_param4, (uint8_t*)&map_param5, (int32_t*)map_result, status_ret);

        status = OS_$BOOT_ERRCHK((char*)msg_unable_to_map, (char*)boot_shell_path,
                                  (uint16_t*)&path_len, status_ret);
        if ((int8_t)status >= 0) {
            return status;
        }

        /* Save mapping info for remap */
        /* map_info receives: base_addr, size, attributes from A0 return */
        /* For now, we'll use the result directly */

        /* Unmap the boot shell */
        {
            uint8_t unmap_result[8];

            MST_$UNMAP(&boot_shell_uid, (uint32_t*)map_result, (uint32_t*)unmap_result, status_ret);

            status = OS_$BOOT_ERRCHK((char*)msg_unable_to_unmap, (char*)boot_shell_path,
                                      (uint16_t*)&path_len, status_ret);
            if ((int8_t)status >= 0) {
                return status;
            }
        }

        /* Remap at fixed address for execution */
        {
            uint8_t remap_result[8];
            uint32_t fixed_addr = *(uint32_t*)map_result;  /* Use returned base addr */

            MST_$MAP_AT(&fixed_addr, &boot_shell_uid, &map_param1, &map_param2,
                        &map_param3, &map_param4, &map_param5, remap_result, status_ret);

            status = OS_$BOOT_ERRCHK((char*)msg_unable_to_map, (char*)boot_shell_path,
                                      (uint16_t*)&path_len, status_ret);
            if ((int8_t)status >= 0) {
                return status;
            }

            /* Return the final status from mapping */
            return *(status_$t*)(map_result + 4);
        }
    }
}
