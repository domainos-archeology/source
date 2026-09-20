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
 * Frame (0x00E32734, 16 bytes): +0x08 entry_point (cleared with
 * `clr.l (A0)`), +0x0C status_ret (never touched).  Returns a Domain
 * boolean in D0b, always FALSE (`clr.b D0b`).  PROC2_$INIT pushes both
 * arguments (0x00E306C4/0x00E306C6) and cleans up 8 bytes.
 *
 * @param entry_point  Output: cleared
 * @param status_ret   Unused
 * @return 0 (false) - tape boot not supported in this configuration
 */
int8_t TAPE_$BOOT(uint32_t *entry_point, status_$t *status_ret);

#endif /* TAPE_H */
