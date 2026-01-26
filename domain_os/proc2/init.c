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

/* Boot flags storage */
#if defined(M68K)
    #define BOOT_FLAGS          (*(uint16_t*)0xE7C068)
#else
    extern uint16_t boot_flags_storage;
    #define BOOT_FLAGS          boot_flags_storage
#endif

/* Global UID storage for system UIDs */
#if defined(M68K)
    #define PROC_DIR_UID        (*(uid_t*)0xE7BE84)
    #define SYSTEM_UID_2        (*(uid_t*)0xE7BE9C)
    /* UID table: 8 bytes per entry, indexed by ASID */
    #define UID_TABLE_BASE      0xE7BE94
    #define UID_TABLE_ENTRY(n)  (*(uid_t*)(UID_TABLE_BASE + (n) * 8))
#else
    extern uid_t proc_dir_uid;
    extern uid_t system_uid_2;
    extern uid_t uid_table[];
    #define PROC_DIR_UID        proc_dir_uid
    #define SYSTEM_UID_2        system_uid_2
    #define UID_TABLE_ENTRY(n)  uid_table[n]
#endif

/* Eventcount base addresses */
#if defined(M68K)
    #define EC1_FORK_ARRAY      ((void*)0xE2B978)
    #define EC1_CR_REC_ARRAY    ((void*)0xE2B96C)
#else
    extern void *ec1_fork_array;
    extern void *ec1_cr_rec_array;
    #define EC1_FORK_ARRAY      ec1_fork_array
    #define EC1_CR_REC_ARRAY    ec1_cr_rec_array
#endif

/* Address space globals for memory mapping */
#if defined(M68K)
    #define AS_$CR_REC              (*(uint32_t*)0xE2B930)
    #define AS_$CR_REC_FILE_SIZE    (*(uint32_t*)0xE2B96C)
    #define AS_$STACK_FILE_LOW      (*(uint32_t*)0xE2B92C)
    #define AS_$INIT_STACK_FILE_SIZE (*(uint32_t*)0xE2B960)
    #define AS_$STACK_HIGH          (*(uint32_t*)0xE2B950)
#else
    extern uint32_t as_cr_rec;
    extern uint32_t as_cr_rec_file_size;
    extern uint32_t as_stack_file_low;
    extern uint32_t as_init_stack_file_size;
    extern uint32_t as_stack_high;
    #define AS_$CR_REC              as_cr_rec
    #define AS_$CR_REC_FILE_SIZE    as_cr_rec_file_size
    #define AS_$STACK_FILE_LOW      as_stack_file_low
    #define AS_$INIT_STACK_FILE_SIZE as_init_stack_file_size
    #define AS_$STACK_HIGH          as_stack_high
#endif

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

/* Forward declaration of PROC_FORK_EC macro from create.c */
#define PROC_FORK_EC(idx)       ((void*)((uintptr_t)EC1_FORK_ARRAY + ((idx) - 1) * 0x18))
#define PROC_CR_REC_EC(idx)     ((void*)((uintptr_t)EC1_FORK_ARRAY + ((idx) - 1) * 0x18 + 0x0C))

status_$t PROC2_$INIT(int32_t boot_flags_param, status_$t *status_ret)
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
     */
    UID_$GEN(&PROC2_UID);
    UID_$GEN(&SYSTEM_UID_2);

    /*
     * Step 2: Set priority for init process
     */
    min_pri = 0x10;
    max_pri = 0x10;
    PROC1_$SET_PRIORITY(PROC1_$CURRENT, 0xFF00, &min_pri, &max_pri);

    /*
     * Step 3: Initialize UID table with generated UID
     * (56 entries, indices 0-55)
     */
    for (i = 0; i < 56; i++) {
        UID_TABLE_ENTRY(i) = PROC2_UID;
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

        /* Set UID to nil */
        *(uint32_t*)((char*)entry + 0x08) = UID_$NIL.high;
        *(uint32_t*)((char*)entry + 0x0C) = UID_$NIL.low;

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

    /* Copy system UID to entry */
    init_entry->uid = SYSTEM_UID_2;

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
    init_entry->pgroup_uid_idx = 0;

    /* Set name_len to 0x21 (indicates no name) */
    init_entry->name_len = 0x21;

    /* Set creation record pointer from address space global */
    init_entry->cr_rec = AS_$CR_REC;

    /* Set TTY UID to nil */
    init_entry->tty_uid = UID_$NIL;

    /* Set pgroup UID to nil */
    init_entry->pgroup_uid = UID_$NIL;

    /* Clear pgroup_uid_idx */
    init_entry->pgroup_uid_idx = 0;

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
                         (void*)((char*)init_entry + 0x08), status_ret);

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
                         (void*)((char*)init_entry + 0xDC), status_ret);

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
        *(uint16_t*)(stack_top - 6) = BOOT_FLAGS;
    }

    /*
     * Step 11: Initialize boot flags
     */
    BOOT_FLAGS &= 0xC000;  /* Clear all but top 2 bits */

    mmu_mode = MMU_$NORMAL_MODE();
    BOOT_FLAGS &= 0x7FFF;  /* Clear bit 15 */
    BOOT_FLAGS |= (mmu_mode & 0x80) << 8;  /* Set bit 15 from MMU mode */

    BOOT_FLAGS &= 0xBFFF;  /* Clear bit 14 */
    BOOT_FLAGS |= ((~DTTY_$USE_DTTY >> 7) & 1) << 14;  /* Set bit 14 from DTTY flag */

    /*
     * Step 12: Handle tape/floppy boot if requested
     */
    if (((uint8_t*)&boot_flags_param)[1] & 0x01) {
        /* Tape boot requested */
        if (TAPE_$BOOT(&status) >= 0) {
            *status_ret = status;
            return status;
        }
    }

    if (((uint8_t*)&boot_flags_param)[1] & 0x02) {
        /* Floppy boot requested */
        if (FLOP_$BOOT(&status, status_ret) >= 0) {
            return status;
        }
    }

    /*
     * Step 13: Resolve /node_data/proc_dir
     */
    path_len = sizeof(proc_dir_path) - 1;
    NAME_$RESOLVE((char*)proc_dir_path, &path_len, &PROC_DIR_UID, status_ret);
    if (*status_ret != status_$ok) {
        PROC_DIR_UID = UID_$NIL;
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
