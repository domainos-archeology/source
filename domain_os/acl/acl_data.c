/*
 * ACL Data - Global variables for ACL subsystem
 *
 * Original m68k addresses documented in comments.
 * A5-relative base: 0xE7CF54
 */

#include "acl/acl_internal.h"

/*
 * Per-process SID arrays (indexed by PID, stride 0x24 = 36 bytes)
 */

/* Current SIDs: 0xE90D10 */
acl_sid_block_t ACL_$CURRENT_SIDS[PROC1_MAX_PROCESSES];

/* Saved SIDs (pre-enter_super): 0xE91610 */
acl_sid_block_t ACL_$SAVED_SIDS[PROC1_MAX_PROCESSES];

/* Original SIDs (pre-enter_subs): 0xE90410 */
acl_sid_block_t ACL_$ORIGINAL_SIDS[PROC1_MAX_PROCESSES];

/*
 * Project lists metadata (indexed by PID, stride 0x0C = 12 bytes)
 */
acl_proj_list_t ACL_$PROJ_LISTS[PROC1_MAX_PROCESSES];   /* 0xE92228 */
acl_proj_list_t ACL_$SAVED_PROJ[PROC1_MAX_PROCESSES];   /* 0xE91F28 */

/*
 * Per-process project UID array (indexed by PID, 8 UIDs per process)
 * Stride 0x40 = 64 bytes per process
 */
uid_t ACL_$PROJ_UIDS[PROC1_MAX_PROCESSES][ACL_MAX_PROJECTS]; /* 0xE924F4 */

/*
 * Per-process subsystem level counter (indexed by PID, stride 2)
 * 0xE9353A
 */
int16_t ACL_$SUBSYS_LEVEL[PROC1_MAX_PROCESSES];

/*
 * Per-process superuser mode counter (indexed by PID, stride 2)
 * 0xE7DACA (A5+0xB76)
 */
int16_t ACL_$SUPER_COUNT[PROC1_MAX_PROCESSES];

/*
 * ASID bitmaps (8 bytes each, 64 bits for 64 ASIDs)
 */
uint8_t ACL_$ASID_FREE_BITMAP[8];   /* 0xE92534: 1=free */
uint8_t ACL_$ASID_SUSER_BITMAP[8];  /* 0xE935C4: 1=used suser */

/*
 * Locksmith state
 */
int16_t ACL_$LOCAL_LOCKSMITH;       /* 0xE7DAC4 (A5+0xB70) */
int16_t ACL_$LOCKSMITH_OWNER_PID;   /* 0xE7DAC6 (A5+0xB72) */
int8_t ACL_$LOCKSMITH_OVERRIDE;     /* 0xE7DB4C (A5+0xBF8) */

/*
 * Subsystem entry magic value
 */
int32_t ACL_$SUBS_MAGIC;            /* 0xE7DAC0 (A5+0xB6C) */

/*
 * Exclusion lock for ACL operations
 */
ml_$exclusion_t ACL_$EXCLUSION_LOCK; /* 0xE2C014 */

/*
 * Default ACL UIDs
 */
uid_t ACL_$DNDCAL;  /* 0xE174DC: Default ACL for dirs/links */
uid_t ACL_$FNDWRX;  /* 0xE174C4: Default ACL for files */

/*
 * ACL type UIDs - well-known UIDs used to identify ACL operation types
 */
uid_t ACL_$FILEIN_ACL;      /* 0xE17454: {0x00000602, 0x00000000} */
uid_t ACL_$DIRIN_ACL;       /* 0xE1745C: {0x00000603, 0x00000000} */
uid_t ACL_$DIR_MERGE_ACL;   /* 0xE17464: {0x00000604, 0x00000000} */
uid_t ACL_$FILE_MERGE_ACL;  /* 0xE1746C: {0x00000605, 0x00000000} */
uid_t ACL_$FILE_SUBS_ACL;   /* 0xE17474: {0x00000606, 0x00000000} */
uid_t ACL_$DIR_ACL;         /* Well-known ACL UID for directories (TODO: find actual address) */
