// OS_$INIT - Main operating system initialization
// Address: 0x00e337f4
// Size: 4054 bytes
//
// This is the main boot initialization function that initializes all
// operating system subsystems in the correct order.

#include "os.h"

// External boot device and flags
short OS_$BOOT_DEVICE;

// External network/diskless flags
extern char NETWORK_$DISKLESS;
extern char NETWORK_$REALLY_DISKLESS;
extern char NETWORK_$DO_CHKSUM;
extern uint32_t NETWORK_$MOTHER_NODE;
extern uid_$t NETWORK_$PAGING_FILE_UID;

// External disk flag
extern char DISK_$DO_CHKSUM;

// External paging flag
extern char PMAP_$SHUTTING_DOWN_FLAG;

// External UIDs
extern uid_$t UID_$NIL;
extern uid_$t OS_WIRED_$UID;
extern uid_$t DISPLAY1_$UID;
extern uid_$t NAME_$NODE_UID;
extern uid_$t ACL_$FNDWRX;
extern uid_$t LV_LABEL_$UID;

// External process identifiers
extern short PROC1_$CURRENT;
extern uint32_t NODE_$ME;

// External time variables
extern uint32_t TIME_$CLOCKH;
extern uint32_t TIME_$CURRENT_CLOCKH;
extern uint32_t TIME_$BOOT_TIME;
extern uint32_t TIME_$CURRENT_TIME;

// External memory management variables
extern short MST_$MST_PAGES_LIMIT;
extern uint32_t AS_$STACK_HIGH;
extern int ROUTE_$PORT;
extern short CAL_$BOOT_VOLX;

// External calendar timezone
extern struct {
    struct { uint32_t high; uint32_t low; } drift;
} CAL_$TIMEZONE;

// External null process pointer
extern void *_NULL_PC;
extern void *NULLPROC;

// External trap vectors
extern void *_PROM_TRAP_BUS_ERROR;
extern void *FIM_$BUS_ERR;
extern void *FIM_$PARITY_TRAP;

// External interrupt stack base
extern char *INT_STACK_BASE;

// Boot info data table at 0xe351f4
extern uint32_t BOOT_INFO_TABLE[];

// External subsystem pre-initialization functions
extern void MST_$PRE_INIT(void);
extern void MMU_$INIT(void);
extern void AS_$INIT(void);
extern void MMU_$REMOVE(uint32_t page);
extern void MMAP_$INIT(void *boot_info);
extern void MMU_$SET_SYSREV(void);
extern void MMU_$SET_PROT(uint32_t ppn, uint16_t prot);
extern void PEB_$INIT(void);
extern void MST_$INIT(void);
extern void DXM_$INIT(void);

// External network boot function
extern char NET_IO_$BOOT_DEVICE(short boot_device, short param);

// External ring ID function
extern uint32_t RING_$GET_ID(void *param);

// External I/O functions
extern void IO_$INIT(void *param1, void *param2, status_$t *status);
extern void IO_$GET_DCTE(void *param1, void *param2, status_$t *status);

// External terminal init
extern void TERM_$INIT(short *param1, short *param2);
extern void DTTY_$INIT(short *param1, short *param2);

// External time init
extern void TIME_$INIT(short *param);

// External UID init
extern void UID_$INIT(void);

// External process functions
extern void PROC1_$INHIBIT_BEGIN(void);
extern void PROC1_$INHIBIT_END(void);
extern void PROC1_$INIT(void);
extern void PROC1_$INIT_LOADAV(void);
extern void PROC1_$SET_TYPE(short pid, short type);
extern void PROC1_$SET_ASID(uint16_t asid);
extern void PROC1_$CREATE_P(void *func, uint32_t flags, status_$t *status);
extern void *PROC2_$INIT(void *param, status_$t *status);

// External subsystem init functions
extern void SMD_$INIT(void);
extern void SMD_$INIT_BLINK(void);
extern short SMD_$INQ_DISP_TYPE(short param);
extern void TPAD_$INIT(void);
extern void EC2_$INIT_S(void);
extern void ACL_$INIT(void);
extern void ACL_$ENTER_SUPER(void);
extern void AST_$INIT(void);
extern void AST_$ACTIVATE_AOTE_CANNED(short param1, short param2);
extern void AST_$PMAP_ASSOC(void *param1, short param2, uint32_t param3,
                            uint8_t param4, uint16_t param5, status_$t *status);
extern void AREA_$INIT(void);
extern void DISK_$INIT(void);
extern void DBUF_$INIT(void);
extern void *DBUF_$GET_BLOCK(short volx, short param2, uid_$t *uid,
                             short param4, short param5, status_$t *status);
extern void DBUF_$SET_BUFF(void *buf, short mode, status_$t *status);
extern void SOCK_$INIT(void);
extern void NETWORK_$INIT(void);
extern void NETWORK_$LOAD(void);
extern void NETWORK_$ADD_REQUEST_SERVERS(void *param, status_$t *status);
extern void FILE_$LOCK_INIT(void);
extern void FILE_$LOCK(uid_$t *uid, void *param2, void *param3,
                       void *param4, void *buf, status_$t *status);
extern void FILE_$SET_LEN(uid_$t *uid, void *len, status_$t *status);
extern void FILE_$SET_REFCNT(uid_$t *uid, void *param, status_$t *status);
extern void HINT_$INIT(void);
extern void HINT_$INIT_CACHE(void);
extern void HINT_$ADD_NET(short port);
extern void NAME_$INIT(uid_$t *uid1, uid_$t *uid2);
extern void NAME_$SET_WDIR(const char *path, void *param, status_$t *status);
extern void LOG_$INIT(void);
extern void XPD_$INIT(void);
extern void PCHIST_$INIT(void);
extern void PEB_$LOAD_WCS(void);
extern void PACCT_$INIT(void);
extern void AUDIT_$INIT(void);

// External volume functions
extern void VOLX_$MOUNT(void *boot_device, void *param2, void *param3,
                        void *param4, void *param5, void *param6,
                        uid_$t *uid, uid_$t *uid_out, status_$t *status);
extern status_$t VOLX_$SHUTDOWN(void);
extern void VOLX_$REC_ENTRY(void *param1, uid_$t *uid);
extern void VTOCE_$READ(void *param1, void *param2, status_$t *status);

// External calendar functions
extern char CAL_$VERIFY(void *param1, void *param2, void *param3, status_$t *status);
extern uint32_t CAL_$CLOCK_TO_SEC(void *clock);
extern void CAL_$SEC_TO_CLOCK(uint32_t *sec, void *clock);

// External MST functions
extern void MST_$MAP_CANNED_AT(uint32_t vaddr, uid_$t *uid, uint32_t offset,
                                uint32_t size, uint32_t prot, uint32_t flags,
                                uint32_t param, status_$t *status);
extern uint16_t MST_$ALLOC_ASID(status_$t *status);
extern void MST_$DISKLESS_INIT(char flag, uint32_t mother_node, uint32_t node_me);

// External MMAP functions
extern void MMAP_$UNWIRE(uint32_t ppn);

// External paging functions
extern void *PMAP_$PURIFIER_L;
extern void *PMAP_$PURIFIER_R;
extern void *DXM_$HELPER_UNWIRED;
extern void *DXM_$HELPER_WIRED;

// External lock functions
extern void ML_$LOCK(short lock_id);
extern void ML_$UNLOCK(short lock_id);

// External helper functions
extern uint16_t VTOP_OR_CRASH(uint32_t vaddr);
extern void SUB48(void *a, void *b);
extern void PRINT_BUILD_TIME(void);
extern void CRASH_SYSTEM(status_$t *status);
extern char MMU_$NORMAL_MODE(void);
extern char prompt_for_yes_or_no(void);
extern void FUN_00e6d1cc(const char *msg);  // Display message
extern void FUN_00e6d240(uint32_t addr);    // Initialize page
extern void FUN_00e6d254(void *param);      // Final init
extern void FUN_00e3366c(short cmd, uint32_t param);  // Diskless helper
extern void FUN_00e29138(uint8_t a, uint16_t b, short *out);
extern void FUN_00e2f1d4(uint16_t param);
extern void ERROR_$PRINT(short code, short *param1, short *param2);

// Error status codes
extern status_$t No_err;
extern status_$t No_calendar_on_system_err;
extern status_$t OS_BAT_disk_needs_salvaging_err;
extern status_$t status_$disk_needs_salvaging;
extern status_$t status_$cal_refused;
extern status_$t status_$pmap_bad_assoc;

// Boot parameter structure (passed from bootstrap)
typedef struct {
    short boot_device;        // 0x00: Boot device ID
    uint16_t flags;           // 0x02: Boot flags
    uint32_t boot_info;       // 0x04: Additional boot info
    // ... more fields
} boot_params_t;

// Diskless boot info structure
typedef struct {
    uint32_t mother_node;     // 0x00: Mother node ID
    uid_$t paging_file_uid;   // 0x04: Paging file UID
    uid_$t param3;            // 0x0C: Additional UID
    uid_$t extra_uids[3];     // 0x14: Extra UIDs
} diskless_info_t;

void OS_$INIT(uint32_t *param_1, uint32_t *param_2)
{
    status_$t status;
    short boot_device;
    uint16_t boot_flags;
    uint32_t boot_info;
    uid_$t local_uid1, local_uid2;
    char diskless_flag;
    char has_calendar;
    short term_param1, term_param2;
    uint16_t asid;
    short i;
    uint32_t local_buf[9];
    uint32_t diskless_buf[12];
    uint8_t flags_buf[8];
    char err_msg[6];
    uint16_t ws_mode;
    void *ptr;

    // Copy boot parameters from caller
    for (i = 0; i < 9; i++) {
        local_buf[i] = param_1[i];
    }
    for (i = 0; i < 12; i++) {
        diskless_buf[i] = param_2[i];
    }

    // Extract boot device and flags
    boot_device = (short)local_buf[0];
    boot_flags = (uint16_t)(local_buf[0] >> 16);
    boot_info = local_buf[1];

    // Clear shutdown flag
    PMAP_$SHUTTING_DOWN_FLAG = 0;

    // Initialize core memory management subsystems
    MST_$PRE_INIT();
    MMU_$INIT();
    AS_$INIT();

    // Remove mappings for pages that should be free
    // (so MMAP_$INIT sees them as available)
    MMU_$REMOVE(0x405);  // Page at 0x101400
    MMU_$REMOVE(0x406);  // Page at 0x101800

    // Initialize memory map
    MMAP_$INIT(BOOT_INFO_TABLE);

    // Install interrupt vectors from boot info table
    // (vectors at specific locations based on table data)
    // This loop processes the vector table entries
    {
        uint32_t *vec_table = &BOOT_INFO_TABLE[1];
        uint32_t *vec_src = vec_table;
        short vec_count = 1;

        for (vec_count = 1; vec_count < 0x29; vec_count++) {
            short vec_start = *(short *)((char *)vec_src + 0xdc);
            short vec_num = *(short *)((char *)vec_src + 0xde);

            if (vec_num > 0) {
                uint32_t *vec_ptr = (uint32_t *)(vec_start * 4);
                short j;
                for (j = 0; j < vec_num; j++) {
                    if (vec_src[0x38] != 0) {
                        *vec_ptr = vec_src[0x38];
                    }
                    vec_ptr++;
                    vec_src++;
                    vec_count++;
                }
            }
            vec_src++;
        }
    }

    // Handle boot device flags
    ws_mode = 0;
    if (boot_device == 1) {
        ws_mode = 2;
        boot_device = 0;
    }
    OS_$BOOT_DEVICE = boot_device;

    // Set system revision info
    MMU_$SET_SYSREV();

    // Initialize network boot and determine if diskless
    NETWORK_$DISKLESS = NET_IO_$BOOT_DEVICE(boot_device, (short)boot_info);

    if (NETWORK_$DISKLESS < 0) {
        // Diskless boot - get info from mother node
        NETWORK_$MOTHER_NODE = diskless_buf[0];
        NETWORK_$PAGING_FILE_UID.high = diskless_buf[1];
        NETWORK_$PAGING_FILE_UID.low = diskless_buf[2];
        local_uid1.high = diskless_buf[3];
        local_uid1.low = diskless_buf[4];
    } else {
        // Local boot
        local_uid1.high = UID_$NIL.high;
        local_uid1.low = UID_$NIL.low;
    }
    local_uid2.high = local_uid1.high;
    local_uid2.low = local_uid1.low;

    // Begin inhibiting interrupts for initialization
    PROC1_$INHIBIT_BEGIN();

    // Initialize MST and DXM
    MST_$INIT();
    DXM_$INIT();

    // Map OS wired areas into memory
    MST_$MAP_CANNED_AT(0xd00000, &OS_WIRED_$UID, 0, 0xa8000, 0x170001, 0, 0, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    MST_$MAP_CANNED_AT(0xe00000, &OS_WIRED_$UID, 0, 0x38000, 0x170001, 0, 0, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    // Map additional OS areas with appropriate protection
    {
        uint16_t prot = (boot_flags & 0x8004) == 4 ? 0x13 : 0x17;
        MST_$MAP_CANNED_AT(0xe38000, &OS_WIRED_$UID, 0, 0x40000, (prot << 16) | 1,
                           0xFF0000, 0, &status);
        if (status != status_$ok) {
            CRASH_SYSTEM(&status);
        }
    }

    // Initialize PEB subsystem
    PEB_$INIT();

    // Initialize I/O subsystem
    flags_buf[0] = (boot_flags & 0x8000) ? 0xFF : 0;
    IO_$INIT(&No_err, flags_buf, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    // Get our node ID from the ring
    NODE_$ME = RING_$GET_ID(NULL);

    // Determine terminal initialization parameters
    term_param2 = 1;
    if (boot_flags & 0x10) {
        term_param2 = 2;
    }
    if ((boot_flags & 1) == 0) {
        term_param1 = 0;
        term_param2 = 1;
    } else if ((boot_flags & 2) == 0) {
        term_param1 = 2;
        term_param2 = 1;
    } else {
        term_param1 = 1;
    }

    // Initialize terminal subsystem
    TERM_$INIT(&term_param1, &term_param2);

    // Check for calendar hardware
    IO_$GET_DCTE(NULL, NULL, &status);
    has_calendar = (status == status_$ok) ? -1 : 0;

    // Non-diskless systems require calendar hardware
    if (NETWORK_$DISKLESS >= 0 && has_calendar >= 0) {
        CRASH_SYSTEM(&No_calendar_on_system_err);
    }

    // Initialize time subsystem
    err_msg[0] = has_calendar;
    TIME_$INIT((short *)err_msg);

    // Initialize UID generation
    UID_$INIT();

    // Initialize process management
    _NULL_PC = NULLPROC;
    PROC1_$INIT();
    PROC1_$SET_TYPE(PROC1_$CURRENT, 1);

    // Initialize additional subsystems
    SMD_$INIT();
    TPAD_$INIT();
    DTTY_$INIT(&term_param1, &term_param2);
    EC2_$INIT_S();

    // Print build time banner
    PRINT_BUILD_TIME();

    // Install parity trap handler
    *(void **)0x7c = FIM_$PARITY_TRAP;

    // Initialize security and object management
    ACL_$INIT();
    AST_$INIT();
    AREA_$INIT();

    // Initialize disk subsystems for non-diskless systems
    if (NETWORK_$DISKLESS >= 0) {
        DISK_$INIT();
        DBUF_$INIT();
    }

    CAL_$BOOT_VOLX = 0;

    // Handle boot volume mounting and paging file setup
    if (NETWORK_$DISKLESS < 0) {
        // Diskless boot - paging file comes from network
        // (Complex diskless initialization omitted for brevity)
    } else {
        // Local boot - mount boot volume
        short vol_unit = (short)(boot_info >> 16);
        if (vol_unit == 0) vol_unit = 1;

        VOLX_$MOUNT(&boot_device, NULL, &boot_info, &vol_unit, NULL, NULL,
                    &UID_$NIL, (uid_$t *)local_buf, &status);

        if (status == status_$disk_needs_salvaging) {
            FUN_00e6d1cc("    BOOT VOLUME NEEDS SALVAGING");
            if (MMU_$NORMAL_MODE() < 0) {
                CRASH_SYSTEM(&status);
            }
            FUN_00e6d1cc("Proceed to bring up OS, and risk data?");
            if (prompt_for_yes_or_no() >= 0) {
                CRASH_SYSTEM(&OS_BAT_disk_needs_salvaging_err);
            }
            VOLX_$MOUNT(&boot_device, NULL, &boot_info, &vol_unit, NULL, NULL,
                        &UID_$NIL, (uid_$t *)local_buf, &status);
        }

        if (status != status_$ok) {
            CRASH_SYSTEM(&status);
        }

        CAL_$BOOT_VOLX = 1;

        // Verify calendar
        if (CAL_$VERIFY(NULL, NULL, NULL, &status) >= 0 &&
            status == status_$cal_refused) {
            VOLX_$SHUTDOWN();
            CRASH_SYSTEM(&No_err);
        }

        // Read paging file info from volume label
        ptr = DBUF_$GET_BLOCK(1, 0, &LV_LABEL_$UID, 0, 0, &status);
        if (status != status_$ok) {
            CRASH_SYSTEM(&status);
        }

        {
            uint32_t paging_info = *(uint32_t *)((char *)ptr + 0x5c);
            DBUF_$SET_BUFF(ptr, 8, &status);

            if ((paging_info >> 4) == 0) {
                // No paging file on boot volume
                FUN_00e6d1cc("Boot device has no OS paging file");
                FUN_00e6d1cc("see the Installation Procedures chapter");
                FUN_00e6d1cc("for information on how to correct this");
                FUN_00e6d1cc("For now, the OS will NOT page, with performance");
                FUN_00e6d1cc("degradation");
                NETWORK_$PAGING_FILE_UID.high = OS_WIRED_$UID.high;
                NETWORK_$PAGING_FILE_UID.low = OS_WIRED_$UID.low;
            } else {
                // Read paging file UID
                // (VTOCE read to get paging file info)
            }
        }
    }

    NETWORK_$REALLY_DISKLESS = NETWORK_$DISKLESS;

    // Initialize additional memory pages
    FUN_00e6d240(0xeb0000);
    FUN_00e6d240(0xeb0800);
    FUN_00e6d240(0xeb2000);

    // Clear interrupt stack
    {
        char *stack = INT_STACK_BASE;
        for (i = 0x3ff; i >= 0; i--) {
            *--stack = 0;
        }
    }

    // Handle checksums if requested
    if (NETWORK_$DO_CHKSUM < 0) {
        OS_$CHKSUM(NULL, NULL, NULL, &NETWORK_$DO_CHKSUM, &status);
    }
    if (DISK_$DO_CHKSUM < 0) {
        DISK_$DO_CHKSUM = 0;
        OS_$CHKSUM(NULL, NULL, NULL, flags_buf, &status);
    }

    // Install display ASTE if needed
    {
        short result;
        if (FUN_00e29138(0xd2, 0x4aa0, &result) < 0) {
            OS_$INSTALL_DISPLAY_ASTE((void *)&DISPLAY1_$UID, NULL, NULL, NULL);
        }
    }

    // Lock master lock if we have a calendar
    if (has_calendar >= 0) {
        ML_$LOCK(1);
    }

    // Create system processes
    PROC1_$CREATE_P(PMAP_$PURIFIER_L, 0xc000005, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    PROC1_$CREATE_P(PMAP_$PURIFIER_R, 0xc000005, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    PROC1_$CREATE_P(DXM_$HELPER_UNWIRED, 0xc000006, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    // Allocate ASID for this process
    asid = MST_$ALLOC_ASID(&status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }
    PROC1_$SET_ASID(asid);
    PROC1_$SET_TYPE(PROC1_$CURRENT, 0xb);

    // Enter superuser mode
    ACL_$ENTER_SUPER();

    // Initialize networking
    SOCK_$INIT();
    NETWORK_$INIT();

    // Initialize time from network if diskless
    if (has_calendar >= 0) {
        FUN_00e3366c(2, NETWORK_$MOTHER_NODE);
        TIME_$CURRENT_CLOCKH = TIME_$CLOCKH;
        TIME_$BOOT_TIME = TIME_$CLOCKH;
        // Time conversion and setup...
        UID_$INIT();
        ML_$UNLOCK(1);
    }

    // Initialize load averaging
    PROC1_$INIT_LOADAV();

    // Initialize file locking
    FILE_$LOCK_INIT();

    // Install bus error handler
    _PROM_TRAP_BUS_ERROR = &FIM_$BUS_ERR;

    // Create wired DXM helper
    PROC1_$CREATE_P(DXM_$HELPER_WIRED, 0x8000004, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    // Initialize hint cache
    HINT_$INIT_CACHE();

    // Initialize naming
    NAME_$INIT(&local_uid1, &local_uid2);

    // Add network request servers
    NETWORK_$ADD_REQUEST_SERVERS(NULL, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    // Lock paging file
    FILE_$LOCK(&NETWORK_$PAGING_FILE_UID, NULL, NULL, NULL, NULL, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    if (NETWORK_$DISKLESS < 0) {
        FILE_$SET_REFCNT(&NETWORK_$PAGING_FILE_UID, &No_err, &status);
    }

    // Set paging file length if not using wired area
    if (NETWORK_$PAGING_FILE_UID.high != OS_WIRED_$UID.high ||
        NETWORK_$PAGING_FILE_UID.low != OS_WIRED_$UID.low) {
        // Complex paging file setup with MST mapping...
    }

    // Verify node number matches stored value
    if (NETWORK_$DISKLESS >= 0) {
        if ((NAME_$NODE_UID.low & 0xfffff) != NODE_$ME) {
            FUN_00e6d1cc("The node number of this node differs");
            FUN_00e6d1cc("from that stored on disk");
            FUN_00e6d1cc("Do you want to proceed?");
            if (prompt_for_yes_or_no() >= 0) {
                status = status_$ok;
                OS_$SHUTDOWN(&status);
            }
        }
        VOLX_$REC_ENTRY(NULL, &NAME_$NODE_UID);
    }

    // Set working directory to root
    NAME_$SET_WDIR("/", NULL, &status);

    // Initialize process manager phase 2
    PROC2_$INIT(&ws_mode, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    // Initialize routing and hints
    {
        int route_port = ROUTE_$PORT;
        if (NETWORK_$DISKLESS < 0) {
            HINT_$INIT();
            if (ROUTE_$PORT == 0) {
                ROUTE_$PORT = route_port;
                HINT_$ADD_NET((short)route_port);
            }
        } else {
            HINT_$INIT();
        }
    }

    // Diskless-specific time zone setup
    if (NETWORK_$DISKLESS < 0) {
        FUN_00e3366c(8, diskless_buf[0]);
        CAL_$TIMEZONE.drift.high = 0;
        CAL_$TIMEZONE.drift.low = 0;
        FUN_00e3366c(0x37, diskless_buf[0]);
    }

    // Initialize remaining subsystems
    LOG_$INIT();
    XPD_$INIT();
    PCHIST_$INIT();
    NETWORK_$LOAD();
    PEB_$LOAD_WCS();

    // Set final memory protection
    {
        uint16_t ppn = VTOP_OR_CRASH(0xe33774);
        MMU_$SET_PROT(ppn, 0x13);
    }

    // Check display type for diskless init
    {
        short disp_type = SMD_$INQ_DISP_TYPE(0x37d2);
        if (disp_type == 0) {
            disp_type = SMD_$INQ_DISP_TYPE(0x48e4);
        }
        MST_$DISKLESS_INIT(-(disp_type == 3), NETWORK_$MOTHER_NODE, NODE_$ME);
    }

    // Final initialization
    SMD_$INIT_BLINK();
    PACCT_$INIT();
    AUDIT_$INIT();

    // End interrupt inhibition
    PROC1_$INHIBIT_END();

    // Call final init function
    FUN_00e6d254(&AS_$STACK_HIGH);
}
