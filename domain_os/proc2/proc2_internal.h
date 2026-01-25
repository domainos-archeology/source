/*
 * proc2/proc2_internal.h - Internal PROC2 Definitions
 *
 * Contains internal functions, data, and types used only within
 * the PROC2 subsystem. External consumers should use proc2/proc2.h.
 */

#ifndef PROC2_INTERNAL_H
#define PROC2_INTERNAL_H

#include "proc2/proc2.h"
#include "proc1/proc1.h"
#include "ec/ec.h"
#include "fim/fim.h"
#include "uid/uid.h"
#include "acl/acl.h"
#include "mst/mst.h"
#include "dtty/dtty.h"
#include "name/name.h"
#include "audit/audit.h"
#include "os/os.h"
#include "tape/tape.h"
#include "flop/flop.h"
#include "mmu/mmu.h"
#include "xpd/xpd.h"
#include "file/file_internal.h"
#include "pchist/pchist.h"
#include "msg/msg.h"

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
void DEBUG_UNLINK_FROM_LIST(int16_t proc_idx);
void DEBUG_SETUP_INTERNAL(int16_t target_idx, int16_t debugger_idx, int8_t flag);
void DEBUG_CLEAR_INTERNAL(int16_t proc_idx, int8_t flag);

/*
 * ============================================================================
 * Process Group Internal Functions
 * ============================================================================
 */

/* Find pgroup by UPGID - returns index (1-69) or 0 if not found */
int16_t PGROUP_FIND_BY_UPGID(uint16_t upgid);

/* Process group cleanup - mode: 0=leader only, 1=refcount only, 2=both */
void PGROUP_CLEANUP_INTERNAL(proc2_info_t *entry, int16_t mode);

/* Set process's process group */
void PGROUP_SET_INTERNAL(proc2_info_t *entry, uint16_t new_upgid, status_$t *status_ret);

/* Decrement pgroup leader count - signals orphaned group if count reaches 0 */
void PGROUP_DECR_LEADER_COUNT(int16_t pgroup_idx);

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


#endif /* PROC2_INTERNAL_H */
