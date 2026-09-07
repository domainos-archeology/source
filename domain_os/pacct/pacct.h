/*
 * PACCT - Process Accounting Subsystem
 *
 * This module provides Unix-style process accounting. When enabled,
 * it writes an accounting record for each terminated process to a
 * designated accounting file.
 *
 * Accounting records include:
 *   - User/group/org SIDs
 *   - CPU time (user + system)
 *   - Elapsed time
 *   - Memory usage (average)
 *   - I/O counts
 *   - Process UID and command name
 *   - Exit status flags
 *
 * The accounting file is memory-mapped for efficient writes. Records
 * are 128 bytes (0x80) each.
 *
 * Access Control:
 *   Starting and stopping accounting requires locksmith (superuser)
 *   privileges.
 *
 * Memory Layout (m68k):
 *   Accounting state block: 0xE817EC (32 bytes)
 */

#ifndef PACCT_H
#define PACCT_H

#include "base/base.h"
#include "acl/acl.h"   /* status_$insufficient_rights_to_perform_operation */

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Process accounting record size */
#define PACCT_RECORD_SIZE       0x80    /* 128 bytes per record */

/* Mapped buffer size */
#define PACCT_BUFFER_SIZE       0x8000  /* 32KB mapping */

/* Status codes */
/* status_$insufficient_rights_to_perform_operation is a module-0x23 code
 * defined in acl/acl.h (included above -- bead source-3uo). */

/*
 * ============================================================================
 * Types
 * ============================================================================
 */

/*
 * Compressed accounting value (comp_t)
 *
 * Used for CPU times and other large values. Format:
 *   bits 0-12:  13-bit mantissa
 *   bits 13-15: 3-bit exponent (multiply mantissa by 8^exp)
 *
 * This allows representing values up to ~2^37 in 16 bits.
 */
typedef uint16_t comp_t;

/*
 * The 36-byte SID block ACL_$GET_RE_ALL_SIDS (0x00E48792) writes through
 * each of its first two arguments: nine longwords copied from
 * 0x00E90410 + PROC1_$CURRENT*0x24 (0x00E487C2).
 */
typedef struct pacct_sid_block_t {
    uid_t       user_sid;       /* 0x00: User SID */
    uid_t       group_sid;      /* 0x08: Group SID */
    uid_t       org_sid;        /* 0x10: Org SID */
    uid_t       login_sid;      /* 0x18: Login SID */
    uint32_t    field_20;       /* 0x20: the ninth longword the callee writes */
} __attribute__((packed)) pacct_sid_block_t;

_Static_assert(sizeof(pacct_sid_block_t) == 0x24, "pacct_sid_block_t size");

/*
 * The 12-byte block ACL_$GET_RE_ALL_SIDS writes through each of its third
 * and fourth arguments: three longwords from 0x00E91F28 + PROC1_$CURRENT*0x0C
 * (0x00E487F8).
 */
typedef struct pacct_prot_block_t {
    uid_t       prot_uid;       /* 0x00: Protection UID */
    uint32_t    field_08;       /* 0x08: the third longword the callee writes */
} __attribute__((packed)) pacct_prot_block_t;

_Static_assert(sizeof(pacct_prot_block_t) == 0x0C, "pacct_prot_block_t size");

/*
 * Process accounting record structure
 * Size: 128 bytes (0x80)
 *
 * This is written to the accounting file for each terminated process.
 *
 * Recovered from PACCT_$LOG (0x00E5AA9C), which builds the record in the
 * frame at A6-0x190 and then copies 32 longwords out of it
 * (0x00E5ACEE `lea (-0x190,A6),A0` / `moveq #0x1f,D1` / `move.l (A0)+,(A1)+`).
 * Every displacement below is (record offset - 0x190) in that function.
 *
 * PACKED: m68k aligns 32-bit fields to 2 bytes, so the recovered offsets are
 * only reproducible on a 4/8-byte-aligning host if the record is packed.
 * Packing changes no m68k layout.  The record is genuinely misaligned:
 * ac_zero_42 is a longword on a 2-byte boundary and ac_proc_uid straddles
 * 0x4A..0x51.
 */
typedef struct pacct_record_t {
    uint16_t    ac_flags;       /* 0x00: cleared at 0x00E5AAFC `clr.w`; bit 0
                                 * from arg1's sign bit (0x00E5AB08 byte ops on
                                 * A6-0x18F, the word's LOW byte), bit 1 from
                                 * arg2's (0x00E5AB1A). */
    uint8_t     ac_stat;        /* 0x02: 0x00E5AB2A `move.b (0x1,A0),(-0x18e,A6)`
                                 * -- the low byte of the exit-status word. */
    uint8_t     ac_pad_03;      /* 0x03: never written; leaks stack. */
    pacct_sid_block_t ac_sids;  /* 0x04: 0x00E5AB30-0x00E5AB3E, nine longwords
                                 * from ACL_$GET_RE_ALL_SIDS' FIRST argument. */
    pacct_prot_block_t ac_prot; /* 0x28: 0x00E5AB42-0x00E5AB4E, three longwords
                                 * from ACL_$GET_RE_ALL_SIDS' THIRD argument
                                 * (A6-0xC0), not the second. */
    uint32_t    ac_devno;       /* 0x34: cleared at 0x00E5AB50 `clr.l`, then
                                 * 0x00E5AC5C `move.l D1,(-0x15c,A6)` with D1 =
                                 * -1 on a failed FILE_$GET_ATTR_INFO or the
                                 * zero-extended word at compact-record +0x32. */
    int32_t     ac_btime;       /* 0x38: 0x00E5ABBA `move.l D0,(-0x158,A6)` --
                                 * CAL_$CLOCK_TO_SEC(start_clock) + 0x12CEA600. */
    comp_t      ac_utime;       /* 0x3C: 0x00E5AB96 -- compress(proc_times+0x08). */
    comp_t      ac_stime;       /* 0x3E: 0x00E5ABA6 -- compress(proc_times+0x0C). */
    comp_t      ac_etime;       /* 0x40: 0x00E5ABD6 -- pacct_$clock_to_comp of
                                 * the elapsed clock. */
    uint32_t    ac_zero_42;     /* 0x42: 0x00E5AB54 `clr.l (-0x14e,A6)`; nothing
                                 * ever stores anything else here. */
    comp_t      ac_io_write;    /* 0x46: 0x00E5AB60 -- compress(*arg6), and arg6
                                 * is &PROC1_$STATS[PROC1_$CURRENT].pages_written
                                 * (caller 0x00E748CA `pea (-0x8,A2,D2w*0x1)`
                                 * with A2 = 0x00E25D20, D2 = cur*0x10). */
    comp_t      ac_io_read;     /* 0x48: 0x00E5AB6C -- compress(*arg7) =
                                 * PROC1_$STATS[..].pages_read (0x00E748C6). */
    uid_t       ac_proc_uid;    /* 0x4A: 0x00E5ABDE/0x00E5ABE2, two longwords. */
    char        ac_comm[32];    /* 0x52: 0x00E5AC04 `move.b (-0x1,A2,D0w*0x1),
                                 * (-0x13f,A1)` with A1 = A6+D0 and D0 running
                                 * 1..min(len,0x20); zero-filled to 0x20 at
                                 * 0x00E5AC20. */
    comp_t      ac_mem;         /* 0x72: 0x00E5AB86 -- compress(60 * (*arg6 +
                                 * *arg7)); the 60x is built at 0x00E5AB74-7C as
                                 * (x<<2)<<4 - (x<<2). */
    uint8_t     ac_pad_74[12];  /* 0x74: never written; leaks stack. */
} __attribute__((packed)) pacct_record_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(pacct_record_t, ac_flags) == 0x00, "pacct_record_t.ac_flags");
_Static_assert(__builtin_offsetof(pacct_record_t, ac_stat) == 0x02, "pacct_record_t.ac_stat");
_Static_assert(__builtin_offsetof(pacct_record_t, ac_pad_03) == 0x03, "pacct_record_t.ac_pad_03");
_Static_assert(__builtin_offsetof(pacct_record_t, ac_sids) == 0x04, "pacct_record_t.ac_sids");
_Static_assert(__builtin_offsetof(pacct_record_t, ac_prot) == 0x28, "pacct_record_t.ac_prot");
_Static_assert(__builtin_offsetof(pacct_record_t, ac_devno) == 0x34, "pacct_record_t.ac_devno");
_Static_assert(__builtin_offsetof(pacct_record_t, ac_btime) == 0x38, "pacct_record_t.ac_btime");
_Static_assert(__builtin_offsetof(pacct_record_t, ac_utime) == 0x3C, "pacct_record_t.ac_utime");
_Static_assert(__builtin_offsetof(pacct_record_t, ac_stime) == 0x3E, "pacct_record_t.ac_stime");
_Static_assert(__builtin_offsetof(pacct_record_t, ac_etime) == 0x40, "pacct_record_t.ac_etime");
_Static_assert(__builtin_offsetof(pacct_record_t, ac_zero_42) == 0x42, "pacct_record_t.ac_zero_42");
_Static_assert(__builtin_offsetof(pacct_record_t, ac_io_write) == 0x46, "pacct_record_t.ac_io_write");
_Static_assert(__builtin_offsetof(pacct_record_t, ac_io_read) == 0x48, "pacct_record_t.ac_io_read");
_Static_assert(__builtin_offsetof(pacct_record_t, ac_proc_uid) == 0x4A, "pacct_record_t.ac_proc_uid");
_Static_assert(__builtin_offsetof(pacct_record_t, ac_comm) == 0x52, "pacct_record_t.ac_comm");
_Static_assert(__builtin_offsetof(pacct_record_t, ac_mem) == 0x72, "pacct_record_t.ac_mem");
_Static_assert(__builtin_offsetof(pacct_record_t, ac_pad_74) == 0x74, "pacct_record_t.ac_pad_74");
_Static_assert(sizeof(pacct_record_t) == 0x80, "pacct_record_t size");

/*
 * ============================================================================
 * Function Prototypes
 * ============================================================================
 */

/*
 * PACCT_$INIT - Initialize the process accounting subsystem
 *
 * Sets the accounting owner to UID_$NIL and clears state variables.
 * Called during system startup.
 *
 * Original address: 0x00E31CE8
 */
void PACCT_$INIT(void);

/*
 * PACCT_$SHUTDN - Shutdown the process accounting subsystem
 *
 * If accounting is enabled:
 *   - Unmaps the accounting file buffer
 *   - Unlocks the accounting file
 *   - Clears the accounting owner
 *
 * Original address: 0x00E5A6C0
 */
void PACCT_$SHUTDN(void);

/*
 * PACCT_$START - Start process accounting
 *
 * Enables process accounting to the specified file. Requires locksmith
 * privileges. If accounting is already enabled, shuts down existing
 * accounting first.
 *
 * Parameters:
 *   file_uid   - UID of the accounting file (must be a regular file)
 *   unused     - Unused parameter
 *   status_ret - Output status code
 *
 * Status codes:
 *   status_$ok - Accounting started successfully
 *   status_$insufficient_rights_to_perform_operation - Not locksmith
 *   status_$no_rights - File is not writable
 *
 * Original address: 0x00E5A746
 */
void PACCT_$START(uid_t *file_uid, uint32_t unused, status_$t *status_ret);

/*
 * PACCT_$STOP - Stop process accounting
 *
 * Disables process accounting if currently enabled. Requires locksmith
 * privileges.
 *
 * Does not return a status - check PACCT_$ON to verify accounting
 * has stopped.
 *
 * Original address: 0x00E5A8C0
 */
void PACCT_$STOP(void);

/*
 * PACCT_$ON - Check if process accounting is enabled
 *
 * Returns:
 *   true (-1)  if accounting is enabled
 *   false (0)  if accounting is disabled
 *
 * Original address: 0x00E5A9A4
 */
boolean PACCT_$ON(void);

/*
 * PACCT_$LOG - Log a process accounting record
 *
 * Writes an accounting record for a terminated process. Called by
 * the process termination code.
 *
 * If the accounting buffer is full (< 128 bytes remaining), the
 * buffer is unmapped and a new 32KB region is mapped.
 *
 * Parameters (the sole caller is 0x00E74908; each `pea` is noted):
 *   fork_flag      - Domain boolean, record flags bit 0 (0x00E74904)
 *   su_flag        - Domain boolean, record flags bit 1 (0x00E748F0)
 *   exit_status    - status word; the record keeps its LOW byte (0x00E748E2)
 *   start_clock    - Process start time (clock_t) (0x00E748D2)
 *   proc_times     - process record + 0x38; longwords [2] and [3] become
 *                    ac_utime / ac_stime (0x00E748CE `pea (-0x40,A1)`)
 *   io_write_count - &PROC1_$STATS[PROC1_$CURRENT].pages_written
 *                    (0x00E748CA `pea (-0x8,A2,D2w*0x1)`, A2 = 0x00E25D20)
 *   io_read_count  - &PROC1_$STATS[PROC1_$CURRENT].pages_read (0x00E748C6)
 *   tty_uid        - TTY UID for device number lookup (0x00E748B4)
 *   proc_uid       - Process UID (0x00E748B0)
 *   comm_ptr       - Command name string (0x00E748AC)
 *   comm_len       - Pointer to command name length (0x00E748A8)
 *
 * Original address: 0x00E5AA9C
 */
void PACCT_$LOG(boolean *fork_flag, boolean *su_flag, int16_t *exit_status,
                clock_t *start_clock, const uint32_t *proc_times,
                const uint32_t *io_write_count, const uint32_t *io_read_count,
                uid_t *tty_uid, const uid_t *proc_uid,
                const char *comm_ptr, const int16_t *comm_len);

#endif /* PACCT_H */
