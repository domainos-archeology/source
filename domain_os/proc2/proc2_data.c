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

/* Base address for index calculations (table_base - entry_size) */
proc2_info_t *P2_INFO_TABLE;

/* Allocation pointer (index of first allocated entry) */
uint16_t P2_INFO_ALLOC_PTR;

/* Free list head (index of first free entry) */
uint16_t P2_FREE_LIST_HEAD;

/* Mapping table: PROC1 PID -> PROC2 index (at 0xEA551C + 0x3EB6) */
uint16_t *P2_PID_TO_INDEX_TABLE;

/* Process group table (8-byte entries at 0xEA551C + 0x3F30) */
pgroup_entry_t *PGROUP_TABLE;

/* Process UID storage */
uid_t PROC2_UID;
