/*
 * peb/peb.h - Performance Enhancement Board (PEB) Module
 *
 * The PEB is an Apollo-fabricated floating point accelerator board that
 * provides hardware FPU functionality as an alternative to the MC68881/68882.
 * It includes writable control store (WCS) that holds microcode loaded from
 * /sys/peb2_microcode at boot time.
 *
 * Hardware resources:
 * - 0xFF7000: PEB control register (PEB_CTL)
 * - 0xFF7400: Private per-process PEB register mirror
 * - 0xFF7800: WCS (Writable Control Store) for microcode
 *
 * The PEB maintains per-process FP context in a wired data area, indexed
 * by address space ID. Each process gets 28 (0x1C) bytes of FP register
 * state storage.
 *
 * The PEB is mutually exclusive with the MC68881 - only one FPU type
 * can be active at a time.
 *
 * Original source was likely Pascal, converted to C.
 */

#ifndef PEB_H
#define PEB_H

#include "base/base.h"

/*
 * ============================================================================
 * Status Codes (module 0x24 - PEB)
 * ============================================================================
 */
#define status_$peb_fpu_is_hung 0x00240001 /* PEB FPU not responding */
#define status_$peb_interrupt 0x00240002 /* PEB interrupt (no specific error)  \
                                          */
#define status_$peb_fp_overflow 0x00240003  /* Floating point overflow */
#define status_$peb_fp_underflow 0x00240004 /* Floating point underflow */
#define status_$peb_div_by_zero 0x00240005  /* Division by zero */
#define status_$peb_fp_loss_of_significance                                    \
  0x00240006                                        /* Loss of significance */
#define status_$peb_fp_hw_error 0x00240007          /* FP hardware error */
#define status_$peb_unimplemented_opcode 0x00240008 /* Unimplemented opcode */
#define status_$peb_wcs_verify_failed                                          \
  0x00240009 /* Failed to verify peb microcode */

/* Error codes for TEST_PARITY_ERR */
#define status_$peb_no_parity_error 0x0012000F /* No parity error detected */
#define status_$peb_parity_error 0x0012001B    /* Parity error detected */

/*
 * ============================================================================
 * PEB Info Flags (returned by PEB_$GET_INFO)
 * ============================================================================
 */

#define PEB_INFO_WCS_LOADED 0x80  /* WCS microcode loaded successfully */
#define PEB_INFO_M68881_MODE 0x40 /* Running in MC68881 emulation mode */
#define PEB_INFO_SAVEP_FLAG 0x20  /* Save pending flag */
#define PEB_INFO_UNKNOWN_08 0x08  /* Unknown flag */
#define PEB_INFO_UNKNOWN_10 0x10  /* Unknown flag */

/*
 * ============================================================================
 * PEB Register State
 * ============================================================================
 *
 * Per-process FP register state stored in wired memory at
 * PEB_$WIRED_DATA_START. Each address space has 28 (0x1C) bytes of state
 * storage.
 *
 * Layout of per-process state (28 bytes):
 *   +0x00: FP data register pair 1 (high, low) - 8 bytes
 *   +0x08: FP data register pair 2 (high, low) - 8 bytes
 *   +0x10: FP status register (4 bytes)
 *   +0x14: FP control/instruction register (4 bytes)
 *   +0x18: FP instruction counter (4 bytes)
 */

typedef struct peb_fp_state_t {
  uint32_t data_regs[4];  /* +0x00: FP data registers (2 pairs of 64-bit) */
  uint32_t status_reg;    /* +0x10: FP status register */
  uint32_t ctrl_reg;      /* +0x14: FP control register */
  uint32_t instr_counter; /* +0x18: FP instruction counter */
} peb_fp_state_t;

#define PEB_FP_STATE_SIZE 0x1C /* 28 bytes per process */
#define PEB_MAX_PROCESSES 58   /* 0x3A processes supported */

/*
 * ============================================================================
 * Hardware Register Addresses
 * ============================================================================
 */

#if defined(ARCH_M68K)
/* PEB control register - main control/status interface */
#define PEB_CTL (*(volatile uint16_t *)0xFF7000)

/* PEB status register byte (read status bits) */
#define PEB_STATUS_BYTE (*(volatile uint8_t *)0xFF7001)

/* PEB private mirror - per-process mapping for register access */
#define PEB_PRIVATE_BASE ((volatile uint32_t *)0xFF7400)

/* PEB status register offset from base */
#define PEB_STATUS_OFFSET 0xF4

/* WCS (Writable Control Store) base address */
#define PEB_WCS_BASE ((volatile uint16_t *)0xFF7800)

/* Global PEB data area base (contains flags and state) */
#define PEB_GLOBALS_BASE 0xE24C78

/* Wired data start - per-process FP state storage */
#define PEB_WIRED_DATA_ADDR 0xE84E80
#else
/* For non-m68k platforms, these will be provided by platform init */
extern volatile uint16_t *peb_ctl_reg;
extern volatile uint8_t *peb_status_byte;
extern volatile uint32_t *peb_private_base;
extern volatile uint16_t *peb_wcs_base;
extern uint32_t peb_globals_base;
extern uint32_t peb_wired_data_addr;

#define PEB_CTL (*peb_ctl_reg)
#define PEB_STATUS_BYTE (*peb_status_byte)
#define PEB_PRIVATE_BASE peb_private_base
#define PEB_STATUS_OFFSET 0xF4
#define PEB_WCS_BASE peb_wcs_base
#define PEB_GLOBALS_BASE peb_globals_base
#define PEB_WIRED_DATA_ADDR peb_wired_data_addr
#endif

/*
 * ============================================================================
 * PEB Control Register Bits
 * ============================================================================
 */

/* Control register bit masks */
#define PEB_CTL_BUSY 0x8000          /* PEB is busy (negative when busy) */
#define PEB_CTL_WCS_PAGE_MASK 0x03F0 /* WCS page select bits (4-9) */
#define PEB_CTL_WCS_PAGE_SHIFT 4
#define PEB_CTL_ENABLE 0x000D /* PEB enable bits */

/*
 * ============================================================================
 * PEB Status Register Bits
 * ============================================================================
 */

#define PEB_STATUS_INTERRUPT 0x04  /* Interrupt pending */
#define PEB_STATUS_PARITY_ERR 0x02 /* Parity error detected */

/* FP exception status bits (from offset 0xF4) */
#define PEB_EXC_OVERFLOW 0x01     /* Overflow */
#define PEB_EXC_UNDERFLOW 0x02    /* Underflow */
#define PEB_EXC_DIV_BY_ZERO 0x04  /* Division by zero */
#define PEB_EXC_LOSS_SIG 0x08     /* Loss of significance */
#define PEB_EXC_HW_ERROR 0x10     /* Hardware error */
#define PEB_EXC_UNIMP_OPCODE 0x20 /* Unimplemented opcode */
#define PEB_EXC_MASK 0x3F         /* All exception bits */

/*
 * ============================================================================
 * External Symbols
 * ============================================================================
 */

/* Per-process FP state storage (wired memory) */
extern peb_fp_state_t PEB_$WIRED_DATA_START[];

/* PEB status register shadow (cached copy of last interrupt status) */
extern uint32_t PEB_$STATUS_REG;

/*
 * ============================================================================
 * Function Prototypes - Public API
 * ============================================================================
 */

/*
 * PEB_$INIT - Initialize PEB subsystem
 *
 * Called during system boot to initialize the PEB hardware and data
 * structures. If MC68881 is present (indicated by M68881_EXISTS < 0),
 * the system uses 68881 mode instead and installs the FIM F-line handler.
 *
 * For PEB mode, this function:
 * 1. Initializes the PEB eventcount
 * 2. Zeroes all per-process FP state storage
 * 3. Installs MMU mappings for PEB registers
 * 4. Probes for PEB hardware and installs interrupt handler
 * 5. Loads WCS microcode if hardware present
 *
 * Original address: 0x00E31D0C (194 bytes)
 */
void PEB_$INIT(void);

/*
 * PEB_$INT - PEB interrupt handler
 *
 * Handles PEB interrupt signals. Called from the interrupt vector.
 * Checks for spurious interrupts, saves FP status, and signals
 * waiting processes via DXM.
 *
 * Original address: 0x00E2446C (110 bytes)
 */
void PEB_$INT(void);

/*
 * PEB_$TEST_PARITY_ERR - Test for PEB parity errors
 *
 * Checks if a parity error has occurred in the PEB hardware.
 * Returns status indicating parity error state.
 *
 * Parameters:
 *   status - Pointer to receive status code
 *            status_$peb_parity_error (0x12001B) if error detected
 *            status_$peb_no_parity_error (0x12000F) if no error
 *
 * Original address: 0x00E11FF4 (54 bytes)
 */
void PEB_$TEST_PARITY_ERR(status_$t *status);

/*
 * PEB_$LOAD_WCS - Load WCS microcode from /sys/peb2_microcode
 *
 * Loads the PEB microcode from the system file into the WCS.
 * Verifies the loaded code by reading it back. Also wires the
 * PEB data areas into memory.
 *
 * For 68881 mode (no PEB hardware), creates a temporary file
 * and maps it for the 68881 save area.
 *
 * Original address: 0x00E31FD8 (684 bytes)
 */
void PEB_$LOAD_WCS(void);

/*
 * PEB_$ASSOC - Associate PEB with current process
 *
 * Sets up the PEB for the current process by installing
 * MMU mappings for the PEB registers. Called during process
 * context switch to make PEB accessible.
 *
 * Original address: 0x00E5AD38 (108 bytes)
 */
void PEB_$ASSOC(void);

/*
 * PEB_$DISSOC - Disassociate PEB from current process
 *
 * Removes the PEB MMU mappings when switching away from a
 * process that was using the PEB.
 *
 * Original address: 0x00E5ADA4 (38 bytes)
 */
void PEB_$DISSOC(void);

/*
 * PEB_$GET_STATUS - Get PEB exception status
 *
 * Returns a status code indicating the type of FP exception
 * that occurred based on the PEB status register.
 *
 * Returns:
 *   Status code indicating exception type:
 *   - status_$peb_fp_overflow
 *   - status_$peb_fp_underflow
 *   - status_$peb_div_by_zero
 *   - status_$peb_fp_loss_of_significance
 *   - status_$peb_fp_hw_error
 *   - status_$peb_unimplemented_opcode
 *   - status_$peb_interrupt (if none of above)
 *
 * Original address: 0x00E5ADCA (150 bytes)
 */
status_$t PEB_$GET_STATUS(void);

/*
 * PEB_$LOAD_REGS - Load FP registers from memory
 *
 * Loads the FP hardware registers from the specified state buffer.
 *
 * Parameters:
 *   state - Pointer to FP state buffer (28 bytes)
 *
 * Original address: 0x00E5AE60 (70 bytes)
 */
void PEB_$LOAD_REGS(peb_fp_state_t *state);

/*
 * PEB_$UNLOAD_REGS - Unload FP registers to memory
 *
 * Saves the FP hardware registers to the specified state buffer.
 *
 * Parameters:
 *   state - Pointer to FP state buffer (28 bytes)
 *
 * Original address: 0x00E5AEA6 (70 bytes)
 */
void PEB_$UNLOAD_REGS(peb_fp_state_t *state);

/*
 * PEB_$GET_FP - Get FP context for address space
 *
 * Loads the FP registers from the saved state for the specified
 * address space ID.
 *
 * Parameters:
 *   asid - Pointer to address space ID
 *
 * Original address: 0x00E5AEEC (38 bytes)
 */
void PEB_$GET_FP(int16_t *asid);

/*
 * PEB_$PUT_FP - Put (save) FP context for address space
 *
 * Saves the current FP registers to the state buffer for the
 * specified address space ID.
 *
 * Parameters:
 *   asid - Pointer to address space ID
 *
 * Original address: 0x00E5AF12 (38 bytes)
 */
void PEB_$PUT_FP(int16_t *asid);

/*
 * PEB_$TOUCH - Touch PEB to establish context
 *
 * Called when accessing PEB registers to ensure proper context.
 * If the access address is in the PEB range and the PEB is not
 * currently mapped for this process, saves the previous owner's
 * state and loads this process's state.
 *
 * Parameters:
 *   addr - Pointer to access address being touched
 *
 * Returns:
 *   Non-zero (0xFF) if PEB context was established, 0 otherwise
 *
 * Original address: 0x00E70810 (320 bytes)
 */
uint8_t PEB_$TOUCH(uint32_t *addr);

/*
 * PEB_$GET_INFO - Get PEB subsystem information
 *
 * Returns information about the PEB subsystem state including
 * whether WCS is loaded, MC68881 mode, and other flags.
 *
 * Parameters:
 *   info_flags - Pointer to receive info flags byte
 *   info_byte  - Pointer to receive additional info byte
 *
 * Original address: 0x00E709E8 (84 bytes)
 */
void PEB_$GET_INFO(uint8_t *info_flags, uint8_t *info_byte);

/*
 * PEB_$PROC_CLEANUP - Clean up PEB state for process termination
 *
 * Called during process cleanup to release PEB resources.
 * Clears the per-process FP state storage.
 *
 * Original address: 0x00E752BC (32 bytes)
 */
void PEB_$PROC_CLEANUP(void);

#endif /* PEB_H */
