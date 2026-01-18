/*
 * peb/peb_internal.h - PEB Internal Definitions
 *
 * Internal data structures, globals, and helper functions for the PEB
 * subsystem. Not for use by external modules.
 */

#ifndef PEB_INTERNAL_H
#define PEB_INTERNAL_H

#include "peb/peb.h"
#include "ec/ec.h"

/*
 * ============================================================================
 * Global Data Structures
 * ============================================================================
 *
 * PEB global data is located at 0xE24C78.
 * Layout:
 *   +0x00: Event counter (8 bytes)
 *   +0x08: Reserved
 *   +0x14: Current owner process ID (2 bytes)
 *   +0x16: Current owner AS ID (2 bytes)
 *   +0x18: PEB CTL shadow register (2 bytes)
 *   +0x1A: PEB_$INSTALLED flag (1 byte)
 *   +0x1B: PEB_$WCS_LOADED flag (1 byte)
 *   +0x1C: PEB_$SAVEP_FLAG (1 byte)
 *   +0x1D: Unknown flag (1 byte)
 *   +0x1E: PEB info byte (1 byte)
 *   +0x1F: PEB_$MMU_INSTALLED flag (1 byte)
 *   +0x20: M68881_$SAVE_FLAG (1 byte)
 *   +0x21: Unknown flag (1 byte)
 */

typedef struct peb_globals_t {
    ec_$eventcount_t    eventcount;         /* +0x00: PEB event counter */
    uint8_t             reserved1[12];      /* +0x08: Reserved */
    uint16_t            owner_pid;          /* +0x14: Current owner process ID */
    uint16_t            owner_asid;         /* +0x16: Current owner AS ID */
    uint16_t            ctl_shadow;         /* +0x18: PEB_CTL shadow register */
    uint8_t             installed;          /* +0x1A: PEB hardware installed */
    uint8_t             wcs_loaded;         /* +0x1B: WCS microcode loaded */
    uint8_t             savep_flag;         /* +0x1C: Save pending flag */
    uint8_t             flag_1d;            /* +0x1D: Unknown flag */
    uint8_t             info_byte;          /* +0x1E: Info byte */
    uint8_t             mmu_installed;      /* +0x1F: MMU mappings installed */
    uint8_t             m68881_save_flag;   /* +0x20: MC68881 save flag */
    uint8_t             flag_21;            /* +0x21: Unknown flag */
} peb_globals_t;

/*
 * Global PEB data
 * Address: 0xE24C78
 */
#if defined(M68K)
    #define PEB_GLOBALS         (*(peb_globals_t *)0xE24C78)
#else
    extern peb_globals_t peb_globals;
    #define PEB_GLOBALS         peb_globals
#endif

/*
 * Convenience macros for global fields
 */
#define PEB_$OWNER_PID          PEB_GLOBALS.owner_pid
#define PEB_$OWNER_ASID         PEB_GLOBALS.owner_asid
#define PEB_$CTL_SHADOW         PEB_GLOBALS.ctl_shadow
#define PEB_$INSTALLED          PEB_GLOBALS.installed
#define PEB_$WCS_LOADED         PEB_GLOBALS.wcs_loaded
#define PEB_$SAVEP_FLAG         PEB_GLOBALS.savep_flag
#define PEB_$MMU_INSTALLED      PEB_GLOBALS.mmu_installed
#define PEB_$M68881_SAVE_FLAG   PEB_GLOBALS.m68881_save_flag
#define PEB_$INFO_BYTE          PEB_GLOBALS.info_byte
#define PEB_$EVENTCOUNT         PEB_GLOBALS.eventcount

/*
 * MC68881 existence flag
 * Set negative (<0) if MC68881 is present instead of PEB
 * Address: 0xE8180C
 */
#if defined(M68K)
    #define M68881_EXISTS       (*(volatile int8_t *)0xE8180C)
#else
    extern volatile int8_t m68881_exists;
    #define M68881_EXISTS       m68881_exists
#endif

/*
 * ============================================================================
 * WCS (Writable Control Store) Structures
 * ============================================================================
 *
 * WCS microcode entry format (8 bytes):
 *   +0x00: Microcode word 0 (2 bytes)
 *   +0x02: Microcode word 1 (2 bytes)
 *   +0x04: Microcode word 2 (4 bytes)
 */

typedef struct peb_wcs_entry_t {
    uint16_t    word0;          /* +0x00 */
    uint16_t    word1;          /* +0x02 */
    uint32_t    word2;          /* +0x04 */
} peb_wcs_entry_t;

/*
 * WCS microcode file header:
 *   +0x00: Start address (2 bytes)
 *   +0x02: Entry count (2 bytes)
 *   +0x04: First entry follows
 */

typedef struct peb_wcs_header_t {
    uint16_t    start_addr;     /* +0x00: Starting WCS address */
    uint16_t    entry_count;    /* +0x02: Number of entries */
    /* peb_wcs_entry_t entries[] follow */
} peb_wcs_header_t;

/*
 * ============================================================================
 * Hardware Register Access Helpers
 * ============================================================================
 */

/*
 * Get FP state pointer for address space ID
 * Each AS has 0x1C (28) bytes of state storage
 */
static inline peb_fp_state_t *peb_get_fp_state(int16_t asid)
{
    return &PEB_$WIRED_DATA_START[asid];
}

/*
 * PEB register offsets (from base 0x7000 or 0xFF7400)
 */
#define PEB_REG_CTRL        0x00    /* Control register */
#define PEB_REG_DATA_IN_0   0x8C    /* Data input register 0 */
#define PEB_REG_DATA_IN_1   0x90    /* Data input register 1 */
#define PEB_REG_DATA_OUT_0  0x94    /* Data output register 0 */
#define PEB_REG_DATA_OUT_1  0x98    /* Data output register 1 */
#define PEB_REG_STAT_IN_0   0x1D0   /* Status input 0 */
#define PEB_REG_STAT_IN_1   0x1D4   /* Status input 1 */
#define PEB_REG_STAT_OUT_0  0x1B0   /* Status output 0 */
#define PEB_REG_STAT_OUT_1  0x1B4   /* Status output 1 */
#define PEB_REG_STATUS      0xF4    /* Exception status register */
#define PEB_REG_CTRL_IN     0x84    /* Control input register */
#define PEB_REG_CTRL_OUT    0x104   /* Control output register */
#define PEB_REG_MISC        0x1DC   /* Misc register */

/*
 * ============================================================================
 * Error Messages
 * ============================================================================
 */

extern const char PEB_interrupt[];          /* "PEB_interrupt" */
extern const char PEB_FPU_Is_Hung_Err[];    /* "PEB FPU Is Hung Err" */
extern const char PEB_WCS_Verify_Failed_Err[]; /* "PEB WCS Verify Failed Err" */

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * PEB_$LOAD_WCS_CHECK_ERR - Check for WCS load errors
 *
 * Called after each WCS operation to check for errors.
 * If an error occurred, prints a warning and may disable the PEB.
 *
 * Parameters:
 *   msg - Error message to print if error occurred
 *
 * Returns:
 *   0 if no error, -1 if error occurred
 *
 * Original address: 0x00E31EC8 (144 bytes)
 */
int8_t PEB_$LOAD_WCS_CHECK_ERR(const char *msg);

/*
 * peb_$write_wcs - Write a WCS entry
 *
 * Writes microcode data to a WCS address. Sets the WCS page
 * select bits in the control register before writing.
 *
 * Parameters:
 *   addr - WCS address (0-based, includes page in upper bits)
 *   data - Pointer to 8-byte WCS entry data
 *
 * Original address: 0x00E31DD4 (122 bytes)
 */
void peb_$write_wcs(uint16_t addr, peb_wcs_entry_t *data);

/*
 * peb_$read_wcs - Read a WCS entry
 *
 * Reads microcode data from a WCS address. Sets the WCS page
 * select bits in the control register before reading.
 *
 * Parameters:
 *   addr - WCS address (0-based, includes page in upper bits)
 *   data - Pointer to receive 8-byte WCS entry data
 *
 * Original address: 0x00E31E4E (122 bytes)
 */
void peb_$read_wcs(uint16_t addr, peb_wcs_entry_t *data);

/*
 * peb_$cleanup_internal - Internal cleanup helper
 *
 * Called by PEB_$PROC_CLEANUP to do the actual cleanup work.
 * Waits for PEB to become not busy, removes MMU mappings,
 * and clears per-process state.
 *
 * Original address: 0x00E70954 (148 bytes)
 */
void peb_$cleanup_internal(void);

/*
 * ============================================================================
 * External Dependencies
 * ============================================================================
 */

/* From proc1 module */
extern uint16_t PROC1_$CURRENT;     /* Current process ID */
extern uint16_t PROC1_$AS_ID;       /* Current address space ID */

/* From mmu module */
void MMU_$INSTALL(uint32_t ppn, uint32_t va, uint32_t flags);
void MMU_$INSTALL_PRIVATE(uint32_t ppn, uint32_t va, uint16_t asid, uint32_t flags);
void MMU_$REMOVE(uint32_t ppn);

/* From fim module */
extern void FIM_$FLINE(void);       /* F-line exception handler */
extern void FIM_$UII(void);         /* Unimplemented integer instruction */
extern void FIM_$SPURIOUS_INT(void); /* Spurious interrupt handler */
extern void FIM_$EXIT(void);        /* Interrupt exit routine */

/* From ec module */
void EC_$INIT(ec_$eventcount_t *ec);

/* From misc module */
void CRASH_SYSTEM(const char *msg);

/* From fp module */
extern uint32_t FP_$SAVEP;          /* FP save pending pointer */

/* From io module */
void IO_$USE_INT_STACK(uint16_t sr);
extern void *IO_$SAVED_OS_SP;

/* From dxm module */
void DXM_$ADD_SIGNAL(uint16_t, uint16_t, uint16_t, uint32_t, uint8_t, status_$t *);

#endif /* PEB_INTERNAL_H */
