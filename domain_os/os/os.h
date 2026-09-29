// OS subsystem header for Domain/OS
// Core operating system functions

#ifndef OS_H
#define OS_H

#include "base/base.h"
#include "ec/ec.h"
#include "ml/ml.h"

// =============================================================================
// OS Revision Information
// =============================================================================

// OS_$REV structure size (0x33 * 4 = 204 bytes)
#define OS_REV_SIZE  204

// OS revision info (located at 0xe78400)
extern uint32_t OS_$REV[];

// =============================================================================
// OS Shutdown State
// =============================================================================

// Flag indicating shutdown is in progress
extern char OS_$SHUTTING_DOWN_FLAG;

/*
 * Boot device record (0xE82728), filled in by OS_$INIT at 0x00E338F2:
 *   +0x00  move.w (-0x28,A6),(A3)       the boot device number
 *   +0x02  clr.w  (0x2,A3)
 *   +0x04  move.l (-0x26,A6),(0x4,A3)   the {controller, unit} longword
 */
typedef struct os_$boot_device_t {
    int16_t device;   /* +0x00 */
    int16_t reserved; /* +0x02, always cleared */
    int16_t ctlr;     /* +0x04 */
    int16_t unit;     /* +0x06 */
} os_$boot_device_t;

extern os_$boot_device_t OS_$BOOT_DEVICE;

// Shutdown eventcount
extern ec_$eventcount_t OS_$SHUTDOWN_EC;

/*
 * PTR_OS_DATA_SHUTWIRED - pointer cell naming the start of the
 * OS_DATA_SHUTWIRED region (map: 0x00E82128).  Passed by address to
 * MST_$WIRE_AREA by both OS_$SHUTDOWN and STOP_$WATCH (as the end of the
 * stopwatch wire area).  There are two cells with this value in the image:
 * OS_$SHUTDOWN's literal at 0x00E6D688 and STOP_$WATCH's copy at 0x00E81D20;
 * they share the one definition in os/os_data.c (bead source-wk2f).
 */
extern m68k_ptr_t PTR_OS_DATA_SHUTWIRED;

// =============================================================================
// Memory Copy/Zero Functions
// =============================================================================

// OS_$DATA_COPY - Copy memory efficiently
// Optimized copy that uses 4-byte transfers when both src and dst are aligned
// @param src: Source address
// @param dst: Destination address
// @param len: Number of bytes to copy
extern void OS_$DATA_COPY(const void *src, void *dst, uint32_t len);

// OS_$DATA_ZERO - Zero memory efficiently
// Optimized zero that handles alignment and uses 4-byte writes when possible
// @param ptr: Address to zero
// @param len: Number of bytes to zero
extern void OS_$DATA_ZERO(void *ptr, uint32_t len);

// =============================================================================
// Boot and Initialization Functions
// =============================================================================

// OS_$INIT - Main OS initialization entry point
// Called during system boot to initialize all subsystems
// @param param_1: Boot parameters
// @param param_2: Additional boot parameters
extern void OS_$INIT(uint32_t *param_1, uint32_t *param_2);

// OS_$BOOT_ERRCHK - Check and report boot errors
// If status is non-zero, formats and displays error message, then waits
// @param format_str: Format string for error message
// @param arg_str: Additional argument string
// @param line_ptr: Pointer to line number
// @param status: Pointer to status to check
// @return: 0xFF if status was ok, 0 if error was displayed
extern char OS_$BOOT_ERRCHK(const char *format_str, const char *arg_str,
                            short *line_ptr, status_$t *status);

// =============================================================================
// Version and Information Functions
// =============================================================================

// OS_$GET_REV_INFO - Get OS revision information
// Copies the OS_$REV structure to the caller's buffer
// @param buf: Buffer to receive revision info (must be OS_REV_SIZE bytes)
extern void OS_$GET_REV_INFO(void *buf);

// =============================================================================
// Display Functions
// =============================================================================

// OS_$INSTALL_DISPLAY_ASTE - Install display address space table entry
// Sets up memory mapping for display hardware
// @param uid: UID of the display object
// @param param_2: Virtual address parameter
// @param size: Pointer to size of display memory
// @param touch: Pointer to touch flag (if true, touch all pages)
extern void OS_$INSTALL_DISPLAY_ASTE(uid_t *uid, void *param_2,
                                     int *size, char *touch);

// =============================================================================
// System Control Functions
// =============================================================================

// OS_$SHUTDOWN - Perform system shutdown
// Shuts down all subsystems in order and halts the system
// @param status: Pointer to status (used for privilege check)
extern void OS_$SHUTDOWN(status_$t *status);

// =============================================================================
// Checksum Functions
// =============================================================================

/*
 * OS_$CHKSUM (0x00E6D698) - a stub in this build.
 *
 * The body is only `clr.l (A0)` on the argument at (0x18,A6) and `clr.b (A1)`
 * on the one at (0x14,A6): the enable flag is cleared and the status is set
 * to status_$ok.  The first three arguments are never read.  OS_$INIT calls
 * it twice, at 0x00E340E0 and 0x00E34112.
 *
 * @param param_1    unused (by reference)
 * @param param_2    unused (by reference)
 * @param param_3    unused (by reference)
 * @param enable     enable flag, cleared on return
 * @param status_ret set to status_$ok
 */
extern void OS_$CHKSUM(void *param_1, void *param_2, void *param_3,
                       char *enable, status_$t *status_ret);

// =============================================================================
// Eventcount Functions
// =============================================================================

// OS_$GET_EC - Get the shutdown eventcount
// Returns a registered eventcount for monitoring shutdown state
// @param param_1: Unused parameter
// @param ec_ret: Pointer to receive eventcount pointer
// @param status: Pointer to receive status
extern void OS_$GET_EC(void *param_1, ec_$eventcount_t **ec_ret,
                       status_$t *status);

                       /*
 * OS_DISK_PROC - Clear disk process table entries
 *
 * Parameters:
 *   proc_id - Process ID to clear, or 0 to clear all entries
 *
 * When an entry's process ID matches (or proc_id == 0):
 *   - The process ID field is cleared to 0
 *   - The status field is set to 0xFFFF (invalid/unused)
 */
extern void OS_DISK_PROC(int16_t proc_id);

/*
 * OS_TERM_INIT - Initialize a console terminal structure (0x00E32A60)
 *
 * Called by TERM_$INIT (term/init.c) and SIO_$INIT (sio/init.c).
 * Signature follows the definition in os/term_init.c: six pointers to
 * 32-bit cells.
 *
 * Parameters:
 *   term_state    - Terminal state structure to initialize
 *   parent_desc   - Parent descriptor (DTTE; receives back-pointer at +0x2C)
 *   src_field_14  - Pointer to the value stored at term_state+0x14 (line data)
 *   src_field_00  - Pointer to the value stored at term_state+0x00 (rcv handler)
 *   src_field_10  - Pointer to the value stored at term_state+0x10 (SIO desc)
 *   src_fields    - Vtable; entries +4..+0xC copied to term_state+0x04..0x0C
 */
extern void OS_TERM_INIT(uint32_t *term_state, uint32_t *parent_desc,
                         uint32_t *src_field_14, uint32_t *src_field_00,
                         uint32_t *src_field_10, uint32_t *src_fields);

/*
 * OS_$DATA_COPY - block copy, longwords when both ends are even (0x00E11F04)
 *
 * Arguments are (src, dst, len): 0x00E11F04 `movem.l (0x4,SP),{A0 A1}` then
 * 0x00E11F0A-0x00E11F0E swaps them, so A0 (the `(A0)+` destination) ends up
 * holding argument 2.  Only the low WORD of the length is used
 * (0x00E11F24 `move.w D0w,D1w`).
 */
extern void OS_$DATA_COPY(const void *src, void *dst, uint32_t len);

/*
 * ============================================================================
 * The STACK segment - OS_$STACK
 * ============================================================================
 *
 * Module data block OS_$STACK: Claude Opus 5.5 (source-4k71).
 *
 * SAU2 map: "D EB0000 STACK size = 2C00" (loader segment "D64 EB0000 PAGE",
 * the start of OS_PAGE), 0x00EB0000..0x00EB2BFF, with the symbols
 *
 *   EB0000 NULL_STACK_GUARD / OS_PAGE   EB0800 P1_STACK_GUARD
 *   EB07F4 NULL_STACK                   EB2000 INT_STACK_GUARD / P1_STACK_BASE
 *   EB07FC NULL_PC                      EB2C00 INT_STACK_BASE (the next
 *                                              segment, VTOC_CACHE, starts
 *                                              there: the block's end)
 *
 * The image carries no bytes for it (Ghidra cannot read 0xEB0000..0xEB2BFF),
 * so the block is zero-filled.  What the code does with it:
 *
 *   - OS_$INIT frees the three guard pages by VA: `move.l #0xeb0000` /
 *     `#0xeb0800` / `#0xeb2000,-(SP)` + `jsr os_$free_va_page` at
 *     0x00E3406C / 0x00E3407A / 0x00E34088.  Pages are 0x400 bytes, so the
 *     block must start on a page boundary for those three VAs to name its
 *     own pages: the type is aligned to 0x400 (the image's 0xEB0000 is).
 *   - OS_$INIT zeroes the 0x400 bytes below INT_STACK_BASE:
 *     `movea.l #0xeb2c00,A0` / `clr.b -(A0)` x 0x400 (0x00E34096-0x00E340A2).
 *   - OS_$INIT stores NULLPROC's address in NULL_PC:
 *     `move.l #0xe24c60,(0x00eb07fc).l` at 0x00E33CE6.
 *   - PROC1_$INIT gives process 1 the stack whose top is P1_STACK_BASE:
 *     `move.l #0xeb2000,(0x734,A0)` at 0x00E2F976 (PROC1_$DATA.os_stack_base[1]).
 *   - IO_$USE_INT_STACK (io/sau2/use_int_stack.s) switches SP to 0xEB2BE8,
 *     0x18 below INT_STACK_BASE (`movea.l #0xeb2be8,SP` at 0x00E2E83E), after
 *     saving the interrupted SR at 0xEB2BF8 (`move.w (0x10,SP),(0x00eb2bf8).l`
 *     at 0x00E2E830); PROC1_$INT_ADVANCE / PROC1_$INT_EXIT
 *     (proc1/sau2/int_handler.s) compare SP with 0xEB2BE8 to tell whether an
 *     interrupt came in on the interrupt stack.  Both reach the block through
 *     `.set NAME, OS_$STACK + off` aliases.
 *
 * Page layout (1 KB pages): +0x0000 guard, +0x0400 null process stack,
 * +0x0800 guard, +0x0C00..+0x2000 process 1 stack, +0x2000 guard,
 * +0x2400..+0x2C00 interrupt stack.  Every field is pointer-free (NULL_PC
 * holds a VA), so the asserts are unconditional.
 */
#define OS_$STACK_SIZE          0x2C00  /* map: STACK size = 2C00 */
#define OS_$STACK_INT_SP_OFF    0x2BE8  /* IO_$USE_INT_STACK's initial SP */

typedef struct __attribute__((aligned(0x400))) os_$stack_t {
    uint8_t  null_stack_guard[0x400];   /* +0x0000 NULL_STACK_GUARD, freed  */
    uint8_t  null_stack_area[0x3F4];    /* +0x0400                          */
    uint8_t  null_stack[8];             /* +0x07F4 NULL_STACK               */
    uint32_t null_pc;                   /* +0x07FC NULL_PC: VA of NULLPROC  */
    uint8_t  p1_stack_guard[0x400];     /* +0x0800 P1_STACK_GUARD, freed    */
    uint8_t  p1_stack[0x1400];          /* +0x0C00 .. P1_STACK_BASE +0x2000 */
    uint8_t  int_stack_guard[0x400];    /* +0x2000 INT_STACK_GUARD, freed   */
    uint8_t  int_stack[0x7E8];          /* +0x2400 .. initial SP +0x2BE8    */
    uint8_t  int_stack_top[0x10];       /* +0x2BE8                          */
    uint16_t saved_int_sr;              /* +0x2BF8 IO_$SAVED_INT_SR         */
    uint8_t  _2bfa[6];                  /* +0x2BFA .. INT_STACK_BASE +0x2C00 */
} os_$stack_t;

_Static_assert(__builtin_offsetof(os_$stack_t, null_stack) == 0x07F4,
               "NULL_STACK 0xEB07F4");
_Static_assert(__builtin_offsetof(os_$stack_t, null_pc) == 0x07FC,
               "NULL_PC 0xEB07FC (move.l #0xe24c60,(0x00eb07fc).l)");
_Static_assert(__builtin_offsetof(os_$stack_t, p1_stack_guard) == 0x0800,
               "P1_STACK_GUARD 0xEB0800");
_Static_assert(__builtin_offsetof(os_$stack_t, int_stack_guard) == 0x2000,
               "INT_STACK_GUARD / P1_STACK_BASE 0xEB2000");
_Static_assert(__builtin_offsetof(os_$stack_t, int_stack_top) == OS_$STACK_INT_SP_OFF,
               "interrupt stack initial SP 0xEB2BE8 (movea.l #0xeb2be8,SP)");
_Static_assert(__builtin_offsetof(os_$stack_t, saved_int_sr) == 0x2BF8,
               "IO_$SAVED_INT_SR 0xEB2BF8");
_Static_assert(sizeof(os_$stack_t) == OS_$STACK_SIZE,
               "STACK: 0xEB0000..INT_STACK_BASE 0xEB2C00");

MODULE_DATA_DECLARE(os_$stack_t, OS_$STACK, 0x00EB0000);

/*
 * The map's labels, as addresses in the block.  INT_STACK_BASE is the
 * block's end (the first byte past the interrupt stack); P1_STACK_BASE is the
 * top of process 1's stack, i.e. the INT_STACK_GUARD page.
 */
#define NULL_STACK_GUARD ((char *)OS_$STACK.null_stack_guard)
#define P1_STACK_GUARD   ((char *)OS_$STACK.p1_stack_guard)
#define INT_STACK_GUARD  ((char *)OS_$STACK.int_stack_guard)
#define P1_STACK_BASE    ((char *)OS_$STACK.int_stack_guard)
#define INT_STACK_BASE   ((char *)&OS_$STACK + OS_$STACK_SIZE)
#define NULL_PC          (OS_$STACK.null_pc)

#endif /* OS_H */
