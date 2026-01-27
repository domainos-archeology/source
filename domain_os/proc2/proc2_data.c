/*
 * proc2_data.c - PROC2 Global Data Definitions
 *
 * This file defines the global variables used by the process management
 * subsystem. On the original M68K hardware, these were at fixed addresses.
 * For portability, we define them as regular variables here.
 *
 */

#include "proc2_internal.h"

status_$t PROC2_Internal_Error = status_$proc2_internal_error;