/*
 * DXM - Deferred Execution Manager
 *
 * This module provides deferred/callback execution services.
 */

#ifndef DXM_H
#define DXM_H

#include "base/base.h"

/*
 * DXM callback queue for unwired (non-locked) callbacks
 */
extern void *DXM_$UNWIRED_Q;

/*
 * DXM_$ADD_CALLBACK - Add a callback to a deferred execution queue
 *
 * @param queue     Queue to add callback to
 * @param callback  Callback function pointer
 * @param param     Parameter to pass to callback
 * @param flags     Callback flags
 * @param status    Output status code
 */
void DXM_$ADD_CALLBACK(void *queue, void *callback, void *param,
                       uint32_t flags, status_$t *status);

#endif /* DXM_H */
