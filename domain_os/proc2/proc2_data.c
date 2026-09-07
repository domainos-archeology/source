/*
 * proc2_data.c - PROC2 Global Data Definitions
 *
 * This file defines the global variables used by the process management
 * subsystem. On the original M68K hardware, these were at fixed addresses.
 * For portability, we define them as regular variables here.
 *
 */

#include "proc2/proc2_internal.h"

status_$t PROC2_Internal_Error = status_$proc2_internal_error;

/* Base address for index calculations (table_base - entry_size) */
proc2_info_t *P2_INFO_TABLE;

/* Allocation pointer (index of first allocated entry) */
uint16_t P2_INFO_ALLOC_PTR;

/* Free list head (index of first free entry) */
uint16_t P2_FREE_LIST_HEAD;

/*
 * Rolling UPID allocator (0xE7C06A).  Initialised data in the image: the
 * word at 0xE7C06A reads 0x0041, the same value the wrap at 0x00E7333C
 * resets it to.
 */
uint16_t PROC2_$NEXT_UPID = P2_UPID_WRAP_TO;

/* Mapping table: PROC1 PID -> PROC2 index (at 0xEA551C + 0x3EB6) */
uint16_t *P2_PID_TO_INDEX_TABLE;

/* Process group table (8-byte entries at 0xEA551C + 0x3F30) */
pgroup_entry_t *PGROUP_TABLE;

/* Per-ASID process UID table (0xE7BE94) */
uid_t PROC2_UID[PROC2_UID_TABLE_SIZE];

/* UID of /node_data/proc_dir (0xE7BE84, DAT_00e7be84) */
uid_t proc2_proc_dir_uid;

/* System process UID (0xE7BE8C, DAT_00e7be8c) */
uid_t proc2_system_uid;

/* Boot flags word (0xE7C068, DAT_00e7c068) */
int16_t proc2_boot_flags;

/* Per-process fork / creation record eventcounts (0xE2B978, PROC2_$EC) */
proc2_ec_entry_t PROC2_$EC[PROC2_EC_ENTRIES];
