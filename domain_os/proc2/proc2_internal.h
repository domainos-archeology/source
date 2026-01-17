/*
 * proc2/proc2_internal.h - Internal PROC2 Definitions
 *
 * Contains internal functions, data, and types used only within
 * the PROC2 subsystem. External consumers should use proc2/proc2.h.
 */

#ifndef PROC2_INTERNAL_H
#define PROC2_INTERNAL_H

#include "proc2/proc2.h"
#include "ec/ec.h"
#include "fim/fim.h"
#include "uid/uid.h"
#include "acl/acl.h"
#include "mst/mst.h"
#include "dtty/dtty.h"

/*
 * ============================================================================
 * Internal Data Declarations
 * ============================================================================
 */

/* Boot flags */
extern int16_t proc2_boot_flags;

/* System UID */
extern uid_t proc2_system_uid;

/* Event counter arrays for process synchronization */
extern ec_$eventcount_t AS_$CR_REC_FILE_SIZE;   /* Base for CR_REC EC array */
extern ec_$eventcount_t AS_$INIT_STACK_FILE_SIZE; /* Init stack EC array */

/* Stack file limits */
extern uint32_t AS_$STACK_FILE_LOW;

/* FIM initial stack size */
extern uint32_t FIM_$INITIAL_STACK_SIZE;

/*
 * ============================================================================
 * Internal PROC2 Functions
 * ============================================================================
 */

/* Process entry initialization */
void PROC2_$INIT_ENTRY_INTERNAL(proc2_info_t *entry);

/* Cleanup handlers */
void PROC2_$CLEANUP_HANDLERS_INTERNAL(proc2_info_t *entry);

/* Signal delivery */
uint32_t PROC2_$DELIVER_SIGNAL_INTERNAL(int16_t proc_index, int16_t signal,
                                         int32_t param, status_$t *status_ret);

/* Build process info structure */
void PROC2_$BUILD_INFO_INTERNAL(int16_t proc2_index, int16_t proc1_pid,
                                 void *info, status_$t *status_ret);

/* Signal event logging */
void PROC2_$LOG_SIGNAL_EVENT(uint16_t event_type, int16_t target_idx,
                              uint16_t signal, uint32_t param, int32_t success);

/* Debug setup and teardown */
void DEBUG_SETUP_INTERNAL(int16_t target_idx, int16_t debugger_idx, int8_t flag);
void DEBUG_CLEAR_INTERNAL(int16_t proc_idx, int8_t flag);

/* Process group cleanup */
void PGROUP_CLEANUP_INTERNAL(proc2_info_t *entry, int16_t mode);

/* Signal process group internal implementation */
void PROC2_$SIGNAL_PGROUP_INTERNAL(int16_t pgroup_idx, int16_t signal,
                                    uint32_t param, int8_t check_perms,
                                    status_$t *status_ret);

/*
 * ============================================================================
 * Internal Helper Functions (FUN_*)
 * ============================================================================
 */

/* Get next pending signal from process */
int16_t FUN_00e3ef38(proc2_info_t *info);

/* Unknown - called during acknowledge and signal operations */
void FUN_00e0a96c(void);

/* Wait helper - collect child status */
void FUN_00e3fc5c(int16_t child_idx, uint16_t options, int16_t parent_idx,
                  int16_t prev_idx, int8_t *found, uint32_t *result,
                  int16_t *pid_ret);

/* Wait helper - process zombie */
void FUN_00e3fd06(int16_t zombie_idx, uint16_t options,
                  int8_t *found, uint32_t *result, int16_t *pid_ret);

/* Child list manipulation */
void FUN_00e40df4(int16_t child_idx, int16_t prev_sibling_idx);

/* UPID to pgroup index lookup */
int16_t FUN_00e42224(int16_t upid);

/* Session setup helper */
void FUN_00e41e86(proc2_info_t *info, int16_t session_id, status_$t *status);

/* Unknown pgroup helper */
void FUN_00e420b8(proc2_info_t *info, int16_t param);

/*
 * ============================================================================
 * External Module Functions - FIM (not in fim.h)
 * ============================================================================
 */

void FIM_$FP_INIT(uint16_t asid);
void FIM_$PROC2_STARTUP(void *context);
void FIM_$DELIVER_TRACE_FAULT(uint16_t asid);
void FIM_$FAULT_RETURN(void *context, void *param2, void *param3);
void FIM_$POP_SIGNAL(void *context);

/*
 * ============================================================================
 * External Module Functions - AUDIT
 * ============================================================================
 */

void AUDIT_$INHERIT_AUDIT(int16_t *pid_ptr, int16_t *status_ptr);
void AUDIT_$LOG_EVENT(void *event_header, uint16_t *success_flag,
                       void *success, void *param, void *extra);

/*
 * ============================================================================
 * External Module Functions - XPD (Extended Ptrace/Debug)
 * ============================================================================
 */

int8_t XPD_$INHERIT_PTRACE_OPTIONS(int16_t entry_offset);
void XPD_$CAPTURE_FAULT(void *param1, void *param2,
                        void *param3, void *param4);

/*
 * ============================================================================
 * External Module Functions - MSG
 * ============================================================================
 */

int8_t MSG_$FORK(uint16_t param1, void *asid_ptr);

/*
 * ============================================================================
 * External Module Functions - PCHIST
 * ============================================================================
 */

void PCHIST_$UNIX_PROFIL_FORK(void *pid_ptr, void *asid_ptr);

/*
 * ============================================================================
 * External Module Functions - Boot
 * ============================================================================
 */

int8_t OS_$BOOT_ERRCHK(char *msg1, char *msg2, uint16_t *param, status_$t *status_ret);
int8_t TAPE_$BOOT(status_$t *status_ret);
int8_t FLOP_$BOOT(status_$t *status_ret, status_$t *status_ret2);

#endif /* PROC2_INTERNAL_H */
