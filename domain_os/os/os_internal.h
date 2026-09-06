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
#include "acl/acl.h"
#include "name/name.h"
#include "route/route.h"
#include "io/io.h"
#include "disk/disk.h"      /* DISK_$INIT, DISK_$DO_CHKSUM, status_$disk_needs_salvaging */
#include "pmap/pmap.h"      /* PMAP_$SHUTTING_DOWN_FLAG, PMAP_$PURIFIER_L/R */
#include "rgyc/rgyc.h"      /* RGYC_$G_LOCKSMITH_UID */
#include "as/as.h"          /* AS_$INIT, AS_$STACK_HIGH */
#include "fim/fim.h"        /* FIM_$BUS_ERR, FIM_$PARITY_TRAP */
#include "dxm/dxm.h"        /* DXM_$INIT, DXM_$HELPER_WIRED/UNWIRED */
#include "fp/fp.h"          /* FP_$SAVEP */
#include "peb/peb.h"        /* PEB_$INIT, PEB_$LOAD_WCS */
#include "term/term.h"      /* TERM_$INIT */
#include "dtty/dtty.h"      /* DTTY_$INIT */
#include "smd/smd.h"        /* SMD_$INIT, SMD_$INIT_BLINK, SMD_$INQ_DISP_TYPE */
#include "tpad/tpad.h"      /* TPAD_$INIT */
#include "ec/ec.h"          /* EC2_$INIT_S, EC2_$REGISTER_EC1 */
#include "area/area.h"      /* AREA_$INIT, AREA_$SHUTDOWN */
#include "dbuf/dbuf.h"      /* DBUF_$INIT, DBUF_$GET_BLOCK, DBUF_$SET_BUFF */
#include "volx/volx.h"      /* VOLX_$MOUNT, VOLX_$SHUTDOWN, VOLX_$REC_ENTRY */
#include "vtoc/vtoc.h"      /* VTOCE_$READ */
#include "sock/sock.h"      /* SOCK_$INIT */
#include "net_io/net_io.h"  /* NET_IO_$BOOT_DEVICE */
#include "ring/ring.h"      /* RING_$GET_ID */
#include "hint/hint.h"      /* HINT_$INIT, HINT_$INIT_CACHE, HINT_$ADD_NET, HINT_$SHUTDN */
#include "log/log.h"        /* LOG_$INIT, LOG_$SHUTDN */
#include "audit/audit.h"    /* AUDIT_$INIT, AUDIT_$SHUTDOWN */
#include "xpd/xpd.h"        /* XPD_$INIT */
#include "pchist/pchist.h"  /* PCHIST_$INIT */
#include "pacct/pacct.h"    /* PACCT_$INIT, PACCT_$SHUTDN */
#include "prom/prom.h"      /* io_$probe */

/*
 * Well-known UIDs (OS_WIRED_$UID, DISPLAY1_$UID, LV_LABEL_$UID) come from
 * uid/uid.h; ACL_$FNDWRX from acl/acl.h; RGYC_$G_LOCKSMITH_UID from
 * rgyc/rgyc.h; NAME_$NODE_UID from name/name.h.
 *
 * DISK_$DO_CHKSUM is in disk/disk.h, PMAP_$SHUTTING_DOWN_FLAG in pmap/pmap.h,
 * MST_$MST_PAGES_LIMIT in mst/mst.h, AS_$STACK_HIGH is a macro in as/as.h,
 * ROUTE_$PORT is in route/route.h, CAL_$BOOT_VOLX is a macro in cal/cal.h.
 */

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
/* FIM_$BUS_ERR and FIM_$PARITY_TRAP: see fim/fim.h */

/*
 * Daemon entry points PMAP_$PURIFIER_L/R (pmap/pmap.h) and
 * DXM_$HELPER_WIRED/UNWIRED (dxm/dxm.h) are declared by their owners.
 */

/*
 * ============================================================================
 * Global Data - Shutdown Wiring Info
 * ============================================================================
 */

/* FP_$SAVEP: see fp/fp.h */
extern m68k_ptr_t PTR_OS_PROC_SHUTWIRED;
extern m68k_ptr_t PTR_OS_PROC_SHUTWIRED_END;
/* PTR_OS_DATA_SHUTWIRED is declared in os/os.h (shared with stop/) */
extern m68k_ptr_t PTR_OS_DATA_SHUTWIRED_END;

/*
 * ============================================================================
 * Global Data - Status Constants
 * ============================================================================
 */

extern status_$t No_err;
extern status_$t No_calendar_on_system_err;
extern status_$t OS_BAT_disk_needs_salvaging_err;
/* status_$disk_needs_salvaging is a macro in disk/disk.h */
/* status_$cal_refused is a macro in cal/cal.h */
/* status_$pmap_bad_assoc is a macro in ast/ast.h */

/*
 * ============================================================================
 * Subsystem Init/Shutdown Functions
 * ============================================================================
 *
 * All of the subsystem entry points called by OS_$INIT and OS_$SHUTDOWN are
 * declared by their owning subsystem's public header (included above):
 *
 *   mst/mst.h        MST_$PRE_INIT, MST_$INIT, MST_$DISKLESS_INIT,
 *                    MST_$MAP_CANNED_AT, MST_$ALLOC_ASID
 *   as/as.h          AS_$INIT
 *   mmu/mmu.h        MMU_$INIT, MMU_$REMOVE, MMU_$SET_SYSREV, MMU_$SET_PROT
 *   mmap/mmap.h      MMAP_$INIT, MMAP_$UNWIRE
 *   peb/peb.h        PEB_$INIT, PEB_$LOAD_WCS
 *   dxm/dxm.h        DXM_$INIT
 *   io/io.h          IO_$INIT, IO_$GET_DCTE
 *   term/term.h      TERM_$INIT
 *   dtty/dtty.h      DTTY_$INIT
 *   smd/smd.h        SMD_$INIT, SMD_$INIT_BLINK, SMD_$INQ_DISP_TYPE
 *   tpad/tpad.h      TPAD_$INIT
 *   time/time.h      TIME_$INIT
 *   uid/uid.h        UID_$INIT
 *   proc1/proc1.h    PROC1_$INIT, PROC1_$CREATE_P, ...
 *   proc2/proc2.h    PROC2_$INIT, PROC2_$SHUTDOWN
 *   ec/ec.h          EC2_$INIT_S, EC2_$REGISTER_EC1
 *   acl/acl.h        ACL_$INIT, ACL_$ENTER_SUPER
 *   ast/ast.h        AST_$INIT, AST_$ACTIVATE_AOTE_CANNED, AST_$PMAP_ASSOC
 *   area/area.h      AREA_$INIT, AREA_$SHUTDOWN
 *   disk/disk.h      DISK_$INIT
 *   dbuf/dbuf.h      DBUF_$INIT, DBUF_$GET_BLOCK, DBUF_$SET_BUFF
 *   volx/volx.h      VOLX_$MOUNT, VOLX_$SHUTDOWN, VOLX_$REC_ENTRY
 *   vtoc/vtoc.h      VTOCE_$READ
 *   sock/sock.h      SOCK_$INIT
 *   network/network.h NETWORK_$INIT, NETWORK_$LOAD, NETWORK_$ADD_REQUEST_SERVERS,
 *                    NETWORK_$DISMISS_REQUEST_SERVERS, NETWORK_$SET_SERVICE,
 *                    network_$fetch_diskless_info
 *   net_io/net_io.h  NET_IO_$BOOT_DEVICE
 *   ring/ring.h      RING_$GET_ID
 *   route/route.h    ROUTE_$SHUTDOWN
 *   file/file.h      FILE_$LOCK_INIT, FILE_$LOCK, FILE_$SET_LEN,
 *                    FILE_$SET_REFCNT, FILE_$PRIV_UNLOCK_ALL
 *   hint/hint.h      HINT_$INIT, HINT_$INIT_CACHE, HINT_$ADD_NET, HINT_$SHUTDN
 *   name/name.h      NAME_$INIT, NAME_$SET_WDIR
 *   log/log.h        LOG_$INIT, LOG_$SHUTDN
 *   audit/audit.h    AUDIT_$INIT, AUDIT_$SHUTDOWN
 *   xpd/xpd.h        XPD_$INIT
 *   pchist/pchist.h  PCHIST_$INIT
 *   pacct/pacct.h    PACCT_$INIT, PACCT_$SHUTDN
 *   cal/cal.h        CAL_$VERIFY, CAL_$SHUTDOWN, ...
 */

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

/* io_$probe: see prom/prom.h */
void FUN_00e2f1d4(uint16_t param);
/* network_$fetch_diskless_info: see network/network.h (2=time, 8=tz, 0x37=route) */
void OS_$PRINT_INIT_ERROR(const char *msg);            /* Display message */
void os_$free_va_page(uint32_t vaddr);          /* Free page at virtual address */
void os_$start_proc2(void *param);             /* Free init pages and start proc2 */

#endif /* OS_INTERNAL_H */
