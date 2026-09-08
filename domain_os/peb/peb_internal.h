/*
 * peb/peb_internal.h - PEB Internal Definitions
 *
 * Internal data structures, globals, and helper functions for the PEB
 * subsystem. Not for use by external modules.
 */

#ifndef PEB_INTERNAL_H
#define PEB_INTERNAL_H

#include "peb/peb.h"
#include "dxm/dxm.h"
#include "ec/ec.h"
#include "fim/fim.h"
#include "fp/fp.h"
#include "io/io.h"
#include "misc/crash_system.h"
#include "mmu/mmu.h"
#include "proc1/proc1.h"
#include "prom/prom.h"
#include "uid/uid.h"   /* UID_$NIL */

/*
 * ============================================================================
 * Constant pointer cells in the code segment
 * ============================================================================
 * The four longwords at 0x00E322DC..0x00E322EB that PEB_$LOAD_WCS passes to
 * MST_$WIRE_AREA by address are file statics in peb/load_wcs.c - they live
 * in the PEB_UNWIRED code segment and only that routine reaches them
 * (bead source-f3dk).  Their contents are the map symbols
 * PEB_$WIRED_DATA_START / PEB_$WIRED_DATA_END / PEB_$WIRED_PROC_START
 * (= PEB_$TOUCH) / PEB_$WIRED_PROC_END.
 *
 * The pair PEB_$INIT hands io_$probe - the word at 0x00E31DCE and the
 * longword at 0x00E31DD0 - are file-statics in peb/init.c, its only user
 * (source-fzke).
 */

/*
 * ============================================================================
 * Global Data Structures
 * ============================================================================
 *
 * PEB global data is located at 0xE24C78.
 * Layout:
 *   +0x00: Reserved (8 bytes)
 *   +0x08: Event counter (12 bytes, EC_$INIT'd at 00e31d12)
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
  /* PEB_$INIT calls EC_$INIT with 0xE24C80 (00e31d12 `pea (0xe24c80).l`),
   * i.e. globals + 0x08, and ec_$eventcount_t is 12 bytes -- so the counter
   * runs 0x08..0x13 and owner_pid follows at 0x14 (00e5ad42
   * `move.w (0x00e20608).l,(0x14,A0)`).  The earlier layout put the counter
   * at 0x00 and reserved1 at 0x08, which overlapped both. */
  uint8_t reserved0[8];        /* +0x00: Reserved */
  ec_$eventcount_t eventcount; /* +0x08: PEB event counter */
  uint16_t owner_pid;          /* +0x14: Current owner process ID */
  uint16_t owner_asid;         /* +0x16: Current owner AS ID */
  uint16_t ctl_shadow;         /* +0x18: PEB_CTL shadow register */
  uint8_t installed;           /* +0x1A: PEB hardware installed */
  uint8_t wcs_loaded;          /* +0x1B: WCS microcode loaded */
  uint8_t savep_flag;          /* +0x1C: Save pending flag */
  uint8_t flag_1d;             /* +0x1D: Unknown flag */
  uint8_t info_byte;           /* +0x1E: Info byte */
  uint8_t mmu_installed;       /* +0x1F: MMU mappings installed */
  uint8_t m68881_save_flag;    /* +0x20: MC68881 save flag */
  uint8_t flag_21;             /* +0x21: Unknown flag */
} peb_globals_t;

/* Layout recovered from the disassembly -- see the field comments above.
 * Guarded: the embedded ec_$eventcount_t holds two native pointers, so the
 * record is 8 bytes longer from +0x14 on a 64-bit host. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(peb_globals_t, reserved0) == 0x00, "peb_globals_t.reserved0");
_Static_assert(__builtin_offsetof(peb_globals_t, eventcount) == 0x08, "peb_globals_t.eventcount");
_Static_assert(__builtin_offsetof(peb_globals_t, owner_pid) == 0x14, "peb_globals_t.owner_pid");
_Static_assert(__builtin_offsetof(peb_globals_t, owner_asid) == 0x16, "peb_globals_t.owner_asid");
_Static_assert(__builtin_offsetof(peb_globals_t, ctl_shadow) == 0x18, "peb_globals_t.ctl_shadow");
_Static_assert(__builtin_offsetof(peb_globals_t, installed) == 0x1A, "peb_globals_t.installed");
_Static_assert(__builtin_offsetof(peb_globals_t, wcs_loaded) == 0x1B, "peb_globals_t.wcs_loaded");
_Static_assert(__builtin_offsetof(peb_globals_t, savep_flag) == 0x1C, "peb_globals_t.savep_flag");
_Static_assert(__builtin_offsetof(peb_globals_t, flag_1d) == 0x1D, "peb_globals_t.flag_1d");
_Static_assert(__builtin_offsetof(peb_globals_t, info_byte) == 0x1E, "peb_globals_t.info_byte");
_Static_assert(__builtin_offsetof(peb_globals_t, mmu_installed) == 0x1F, "peb_globals_t.mmu_installed");
_Static_assert(__builtin_offsetof(peb_globals_t, m68881_save_flag) == 0x20, "peb_globals_t.m68881_save_flag");
_Static_assert(__builtin_offsetof(peb_globals_t, flag_21) == 0x21, "peb_globals_t.flag_21");
_Static_assert(sizeof(peb_globals_t) == 0x22,
               "peb_globals_t: fields end at 0x21 (flag_21)");
#endif

/*
 * Global PEB data
 * Address: 0xE24C78
 */
#if defined(ARCH_M68K)
#define PEB_GLOBALS (*(peb_globals_t *)0xE24C78)
#else
extern peb_globals_t peb_globals;
#define PEB_GLOBALS peb_globals
#endif

/*
 * Convenience macros for global fields
 */
#define PEB_$OWNER_PID PEB_GLOBALS.owner_pid
#define PEB_$OWNER_ASID PEB_GLOBALS.owner_asid
#define PEB_$CTL_SHADOW PEB_GLOBALS.ctl_shadow
#define PEB_$INSTALLED PEB_GLOBALS.installed
#define PEB_$WCS_LOADED PEB_GLOBALS.wcs_loaded
#define PEB_$SAVEP_FLAG PEB_GLOBALS.savep_flag
#define PEB_$MMU_INSTALLED PEB_GLOBALS.mmu_installed
#define PEB_$M68881_SAVE_FLAG PEB_GLOBALS.m68881_save_flag
#define PEB_$INFO_BYTE PEB_GLOBALS.info_byte
#define PEB_$EVENTCOUNT PEB_GLOBALS.eventcount

/*
 * MC68881 existence flag
 * Set negative (<0) if MC68881 is present instead of PEB
 * Address: 0xE8180C
 */
#if defined(ARCH_M68K)
#define M68881_EXISTS (*(volatile int8_t *)0xE8180C)
#else
extern volatile int8_t m68881_exists;
#define M68881_EXISTS m68881_exists
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
  uint16_t word0; /* +0x00 */
  uint16_t word1; /* +0x02 */
  uint32_t word2; /* +0x04 */
} peb_wcs_entry_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(peb_wcs_entry_t, word0) == 0x00, "peb_wcs_entry_t.word0");
_Static_assert(__builtin_offsetof(peb_wcs_entry_t, word1) == 0x02, "peb_wcs_entry_t.word1");
_Static_assert(__builtin_offsetof(peb_wcs_entry_t, word2) == 0x04, "peb_wcs_entry_t.word2");

/*
 * WCS microcode file header:
 *   +0x00: Start address (2 bytes)
 *   +0x02: Entry count (2 bytes)
 *   +0x04: First entry follows
 */

typedef struct peb_wcs_header_t {
  uint16_t start_addr;  /* +0x00: Starting WCS address */
  uint16_t entry_count; /* +0x02: Number of entries */
                        /* peb_wcs_entry_t entries[] follow */
} peb_wcs_header_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(peb_wcs_header_t, start_addr) == 0x00, "peb_wcs_header_t.start_addr");
_Static_assert(__builtin_offsetof(peb_wcs_header_t, entry_count) == 0x02, "peb_wcs_header_t.entry_count");

/*
 * ============================================================================
 * Hardware Register Access Helpers
 * ============================================================================
 */

/*
 * Get FP state pointer for address space ID
 * Each AS has 0x1C (28) bytes of state storage
 */
static inline peb_fp_state_t *peb_get_fp_state(int16_t asid) {
  return &PEB_$WIRED_DATA_START[asid];
}

/*
 * PEB register offsets (from base 0x7000 or 0xFF7400)
 */
#define PEB_REG_CTRL 0x00        /* Control register */
#define PEB_REG_DATA_IN_0 0x8C   /* Data input register 0 */
#define PEB_REG_DATA_IN_1 0x90   /* Data input register 1 */
#define PEB_REG_DATA_OUT_0 0x94  /* Data output register 0 */
#define PEB_REG_DATA_OUT_1 0x98  /* Data output register 1 */
#define PEB_REG_STAT_IN_0 0x1D0  /* Status input 0 */
#define PEB_REG_STAT_IN_1 0x1D4  /* Status input 1 */
#define PEB_REG_STAT_OUT_0 0x1B0 /* Status output 0 */
#define PEB_REG_STAT_OUT_1 0x1B4 /* Status output 1 */
#define PEB_REG_STATUS 0xF4      /* Exception status register */
#define PEB_REG_CTRL_IN 0x84     /* Control input register */
#define PEB_REG_CTRL_OUT 0x104   /* Control output register */
#define PEB_REG_MISC 0x1DC       /* Misc register */

/*
 * ============================================================================
 * Error Messages
 * ============================================================================
 */

extern status_$t PEB_interrupt;
extern status_$t PEB_FPU_Is_Hung_Err;
extern status_$t PEB_WCS_Verify_Failed_Err;

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

#endif /* PEB_INTERNAL_H */
