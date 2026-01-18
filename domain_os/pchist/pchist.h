/*
 * PCHIST_$ - Program Counter Histogram (Profiling) Subsystem
 *
 * This subsystem implements program counter sampling and profiling,
 * similar to the UNIX profil() system call. It provides both:
 * - Per-process profiling (UNIX-style profil() support)
 * - System-wide PC histogram collection
 *
 * When profiling is enabled, periodic timer interrupts sample the
 * program counter and update histogram bins or per-process profile
 * buffers.
 *
 * Reverse engineered from Domain/OS
 */

#ifndef PCHIST_H
#define PCHIST_H

#include "os/os.h"
#include "ml/ml.h"

/*
 * Maximum number of processes that can have per-process profiling
 * (based on bitmap size at 0xe2c21c - appears to be ~64 processes)
 */
#define PCHIST_MAX_PROCESSES    64

/*
 * Number of histogram bins for system-wide profiling
 */
#define PCHIST_HISTOGRAM_BINS   256

/*
 * Per-process profiling data structure
 * Size: 20 bytes (0x14)
 * Array located at 0xe85704, indexed by (pid * 0x14)
 */
typedef struct pchist_proc_t {
    void     *buffer;       /* 0x00: User buffer address for profil() */
    uint32_t  bufsize;      /* 0x04: Buffer size in bytes */
    uint32_t  offset;       /* 0x08: PC offset (base address) */
    uint32_t  scale;        /* 0x0C: Scaling factor (fixed point) */
    uint32_t *overflow_ptr; /* 0x10: Pointer to track overflow (or NULL) */
} pchist_proc_t;

/*
 * System-wide histogram control structure
 * Located at 0xe85c24
 */
typedef struct pchist_histogram_t {
    int16_t   enabled;           /* 0x00: 1 if enabled */
    int16_t   doalign;           /* 0x02: Alignment flag */
    int16_t   pad1;              /* 0x04: Padding */
    uint16_t  multiplier;        /* 0x06: Bin size multiplier */
    int16_t   pid_filter;        /* 0x08: PID to profile (0 = all) */
    uint16_t  shift;             /* 0x0A: Shift count for bin calculation */
    uint32_t  range_start;       /* 0x0C: Start of PC range */
    uint32_t  range_end;         /* 0x10: End of PC range */
    uint32_t  bucket_size;       /* 0x14: Bucket size */
    uint32_t  total_samples;     /* 0x18: Total samples taken */
    uint32_t  over_range;        /* 0x1C: Samples above range */
    uint32_t  under_range;       /* 0x20: Samples below range */
    uint32_t  wrong_pid;         /* 0x24: Samples for wrong PID */
    uint32_t  histogram[PCHIST_HISTOGRAM_BINS]; /* 0x28+: Histogram bins */
} pchist_histogram_t;

/*
 * PCHIST control structure (internal state)
 * Located at 0xe2c204
 */
typedef struct pchist_control_t {
    ml_$exclusion_t lock;           /* 0x00: Exclusion lock (size varies) */
    /* At offset 0x18 from base: */
    uint8_t  proc_bitmap[8];        /* Process profiling enable bitmap */
    /* At offset 0x1C from base: */
    uint32_t proc_pc[PCHIST_MAX_PROCESSES]; /* Last sampled PC per process */
    /* At offset 0x120 from base: */
    int16_t  sys_profiling_count;   /* System-wide profiling refcount */
    /* At offset 0x122 from base: */
    int16_t  proc_profiling_count;  /* Per-process profiling refcount */
    /* At offset 0x124 from base: */
    int8_t   histogram_enabled;     /* System histogram enabled flag */
    /* At offset 0x125 from base: */
    int8_t   pad;
    /* At offset 0x126 from base: */
    int8_t   doalign;               /* Alignment mode flag */
} pchist_control_t;

/*
 * External data references
 */
extern pchist_control_t PCHIST_$CONTROL;    /* Control structure at 0xe2c204 */
extern pchist_proc_t PCHIST_$PROC_DATA[];   /* Per-process data at 0xe85704 */
extern pchist_histogram_t PCHIST_$HISTOGRAM; /* Histogram at 0xe85c24 */

/*
 * Alignment mode flag (for PCHIST_$CNTL command 3)
 */
extern int8_t PCHIST_$DOALIGN;

/*
 * ============================================================================
 * Public Functions
 * ============================================================================
 */

/*
 * PCHIST_$INIT - Initialize the PC history subsystem
 *
 * Initializes the exclusion lock used for synchronization.
 *
 * Original address: 0x00e32394
 */
void PCHIST_$INIT(void);

/*
 * PCHIST_$COUNT - Record a PC sample
 *
 * Called with the sampled PC and mode. Updates per-process trace
 * data and/or system-wide histogram depending on configuration.
 *
 * Parameters:
 *   pc_ptr   - Pointer to the sampled program counter value
 *   mode_ptr - Pointer to mode (1 = trace fault mode)
 *
 * Original address: 0x00e1a134
 */
void PCHIST_$COUNT(uint32_t *pc_ptr, int16_t *mode_ptr);

/*
 * PCHIST_$INTERRUPT - Record PC sample from interrupt
 *
 * Wrapper around PCHIST_$COUNT for interrupt-level sampling.
 * Uses a fixed mode value.
 *
 * Parameters:
 *   pc_ptr - Pointer to the sampled program counter value
 *
 * Original address: 0x00e1a1f6
 */
void PCHIST_$INTERRUPT(uint32_t *pc_ptr);

/*
 * PCHIST_$CNTL - Control system-wide PC histogram
 *
 * Commands:
 *   0 - Start profiling with new parameters
 *   1 - Stop profiling and return current data
 *   2 - (unused)
 *   3 - Start profiling with alignment
 *
 * Parameters:
 *   cmd_ptr    - Pointer to command code
 *   range_ptr  - Pointer to range parameters (start, end, pid)
 *   data_ptr   - Pointer to receive histogram data
 *   status_ret - Pointer to receive status
 *
 * Original address: 0x00e5cdb6
 */
void PCHIST_$CNTL(
    int16_t *cmd_ptr,
    uint32_t *range_ptr,
    void *data_ptr,
    status_$t *status_ret
);

/*
 * PCHIST_$UNIX_PROFIL_CNTL - Control per-process profiling (UNIX profil)
 *
 * Implements the UNIX profil() system call semantics.
 *
 * Commands:
 *   0 - Enable profiling for current process
 *   1 - Disable profiling for current process
 *   2 - Set overflow pointer
 *
 * Parameters:
 *   cmd_ptr    - Pointer to command code
 *   buffer_ptr - Pointer to user buffer address
 *   bufsize_ptr - Pointer to buffer size
 *   offset_ptr - Pointer to PC offset (base address)
 *   scale_ptr  - Pointer to scaling factor
 *   status_ret - Pointer to receive status
 *
 * Original address: 0x00e5ca58
 */
void PCHIST_$UNIX_PROFIL_CNTL(
    int16_t *cmd_ptr,
    void **buffer_ptr,
    uint32_t *bufsize_ptr,
    uint32_t *offset_ptr,
    uint32_t *scale_ptr,
    status_$t *status_ret
);

/*
 * PCHIST_$UNIX_PROFIL_FORK - Copy profiling state on fork
 *
 * Called during process fork to copy the parent's profiling
 * configuration to the child process.
 *
 * Parameters:
 *   child_pid_ptr - Pointer to child process ID
 *
 * Original address: 0x00e5cc32
 */
void PCHIST_$UNIX_PROFIL_FORK(int16_t *child_pid_ptr);

/*
 * PCHIST_$UNIX_PROFIL_ADDUPC - Update profiling buffer
 *
 * Called to update the per-process profiling buffer with
 * the last sampled PC value.
 *
 * Original address: 0x00e5cfac
 */
void PCHIST_$UNIX_PROFIL_ADDUPC(void);

#endif /* PCHIST_H */
