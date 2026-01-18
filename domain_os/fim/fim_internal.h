/*
 * fim/fim_internal.h - FIM Internal Declarations
 *
 * Internal data structures, constants, and helper prototypes
 * for the Fault/Interrupt Manager subsystem.
 *
 * This file should be included by FIM implementation files only.
 */

#ifndef FIM_INTERNAL_H
#define FIM_INTERNAL_H

#include "fim/fim.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "ml/ml.h"
#include "as/as.h"
#include "acl/acl.h"
#include "name/name.h"
#include "peb/peb.h"
#include "misc/crash.h"

/*
 * ============================================================================
 * Internal Constants
 * ============================================================================
 */

/* Base address for FIM data area */
#define FIM_DATA_BASE           0x00E2126C

/* Delivery frame magic number */
#define FIM_DF_MAGIC            0xDFDF

/* Delivery frame version */
#define FIM_DF_VERSION          2

/* Delivery frame size */
#define FIM_DF_SIZE             0x6A

/* FIM status codes */
#define FIM_STATUS_OK           0x00000000
#define FIM_STATUS_ACCESS_VIOL  0x00920019  /* Access violation */
#define FIM_STATUS_CLEANUP_SET  0x00240002  /* Cleanup handler set */

/* Bus error handler address (cold boot) */
#define PROM_TRAP_BUS_ERROR     0x00000008  /* Vector 2: Bus Error */

/* Frame size table offset from FIM_DATA_BASE */
#define FIM_FRAME_SIZE_OFFSET   0x124

/* Per-AS flag indicating we're in FIM */
/* FIM_IN_FIM is at FIM_DATA_BASE + 0 */

/* Per-AS user FIM handler address */
/* FIM_$USER_FIM_ADDR is at FIM_DATA_BASE + 0x3C */

/* OS stack base table */
#define OS_STACK_BASE           0x00E25C18

/* Threshold for recoverable fault address */
#define FIM_RECOVERABLE_ADDR    0x00D00000

/*
 * ============================================================================
 * Internal Data Structures
 * ============================================================================
 */

/*
 * FIM exception info extracted from frame
 * Used during BUILD_DF processing
 */
typedef struct fim_exc_info_t {
    uint16_t    sr;             /* Status register */
    uint32_t    pc;             /* Program counter */
    uint16_t    ssw;            /* Special status word (bus error) */
    uint32_t    fault_addr;     /* Fault address */
    uint16_t    format;         /* Frame format code */
} fim_exc_info_t;

/*
 * FIM local variables for BUILD_DF
 */
typedef struct fim_build_locals_t {
    uint16_t    flags;          /* Accumulated flags */
    uint16_t    ssw;            /* Special status word */
    uint32_t    fault_addr;     /* Fault address */
    uint16_t    aux_status;     /* Auxiliary status (from SSW) */
    uint16_t    fp_status;      /* FP save status */
    int8_t      delivered;      /* 0xFF=delivered, 0=not delivered */
    int8_t      cleanup_ran;    /* Non-zero if cleanup handler ran */
} fim_build_locals_t;

/*
 * ============================================================================
 * Internal Global Data References
 * ============================================================================
 */

/*
 * Cleanup handler stack head for each AS
 * Indexed by PROC1_$AS_ID
 * Points to first cleanup handler entry
 */
extern void *FIM_CLEANUP_STACK[];

/*
 * Pointer to current cleanup handler
 * Address: 0x00E216B2
 */
extern void *FIM_CLEANUP_HANDLER_PTR;

/*
 * PROM bus error handler address (cold boot)
 * If this is FIM_$COLD_BUS_ERR, we're in cold boot
 */
extern void *PROM_TRAP_BUS_ERROR_PTR;

/*
 * ============================================================================
 * Internal Function Prototypes
 * ============================================================================
 */

/*
 * fim_get_frame_size - Get exception frame size for format code
 *
 * Parameters:
 *   format - Frame format code (0-11)
 *
 * Returns:
 *   Size in bytes of the exception frame
 */
static inline uint8_t fim_get_frame_size(uint8_t format)
{
    return FIM_FRAME_SIZE_TABLE[format];
}

/*
 * fim_extract_exc_info - Extract exception info from frame
 *
 * Parameters:
 *   frame - Pointer to exception frame
 *   info - Output: extracted exception info
 */
void fim_extract_exc_info(void *frame, fim_exc_info_t *info);

/*
 * fim_check_recoverable - Check if fault is recoverable
 *
 * Parameters:
 *   addr - Fault address
 *   flags - Fault flags
 *
 * Returns:
 *   Non-zero if fault can be recovered (delivered to user)
 */
int fim_check_recoverable(uint32_t addr, uint16_t flags);

/*
 * ============================================================================
 * Assembly Helper References
 * ============================================================================
 */

/*
 * These functions are implemented in assembly and used internally
 */

/* Cold bus error handler - used during boot before FIM is initialized */
extern void FIM_$COLD_BUS_ERR(void);

/* Console output for crash display */
extern void crash_puts_string(const char *str);

/* Error strings for crash display */
extern const char FIM_FAULT_STRING[];        /* "FAULT IN DOMAIN_OS " */
extern const char FIM_EXCEPTION_CHARS[];     /* Exception type characters */

/*
 * ============================================================================
 * Architecture-Specific Macros
 * ============================================================================
 */

#ifdef M68K

/* Get format code from exception frame */
#define FIM_GET_FORMAT(frame) \
    ((((fim_exception_frame_t *)(frame))->format_vector >> 12) & 0x0F)

/* Get vector offset from exception frame */
#define FIM_GET_VECTOR(frame) \
    (((fim_exception_frame_t *)(frame))->format_vector & 0x0FFF)

/* Check if frame is supervisor mode */
#define FIM_IS_SUPERVISOR(frame) \
    ((((fim_exception_frame_t *)(frame))->sr & 0x2000) != 0)

/* Check if trace flag is set */
#define FIM_IS_TRACE(frame) \
    ((((fim_exception_frame_t *)(frame))->sr & 0x8000) != 0)

#endif /* M68K */

#endif /* FIM_INTERNAL_H */
