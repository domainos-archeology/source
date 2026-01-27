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
#include "cal/cal.h"
#include "misc/misc.h"
#include "network/network.h"
#include "file/file.h"

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
/* MST_$MST_PAGES_LIMIT declared in mst/mst.h */
extern uint32_t AS_$STACK_HIGH;
extern int ROUTE_$PORT;
/* CAL_$BOOT_VOLX is a macro in cal/cal.h */

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
void FIM_$PARITY_TRAP(void);

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
/* status_$cal_refused is a macro in cal/cal.h */
/* status_$pmap_bad_assoc is a macro in ast/ast.h */

/*
 * ============================================================================
 * Subsystem Init Functions
 * ============================================================================
 */

/* Memory Management */
void MST_$PRE_INIT(void);
void AS_$INIT(void);  /* Address Space initialization */
/* MMU_$INIT, MMU_$REMOVE, MMAP_$INIT, MMU_$SET_SYSREV, MMU_$SET_PROT, MMAP_$UNWIRE
 * declared in their respective headers (mmu/mmu.h, mmap/mmap.h) */
/* MST_$INIT, MST_$DISKLESS_INIT, MST_$MAP_CANNED_AT, MST_$ALLOC_ASID
 * declared in mst/mst.h */
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

/* Time - TIME_$INIT declared in time/time.h */

/* UID - UID_$INIT declared in uid/uid.h */

/* Process Management - PROC1/PROC2 functions declared in proc1/proc1.h, proc2/proc2.h */

/* Event Counters */
void EC2_$INIT_S(void);
void *EC2_$REGISTER_EC1(ec_$eventcount_t *ec, status_$t *status);

/* Security */
void ACL_$INIT(void);
void ACL_$ENTER_SUPER(void);
void ACL_$GET_RE_SIDS(void *buf, uid_t *uid, status_$t *status);

/* AST - AST_$INIT, AST_$ACTIVATE_AOTE_CANNED, AST_$PMAP_ASSOC declared in ast/ast.h */

/* Area Management */
void AREA_$INIT(void);
void AREA_$SHUTDOWN(void);

/* Disk/Volume */
void DISK_$INIT(void);
void DBUF_$INIT(void);
void *DBUF_$GET_BLOCK(short volx, short param2, uid_t *uid,
                      short param4, short param5, status_$t *status);
void DBUF_$SET_BUFF(void *buf, short mode, status_$t *status);
void VOLX_$MOUNT(void *boot_device, void *param2, void *param3,
                 void *param4, void *param5, void *param6,
                 uid_t *uid, uid_t *uid2, status_$t *status);
status_$t VOLX_$SHUTDOWN(void);
void VOLX_$REC_ENTRY(void *param1, uid_t *uid);
void VTOCE_$READ(void *param1, void *param2, status_$t *status);

/* Network - NETWORK_$ADD_REQUEST_SERVERS, NETWORK_$SET_SERVICE declared in network/network.h */
void SOCK_$INIT(void);
void NETWORK_$INIT(void);
void NETWORK_$LOAD(void);
void NETWORK_$DISMISS_REQUEST_SERVERS(void);
char NET_IO_$BOOT_DEVICE(short boot_device, short param);
uint32_t RING_$GET_ID(void *param);
void ROUTE_$SHUTDOWN(void);

/* File System - FILE_$LOCK_INIT, FILE_$LOCK, FILE_$SET_LEN, FILE_$SET_REFCNT declared in file/file.h */
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

/* Calendar functions declared in cal/cal.h */

/*
 * ============================================================================
 * Utility Functions
 * ============================================================================
 */

/* VFMT_$FORMATN declared in vfmt/vfmt.h (via misc/misc.h) */
void CRASH_SHOW_STRING(const char *str);
/* CRASH_SYSTEM declared in misc/misc.h */
/* MMU_$NORMAL_MODE declared in mmu/mmu.h */
/* prompt_for_yes_or_no declared in misc/misc.h */
uint16_t VTOP_OR_CRASH(uint32_t vaddr);
/* SUB48 declared in cal/cal.h */
void PRINT_BUILD_TIME(void);
/* ERROR_$PRINT declared in vfmt/vfmt.h (via misc/misc.h) */

/*
 * ============================================================================
 * Internal Helper Functions (FUN_*)
 * ============================================================================
 */

int8_t FUN_00e29138(void *type, void *addr, void *result);
void FUN_00e2f1d4(uint16_t param);
void FUN_00e3366c(short cmd, uint32_t param);  /* Diskless helper */
void FUN_00e6d1cc(const char *msg);            /* Display message */
void FUN_00e6d240(uint32_t addr);              /* Initialize page */
void FUN_00e6d254(void *param);                /* Final init */

#endif /* OS_INTERNAL_H */
