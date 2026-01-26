/*
 * rem_name/rem_name.h - Remote Naming Service Functions
 *
 * This header provides the remote naming service API.
 * The actual functions are declared in name/name.h, this is a
 * convenience header for backward compatibility.
 */

#ifndef REM_NAME_H
#define REM_NAME_H

#include "name/name.h"

/*
 * Note: REM_NAME_$* functions are declared in name/name.h
 *
 * There appears to be a signature conflict between:
 *   - name/name.h: void REM_NAME_$REGISTER_SERVER(void)
 *   - rip/server.c: uint16_t REM_NAME_$REGISTER_SERVER(void *, void *)
 *
 * This needs to be resolved by examining the Ghidra disassembly
 * to determine the correct signature.
 */

#endif /* REM_NAME_H */
