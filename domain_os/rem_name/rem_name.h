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
 * Note: REM_NAME_$* functions are defined in name/rem_name.c and declared
 * in name/name.h.  REM_NAME_$REGISTER_SERVER (0xE4A4AE) takes no
 * parameters; its callers in rip/server.c push two ignored arguments.
 */

#endif /* REM_NAME_H */
