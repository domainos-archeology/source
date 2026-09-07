/*
 * svc/svc_internal.h - System Call (SVC) Subsystem Internal Definitions
 *
 * Declarations used only within svc/ (the trap dispatch tables and the
 * assembly-language error handlers in svc/sau2/).  External consumers
 * should use svc/svc.h.
 */

#ifndef SVC_INTERNAL_H
#define SVC_INTERNAL_H

#include "svc/svc.h"

/*
 * Error handlers (assembly, svc/sau2/trap5.s)
 */
void SVC_$INVALID_SYSCALL(void);   /* Invalid syscall number */
void SVC_$UNIMPLEMENTED(void);     /* Unimplemented syscall */

#endif /* SVC_INTERNAL_H */
