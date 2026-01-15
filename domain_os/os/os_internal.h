/*
 * os/os_internal.h - Internal OS Definitions
 *
 * Contains internal functions, data, and types used only within
 * the OS subsystem. External consumers should use os/os.h.
 */

#ifndef OS_INTERNAL_H
#define OS_INTERNAL_H

#include "os/os.h"
#include "uid/uid.h"
#include "time/time.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "ast/ast.h"
#include "mst/mst.h"
#include "mmap/mmap.h"
#include "mmu/mmu.h"

/*
 * ============================================================================
 * Global Data - Network Configuration
 * ============================================================================
 */

extern char NETWORK_$DISKLESS;
extern char NETWORK_$REALLY_DISKLESS;
extern char NETWORK_$DO_CHKSUM;
extern uint32_t NETWORK_$MOTHER_NODE;
extern uid_t NETWORK_$PAGING_FILE_UID;

/*
 * ============================================================================
 * Global Data - Disk Configuration
 * ============================================================================
 */

extern char DISK_$DO_CHKSUM;

/*
 * ============================================================================
 * Global Data - PMAP State
 * ============================================================================
 */

extern char PMAP_$SHUTTING_DOWN_FLAG;

/*
 * ============================================================================
 * Global Data - Well-known UIDs
 * ============================================================================
 */

extern uid_t OS_WIRED_$UID;
extern uid_t DISPLAY1_$UID;
extern uid_t NAME_$NODE_UID;
extern uid_t ACL_$FNDWRX;
extern uid_t LV_LABEL_$UID;
extern uid_t RGYC_$G_LOCKSMITH_UID;

/*
 * ============================================================================
 * Global Data - Process/Memory State
 * ============================================================================
 */

extern uint32_t NODE_$ME;
extern short MST_$MST_PAGES_LIMIT;
extern uint32_t AS_$STACK_HIGH;
extern int ROUTE_$PORT;
extern short CAL_$BOOT_VOLX;

/*
 * ============================================================================
 * Global Data - Boot Info
 * ============================================================================
 */

extern uint32_t BOOT_INFO_TABLE[];

/*
 * ============================================================================
 * Global Data - Interrupt Stack
 * ============================================================================
 */

extern char *INT_STACK_BASE;

/*
 * ============================================================================
 * Global Data - Trap Handlers
 * ============================================================================
 */

extern void *_NULL_PC;
extern void *NULLPROC;
extern void *_PROM_TRAP_BUS_ERROR;
extern void *FIM_$BUS_ERR;
extern void *FIM_$PARITY_TRAP;

/*
 * ============================================================================
 * Global Data - Daemon Entry Points
 * ============================================================================
 */

extern void *PMAP_$PURIFIER_L;
extern void *PMAP_$PURIFIER_R;
extern void *DXM_$HELPER_UNWIRED;
extern void *DXM_$HELPER_WIRED;

/*
 * ============================================================================
 * Global Data - Shutdown Wiring Info
 * ============================================================================
 */

extern m68k_ptr_t FP_$SAVEP;
extern m68k_ptr_t PTR_OS_PROC_SHUTWIRED;
extern m68k_ptr_t PTR_OS_PROC_SHUTWIRED_END;
extern m68k_ptr_t PTR_OS_DATA_SHUTWIRED;
extern m68k_ptr_t PTR_OS_DATA_SHUTWIRED_END;

/*
 * ============================================================================
 * Global Data - Status Constants
 * ============================================================================
 */

extern status_$t No_err;
extern status_$t No_calendar_on_system_err;
extern status_$t OS_BAT_disk_needs_salvaging_err;
extern status_$t status_$disk_needs_salvaging;
extern status_$t status_$cal_refused;
extern status_$t status_$pmap_bad_assoc;

/*
 * ============================================================================
 * Subsystem Init Functions
 * ============================================================================
 */

/* Memory Management */
void MST_$PRE_INIT(void);
void MMU_$INIT(void);
void AS_$INIT(void);
void MMU_$REMOVE(uint32_t page);
void MMAP_$INIT(void *boot_info);
void MMU_$SET_SYSREV(void);
void MMU_$SET_PROT(uint32_t ppn, uint16_t prot);
void MMAP_$UNWIRE(uint32_t ppn);
void MST_$INIT(void);
void MST_$DISKLESS_INIT(char flag, uint32_t mother_node, uint32_t node_me);
void MST_$MAP_CANNED_AT(uint32_t vaddr, uid_t *uid, uint32_t offset,
                        uint32_t len, status_$t *status);
uint16_t MST_$ALLOC_ASID(status_$t *status);
void PEB_$INIT(void);
void PEB_$LOAD_WCS(void);

/* I/O and DMA */
void DXM_$INIT(void);
void IO_$INIT(void *param1, void *param2, status_$t *status);
void IO_$GET_DCTE(void *param1, void *param2, status_$t *status);

/* Terminal/Display */
void TERM_$INIT(short *param1, short *param2);
void DTTY_$INIT(short *param1, short *param2);
void SMD_$INIT(void);
void SMD_$INIT_BLINK(void);
short SMD_$INQ_DISP_TYPE(short param);
void TPAD_$INIT(void);

/* Time */
void TIME_$INIT(short *param);

/* UID */
void UID_$INIT(void);

/* Process Management */
void PROC1_$INHIBIT_BEGIN(void);
void PROC1_$INHIBIT_END(void);
void PROC1_$INIT(void);
void PROC1_$INIT_LOADAV(void);
void PROC1_$SET_TYPE(short pid, short type);
void PROC1_$SET_ASID(uint16_t asid);
void PROC1_$CREATE_P(void *func, uint32_t flags, status_$t *status);
void *PROC2_$INIT(void *param, status_$t *status);
void PROC2_$SHUTDOWN(void);

/* Event Counters */
void EC2_$INIT_S(void);
void *EC2_$REGISTER_EC1(ec_$eventcount_t *ec, status_$t *status);

/* Security */
void ACL_$INIT(void);
void ACL_$ENTER_SUPER(void);
void ACL_$GET_RE_SIDS(void *buf, uid_t *uid, status_$t *status);

/* AST */
void AST_$INIT(void);
void AST_$ACTIVATE_AOTE_CANNED(short param1, short param2);
void AST_$PMAP_ASSOC(void *param1, short param2, uint32_t param3,
                      void *param4, status_$t *status);

/* Area Management */
void AREA_$INIT(void);
void AREA_$SHUTDOWN(void);

/* Disk/Volume */
void DISK_$INIT(void);
void DBUF_$INIT(void);
void *DBUF_$GET_BLOCK(short volx, short param2, uid_t *uid,
                      void *param4, status_$t *status);
void DBUF_$SET_BUFF(void *buf, short mode, status_$t *status);
void VOLX_$MOUNT(void *boot_device, void *param2, void *param3,
                 void *param4, status_$t *status);
status_$t VOLX_$SHUTDOWN(void);
void VOLX_$REC_ENTRY(void *param1, uid_t *uid);
void VTOCE_$READ(void *param1, void *param2, status_$t *status);

/* Network */
void SOCK_$INIT(void);
void NETWORK_$INIT(void);
void NETWORK_$LOAD(void);
void NETWORK_$ADD_REQUEST_SERVERS(void *param, status_$t *status);
void NETWORK_$DISMISS_REQUEST_SERVERS(void);
void NETWORK_$SET_SERVICE(const void *param1, status_$t *param2, status_$t *status);
char NET_IO_$BOOT_DEVICE(short boot_device, short param);
uint32_t RING_$GET_ID(void *param);
void ROUTE_$SHUTDOWN(void);

/* File System */
void FILE_$LOCK_INIT(void);
void FILE_$LOCK(uid_t *uid, void *param2, void *param3,
                void *param4, status_$t *status);
void FILE_$SET_LEN(uid_t *uid, void *len, status_$t *status);
void FILE_$SET_REFCNT(uid_t *uid, void *param, status_$t *status);
void FILE_$PRIV_UNLOCK_ALL(const void *param);

/* Hints */
void HINT_$INIT(void);
void HINT_$INIT_CACHE(void);
void HINT_$ADD_NET(short port);
void HINT_$SHUTDN(void);

/* Naming */
void NAME_$INIT(uid_t *uid1, uid_t *uid2);
void NAME_$SET_WDIR(const char *path, void *param, status_$t *status);

/* Logging/Auditing */
void LOG_$INIT(void);
void LOG_$SHUTDN(void);
void AUDIT_$INIT(void);
void AUDIT_$SHUTDOWN(void);

/* Miscellaneous */
void XPD_$INIT(void);
void PCHIST_$INIT(void);
void PACCT_$INIT(void);
void PACCT_$SHUTDN(void);

/* Calendar */
char CAL_$VERIFY(void *param1, void *param2, void *param3, status_$t *status);
uint32_t CAL_$CLOCK_TO_SEC(void *clock);
void CAL_$SEC_TO_CLOCK(uint32_t *sec, void *clock);
void CAL_$SHUTDOWN(status_$t *status);

/*
 * ============================================================================
 * Utility Functions
 * ============================================================================
 */

void VFMT_$FORMATN(const char *format, char *buf, short *len_ptr, ...);
void CRASH_SHOW_STRING(const char *str);
void CRASH_SYSTEM(status_$t *status);
char MMU_$NORMAL_MODE(void);
char prompt_for_yes_or_no(void);
uint16_t VTOP_OR_CRASH(uint32_t vaddr);
void SUB48(void *a, void *b);
void PRINT_BUILD_TIME(void);
void ERROR_$PRINT(short code, short *param1, short *param2);

/*
 * ============================================================================
 * Internal Helper Functions (FUN_*)
 * ============================================================================
 */

void FUN_00e29138(uint8_t a, uint16_t b, short *out);
void FUN_00e2f1d4(uint16_t param);
void FUN_00e3366c(short cmd, uint32_t param);  /* Diskless helper */
void FUN_00e6d1cc(const char *msg);            /* Display message */
void FUN_00e6d240(uint32_t addr);              /* Initialize page */
void FUN_00e6d254(void *param);                /* Final init */

#endif /* OS_INTERNAL_H */
