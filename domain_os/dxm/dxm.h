/*
 * dxm/dxm.h - Deferred Execution Manager Public API
 *
 * The DXM subsystem provides deferred callback execution services.
 * It maintains two queues:
 *   - Wired queue: For callbacks that run with resource lock 0x0D held
 *   - Unwired queue: For callbacks that run with resource lock 0x03 held
 *
 * Callbacks are queued and later executed by helper processes that
 * wait on event counts associated with each queue.
 *
 * Original addresses: 0x00E16FE0 - 0x00E2FF78
 */

#ifndef DXM_H
#define DXM_H

#include "base/base.h"
#include "ec/ec.h"
#include "ml/ml.h"

/*
 * ============================================================================
 * Queue Entry Structure
 * ============================================================================
 * Each queue entry is 16 bytes:
 *   - 4 bytes: callback function pointer
 *   - 12 bytes: callback data (copied from caller)
 */
#define DXM_ENTRY_SIZE          16
#define DXM_MAX_DATA_SIZE       12

/*
 * DXM Queue Structure
 *
 * Implements a circular buffer of callback entries with spin lock protection.
 * Size: 28 bytes (0x1C)
 */
typedef struct dxm_queue_t {
    uint16_t        head;           /* 0x00: Head index (next to dequeue) */
    uint16_t        tail;           /* 0x02: Tail index (next to enqueue) */
    uint16_t        mask;           /* 0x04: Index mask for circular wrap */
    uint16_t        pad_06;         /* 0x06: Padding/alignment */
    ml_$spinlock_t  lock;           /* 0x08: Spin lock for queue access */
    ec_$eventcount_t ec;            /* 0x0C: Event count for signaling */
    void            *entries;       /* 0x18: Pointer to entry array */
} dxm_queue_t;

/*
 * DXM Queue Entry Structure
 *
 * Represents a single callback in the queue.
 * Size: 16 bytes
 */
typedef struct dxm_entry_t {
    void            (*callback)(void *);  /* 0x00: Callback function */
    uint8_t         data[DXM_MAX_DATA_SIZE]; /* 0x04: Callback data */
} dxm_entry_t;

/*
 * DXM Add Callback Flags
 *
 * The flags parameter to DXM_$ADD_CALLBACK is packed:
 *   - Low 16 bits: Data size (0-12 bytes)
 *   - Bit 16 (high byte bit 0): If set, check for duplicate entries
 */
#define DXM_FLAG_CHECK_DUP      0x00800000  /* Check for duplicate entries */

/*
 * Status codes
 */
#define status_$dxm_no_more_deferred_execution_queue_slots  0x00170001

/*
 * Resource lock IDs used by DXM helper processes
 */
#define DXM_WIRED_LOCK_ID       0x0D    /* Lock held by wired helper */
#define DXM_UNWIRED_LOCK_ID     0x03    /* Lock held by unwired helper */

/*
 * ============================================================================
 * Global Variables
 * ============================================================================
 */

/*
 * DXM_$WIRED_Q - Queue for wired (interrupt-safe) callbacks
 *
 * Callbacks added to this queue are executed by the wired helper
 * process which holds resource lock 0x0D.
 *
 * Original address: 0x00E2ADE0 (base + 0x620)
 */
extern dxm_queue_t DXM_$WIRED_Q;

/*
 * DXM_$UNWIRED_Q - Queue for unwired (normal) callbacks
 *
 * Callbacks added to this queue are executed by the unwired helper
 * process which holds resource lock 0x03.
 *
 * Original address: 0x00E2ADC4 (base + 0x604)
 */
extern dxm_queue_t DXM_$UNWIRED_Q;

/*
 * DXM_$OVERRUNS - Count of queue overflow events
 *
 * Incremented when a callback cannot be added due to queue full.
 *
 * Original address: 0x00E2ADC0 (base + 0x600)
 */
extern uint32_t DXM_$OVERRUNS;

/*
 * ============================================================================
 * Initialization Functions
 * ============================================================================
 */

/*
 * DXM_$INIT - Initialize DXM subsystem
 *
 * Initializes the event counts for both wired and unwired queues.
 * Called during system startup.
 *
 * Original address: 0x00E2FF50
 */
void DXM_$INIT(void);

/*
 * ============================================================================
 * Callback Queue Functions
 * ============================================================================
 */

/*
 * DXM_$ADD_CALLBACK - Add a callback to a deferred execution queue
 *
 * Adds a callback function with associated data to a DXM queue.
 * The callback will be executed later by a helper process.
 *
 * Parameters:
 *   queue - Queue to add callback to (DXM_$WIRED_Q or DXM_$UNWIRED_Q)
 *   callback - Pointer to callback function pointer
 *   data - Pointer to data to copy (up to 12 bytes)
 *   flags - Packed flags: low 16 bits = data size, bit 23 = check duplicates
 *   status_ret - Status return
 *
 * If the check duplicates flag is set and a matching entry already
 * exists in the queue (same callback and data), no new entry is added.
 *
 * Original address: 0x00E16FE0
 */
void DXM_$ADD_CALLBACK(dxm_queue_t *queue, void **callback, void **data,
                       uint32_t flags, status_$t *status_ret);

/*
 * DXM_$SCAN_QUEUE - Process all pending callbacks in a queue
 *
 * Dequeues and executes all pending callbacks in the specified queue.
 * Called by the helper processes.
 *
 * Parameters:
 *   queue - Queue to scan
 *
 * Original address: 0x00E17168
 */
void DXM_$SCAN_QUEUE(dxm_queue_t *queue);

/*
 * ============================================================================
 * Helper Process Entry Points
 * ============================================================================
 * These functions are the main loops for DXM helper processes.
 * They wait for callbacks to be added and then execute them.
 */

/*
 * DXM_$HELPER_COMMON - Common helper process loop
 *
 * Waits on the queue's event count and calls DXM_$SCAN_QUEUE
 * when callbacks are added. Runs in an infinite loop.
 *
 * Parameters:
 *   queue - Queue to service
 *
 * Original address: 0x00E171E4
 */
void DXM_$HELPER_COMMON(dxm_queue_t *queue);

/*
 * DXM_$HELPER_WIRED - Wired helper process entry point
 *
 * Sets resource lock 0x0D and services the wired queue.
 * This function never returns.
 *
 * Original address: 0x00E1721E
 */
void DXM_$HELPER_WIRED(void);

/*
 * DXM_$HELPER_UNWIRED - Unwired helper process entry point
 *
 * Sets resource lock 0x03 and services the unwired queue.
 * This function never returns.
 *
 * Original address: 0x00E17246
 */
void DXM_$HELPER_UNWIRED(void);

/*
 * ============================================================================
 * Signal Functions
 * ============================================================================
 * These functions integrate DXM with the signal delivery system.
 */

/*
 * DXM_$ADD_SIGNAL - Queue a signal for deferred delivery
 *
 * Adds a signal delivery callback to the unwired queue.
 *
 * Parameters:
 *   routine    - index into DXM_$SIGNAL_ROUTINES
 *   proc_index - index into PROC2_UID identifying the target
 *   signal     - signal number
 *   param      - signal parameter (4 bytes)
 *   check_dup  - Domain boolean (0xFF) -> DXM_$ADD_CALLBACK dedupes
 *   status_ret - Status return
 *
 * Original address: 0x00E17270
 */
void DXM_$ADD_SIGNAL(uint16_t routine, uint16_t proc_index, uint16_t signal,
                     uint32_t param, uint8_t check_dup, status_$t *status_ret);

/*
 * Deferred signal record
 *
 * Built on the stack by DXM_$ADD_SIGNAL (0x00E17270) at (-0x10,A6) and
 * copied into the queue entry as ten bytes (`move.w #0xa,-(SP)` at
 * 0x00E172A6):
 *   00e1728e  move.w D0w,(-0x10,A6)   ; +0x00 <- arg at (0x8,A6)
 *   00e17292  move.w D1w,(-0x8,A6)    ; +0x08 <- arg at (0xa,A6)
 *   00e17296  move.w D2w,(-0xe,A6)    ; +0x02 <- arg at (0xc,A6)
 *   00e1729a  move.l D3,(-0xc,A6)     ; +0x04 <- arg at (0xe,A6)
 *
 * Size: 10 bytes (0x0A)
 */
typedef struct dxm_signal_data_t {
    int16_t     routine;        /* 0x00: index into DXM_$SIGNAL_ROUTINES */
    int16_t     signal;         /* 0x02: signal number (passed by reference) */
    uint32_t    param;          /* 0x04: signal parameter (by reference) */
    int16_t     proc_index;     /* 0x08: index into PROC2_UID */
} dxm_signal_data_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(dxm_signal_data_t, routine) == 0x00,
               "dxm_signal_data_t.routine must be at 0x00");
_Static_assert(__builtin_offsetof(dxm_signal_data_t, signal) == 0x02,
               "dxm_signal_data_t.signal must be at 0x02");
_Static_assert(__builtin_offsetof(dxm_signal_data_t, param) == 0x04,
               "dxm_signal_data_t.param must be at 0x04");
_Static_assert(__builtin_offsetof(dxm_signal_data_t, proc_index) == 0x08,
               "dxm_signal_data_t.proc_index must be at 0x08");
_Static_assert(sizeof(dxm_signal_data_t) == 0x0A,
               "dxm_signal_data_t must be 10 bytes");
#endif

/*
 * Deferred signal-delivery routine table
 *
 * DXM_$ADD_SIGNAL_CALLBACK indexes this with routine*4
 * (`lsl.w #0x2,D1w` / `lea (0x0,A5,D1w),A1` at 0x00E721B6) after loading
 * A5 with 0xE85708 (`lea (0xe85708).l,A5` at 0x00E7218C).  The image
 * holds two entries there:
 *   0xE85708: 0x00E3F0A6 = PROC2_$SIGNAL_OS
 *   0xE8570C: 0x00E3F2C2 = PROC2_$SIGNAL_PGROUP_OS
 * and both take (uid_t *, int16_t *, uint32_t *, status_$t *).
 *
 * The Ghidra label NETLOG_$DATA_END also sits at 0xE85708; it marks the
 * end of the NETLOG data block (NETLOG_$CNTL's A5 base 0xE85684 reaches
 * only as far as (0x7e,A5) = 0xE85702) and is unrelated to this table.
 * The Ghidra label DXM_$SIGNAL_ROUTINES was added at 0xE85708.
 */
typedef void (*dxm_$signal_routine_t)(uid_t *uid, int16_t *signal,
                                      uint32_t *param, status_$t *status_ret);

#define DXM_SIGNAL_ROUTINE_COUNT    2
#define DXM_SIGNAL_ROUTINE_PROC     0   /* PROC2_$SIGNAL_OS */
#define DXM_SIGNAL_ROUTINE_PGROUP   1   /* PROC2_$SIGNAL_PGROUP_OS */

#if defined(ARCH_M68K)
#define DXM_$SIGNAL_ROUTINES ((dxm_$signal_routine_t *)0xE85708)
#else
extern dxm_$signal_routine_t DXM_$SIGNAL_ROUTINES[DXM_SIGNAL_ROUTINE_COUNT];
#endif

/*
 * DXM_$ADD_SIGNAL_CALLBACK - Signal delivery callback
 *
 * Called by DXM when a queued signal is ready to be delivered.
 * Looks up the signal handler and invokes it.
 *
 * Parameters:
 *   data - Pointer to a pointer to the queued dxm_signal_data_t
 *
 * Original address: 0x00E72184
 */
void DXM_$ADD_SIGNAL_CALLBACK(void *data);

#endif /* DXM_H */
