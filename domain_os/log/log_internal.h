/*
 * LOG - System Error Log Subsystem - Internal Header
 *
 * Internal definitions for the log subsystem: the on-disk buffer layout,
 * the two low-memory records that survive a crash, the constant cells in
 * the LOG_ code segments, and the module-local routines.
 *
 * Map: `I E1758C LOG_ size = 2C4` (LOG_$SHUTDN, LOG_$UPDATE, LOG_$ADD,
 * log_$read_internal, LOG_$READ, LOG_$READ2), `I E2FF7C LOG_ size = 2EC`
 * (log_$check_op_status, LOG_$INIT and their strings/cells), `D E2B280 LOG_
 * size = 1C` (LOG_$STATE), and at the very start of the image `D E00000
 * CRASH_RECORD size = C` (CRASH_$RECORD) followed by LOG_$LAST_ENTRY at
 * 0xE0000C.
 */

#ifndef LOG_INTERNAL_H
#define LOG_INTERNAL_H

#include "log/log.h"
#include "ml/ml.h"
#include "time/time.h"
#include "vfmt/vfmt.h"
#include "wp/wp.h"

/*
 * Log buffer header structure
 *
 * The log buffer (one 0x400-byte page) is an array of words: word 0 is the
 * index of the oldest entry, word 1 the index at which the next entry is
 * written; entries follow from word 1 on, so entry index i lives at
 * buf[i + 1].
 */
typedef struct log_buffer_header_t {
    int16_t     head;               /* 0x00: index of the oldest entry */
    int16_t     tail;               /* 0x02: index of the next free slot */
    /* Entry data follows */
} log_buffer_header_t;

_Static_assert(__builtin_offsetof(log_buffer_header_t, head) == 0x00, "log_buffer_header_t.head");
_Static_assert(__builtin_offsetof(log_buffer_header_t, tail) == 0x02, "log_buffer_header_t.tail");

/*
 * Log entry header structure
 *
 * Each log entry has a fixed header followed by variable-length data.
 */
typedef struct log_entry_header_t {
    int16_t     size;               /* 0x00: entry size in words (including header) */
    int16_t     type;               /* 0x02: entry type code */
    uint32_t    timestamp;          /* 0x04: TIME_$CURRENT_CLOCKH at LOG_$ADD */
    /* Entry data follows at offset 0x08 */
} log_entry_header_t;

_Static_assert(__builtin_offsetof(log_entry_header_t, size) == 0x00, "log_entry_header_t.size");
_Static_assert(__builtin_offsetof(log_entry_header_t, type) == 0x02, "log_entry_header_t.type");
_Static_assert(__builtin_offsetof(log_entry_header_t, timestamp) == 0x04, "log_entry_header_t.timestamp");

/* =============================================================================
 * The two records at the start of the image (0x00E00000, 0x00E0000C)
 *
 * Both are flagged with LOG_PENDING_MAGIC.  CRASH_$RECORD is filled in by
 * the crash path and turned into a type-5 entry by LOG_$INIT; LOG_$LAST_ENTRY
 * is a copy of the entry LOG_$ADD most recently wrote (0x00E176FC-0x00E17742),
 * so that the last thing logged survives a crash even if the page did not
 * reach the disk, and LOG_$INIT re-adds it when the magic is set.
 * =============================================================================
 */

/* 0x00E00000, map `D E00000 CRASH_RECORD size = C` */
typedef struct crash_$record_t {
    uint32_t    magic;              /* 0x00: LOG_PENDING_MAGIC if valid */
    uint8_t     data[8];            /* 0x04: the 8 bytes LOG_$INIT logs as type 5 */
} crash_$record_t;

_Static_assert(__builtin_offsetof(crash_$record_t, magic) == 0x00, "crash_$record_t.magic");
_Static_assert(__builtin_offsetof(crash_$record_t, data) == 0x04, "crash_$record_t.data");
_Static_assert(sizeof(crash_$record_t) == 0x0C, "crash_$record_t: map size C");

/* 0x00E0000C, map LOG_$LAST_ENTRY.  The header mirrors a log entry, shifted
 * by the magic longword; the data words follow at +0x0C and LOG_$ADD copies
 * up to LOG_MAX_ENTRY_WORDS - 4 of them. */
typedef struct log_$last_entry_t {
    uint32_t    magic;              /* 0x00: LOG_PENDING_MAGIC if valid */
    int16_t     size;               /* 0x04: entry size in words */
    int16_t     type;               /* 0x06: entry type */
    uint32_t    timestamp;          /* 0x08: entry timestamp */
    int16_t     data[LOG_MAX_ENTRY_WORDS - 4];  /* 0x0C: entry data words */
} log_$last_entry_t;

_Static_assert(__builtin_offsetof(log_$last_entry_t, magic) == 0x00, "log_$last_entry_t.magic");
_Static_assert(__builtin_offsetof(log_$last_entry_t, size) == 0x04, "log_$last_entry_t.size");
_Static_assert(__builtin_offsetof(log_$last_entry_t, type) == 0x06, "log_$last_entry_t.type");
_Static_assert(__builtin_offsetof(log_$last_entry_t, timestamp) == 0x08, "log_$last_entry_t.timestamp");
_Static_assert(__builtin_offsetof(log_$last_entry_t, data) == 0x0C, "log_$last_entry_t.data");

extern crash_$record_t   CRASH_$RECORD;     /* 0x00E00000 */
extern log_$last_entry_t LOG_$LAST_ENTRY;   /* 0x00E0000C */

/* =============================================================================
 * Global State
 * =============================================================================
 */

/* LOG_$STATE, LOG_$LOGFILE_PTR are declared in log/log.h (pmap pokes
 * LOG_$LOGFILE_PTR directly) */

/* 0x00E2B280: the log file's UID, the first field of LOG_$STATE */
#define LOG_$LOGFILE_UID        (LOG_$STATE.logfile_uid)

/* =============================================================================
 * Constant cells in the LOG_ code segment at 0x00E2FF7C (`gsk read`)
 *
 * Defined in log_data.c.  Each is ONE object in the image that several
 * routines reach with `pea (d,PC)`; the sharing is what matters, so they are
 * module globals rather than per-file copies.
 * =============================================================================
 */

/* 0x00E2FFFC: 00 00 00 00.  The zero longword log_$check_op_status hands
 * VFMT_$WRITE10 as a placeholder argument and LOG_$INIT hands LOG_$ADD as
 * the (zero-length) data of the init entry. */
extern uint32_t LOG_$VFMT_NO_ARG;

/* 0x00E30020: "`node_data/system_logs/sys_error_log" NUL - 36 characters
 * (the leading "`" is the node-entry-directory prefix).  NAME_$RESOLVE and
 * NAME_$CR_FILE get it from LOG_$INIT, VFMT_$WRITE10 from
 * log_$check_op_status. */
extern char log_$logfile_path[];

/* 0x00E30044: 00 00 00 24 - the path length as a LONG, for VFMT's %a. */
extern int32_t log_$logfile_path_len_l;

/* 0x00E3022A: 00 24 - the path length as a WORD, for the NAME_ calls. */
extern int16_t log_$logfile_path_len;

/* 0x00E30238: 00 00 - FILE_$LOCK's lock index */
extern uint16_t log_$lock_index;
/* 0x00E3023A: 00 04 - FILE_$LOCK's lock mode */
extern uint16_t log_$lock_mode;
/* 0x00E3023C: 00 00 - FILE_$LOCK's rights */
extern uint8_t log_$lock_rights;

/* =============================================================================
 * Internal Functions
 * =============================================================================
 */

/*
 * log_$check_op_status (0x00E2FF7C) - nested procedure of LOG_$INIT
 *
 * When the HIGH word of LOG_$INIT's status is non-zero, prints
 * "Warning: Status <status>, Unable to <op> <path> -- error logging
 * disabled." as three chained VFMT_$WRITE10 calls and returns Domain true;
 * otherwise false.  `status` is the uplevel variable reached through the
 * static link (`movea.l (A6),A2` / `(-0x44,A2)`), made explicit.
 */
int8_t log_$check_op_status(const char *op, status_$t *status);

/*
 * log_$read_internal (0x00E17778) - the body LOG_$READ / LOG_$READ2 gate to
 */
void log_$read_internal(void *buffer, uint16_t offset, uint16_t max_len, uint16_t *actual_len);

#endif /* LOG_INTERNAL_H */
