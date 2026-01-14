/*
 * TAPE - Tape device support
 *
 * This module provides tape device boot and access functionality.
 * In this version of Domain/OS, tape boot support appears to be
 * stubbed out (always returns false/no tape boot).
 */

#ifndef TAPE_H
#define TAPE_H

#include "base/base.h"

/*
 * TAPE_$BOOT - Check if system booted from tape
 *
 * @param status_ret  Output: status code (always status_$ok)
 * @return 0 (false) - tape boot not supported in this configuration
 */
uint8_t TAPE_$BOOT(status_$t *status_ret);

#endif /* TAPE_H */
