/*
 * MMU - Memory Management Unit Interface
 *
 * This module provides the interface to Domain/OS's reverse-mapped MMU
 * hardware. Unlike traditional MMUs that use forward page tables (virtual
 * to physical), this MMU uses an inverted page table where physical pages
 * contain mappings to virtual addresses.
 *
 * Key data structures:
 * - PTT (Page Translation Table) at 0x700000 - indexed by virtual address
 * - PFT (Page Frame Table) at 0xFFB800 - 4 bytes per physical page
 * - ASID table at 0xEC2800 - Address Space Identifier per physical page
 *
 * Note: The MMAPE (Memory Map Page Entry) at 0xEB2800 is a separate 16-byte
 * per-page structure managed by the MMAP layer (see mmap/mmap.h).
 *
 * MMU Control Registers (0xFFB400-0xFFB40B):
 * - 0xFFB400: PID/Privilege/Power register (CSR)
 * - 0xFFB402: Power/status control (the handbook's "FPU Owner Register",
 *             write-only ASID of the current FPU owner)
 * - 0xFFB403: Status register (bit 4 = normal mode)
 * - 0xFFB405: MCR control (M68010)
 * - 0xFFB407: MCR mask
 * - 0xFFB408: MCR control (M68020)
 * - 0xFFB409: Hardware revision
 * - 0xFFB40A: MMU Parity Register (word, 0xFFB40A-0xFFB40B)
 *
 * Register names and bit layouts are Apollo's own, from the "DN3xx" chapter
 * of the Domain Engineering Handbook (002398-04 Rev4, Jan87), pages 7-23
 * through 7-27; SAU2 is DN300/DN320/DN330.
 *
 * Original source was likely Pascal, converted to C.
 */

#ifndef MMU_H
#define MMU_H

#include "base/base.h"

/* MMU status codes (module 0x07) */
#define status_$mmu_miss 0x00070001

/*
 * PFT (Page Frame Table)
 *
 * Located at 0xFFB800, 4 bytes per physical page.
 * Format:
 *   Bytes 0-1: Hash chain link (bits 0-11) + flags (bits 12-15)
 *   Bytes 2-3: Virtual address info for this physical page
 *
 * The PFT implements a hash table for reverse translation (physical to
 * virtual). Multiple physical pages can map to the same PTT slot via hash
 * chaining.
 */

/* PFT flags in low word at offset +2 */
#define PFT_LINK_MASK 0x0FFF       /* Hash chain next link (PPN) */
#define PFT_FLAG_GLOBAL 0x1000     /* Global/shared mapping */
#define PFT_FLAG_REFERENCED 0x2000 /* Page has been accessed */
#define PFT_FLAG_MODIFIED 0x4000   /* Page has been modified */
#define PFT_FLAG_HEAD 0x8000       /* Head of hash chain */

/* PFT protection/flags in high word */
#define PFT_PROT_MASK 0x01F0 /* Protection bits */
#define PFT_PROT_SHIFT 4

/*
 * PTT (Page Translation Table) Entry
 *
 * Located at 0x700000, indexed by virtual address.
 * Each entry is 2 bytes containing the PPN (physical page number) of
 * the head of the hash chain for this virtual address.
 */
#define PTT_PPN_MASK 0x0FFF /* Physical page number */

/*
 * ASID (Address Space Identifier) Table
 *
 * Located at 0xEC2800, 2 bytes per physical page.
 * Contains the virtual address that maps to this physical page.
 */

/*
 * MMU Global Variables
 */
/*
 * Layout of the MMU module data block reached through A5 in MMU_$INIT
 * (A5 = 0xE23D2E).  Recovered from MMU_$INIT (0xE23D38) and the PC-relative
 * reads in MMU_$VTOP / MMU_$INSTALL* / MMU_$REMOVE_VIRTUAL.
 */
typedef struct mmu_globals_t {
  uint16_t m68020;      /* 0x00 (0xE23D2E): 68020+ boolean, in the HIGH byte */
  uint32_t va_ptt_mask; /* 0x02 (0xE23D30): VA to PTT offset mask */
  uint16_t va_shift;    /* 0x06 (0xE23D34): VA shift count (MMU_$VA_SHIFT) */
  uint16_t ptt_shift;   /* 0x08 (0xE23D36): PTT shift count (MMU_$PTT_SHIFT) */
} __attribute__((packed)) mmu_globals_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(mmu_globals_t, m68020) == 0x00,
               "mmu_globals_t.m68020 must be at 0x00 (0xE23D2E)");
_Static_assert(__builtin_offsetof(mmu_globals_t, va_ptt_mask) == 0x02,
               "mmu_globals_t.va_ptt_mask must be at 0x02 (0xE23D30)");
_Static_assert(__builtin_offsetof(mmu_globals_t, va_shift) == 0x06,
               "mmu_globals_t.va_shift must be at 0x06 (0xE23D34)");
_Static_assert(__builtin_offsetof(mmu_globals_t, ptt_shift) == 0x08,
               "mmu_globals_t.ptt_shift must be at 0x08 (0xE23D36)");
_Static_assert(sizeof(mmu_globals_t) == 0x0A, "mmu_globals_t must be 10 bytes");
#endif

/*
 * Architecture-independent macros for MMU access
 * These isolate m68k-specific memory layout
 */
/* TODO(source-o56c): the MMU_ASM cells, MMU_$PTTX and the MMU registers are still absolute on the target (tools/check_guards.py exemption). */
#if defined(ARCH_M68K)
/* PTT - Page Translation Table (indexed by virtual address) */
#define PTT_BASE ((uint16_t *)0x700000)

/* PFT - Page Frame Table (4 bytes per physical page) */
#define PFT_BASE ((uint32_t *)0xFFB800)

/* ASID table - 2 bytes per physical page */
#define ASID_TABLE_BASE ((uint16_t *)0xEC2800)

/* MMU control registers */
#define MMU_CSR (*(volatile uint16_t *)0xFFB400)       /* PID/Priv/Power */
#define MMU_POWER_REG (*(volatile uint16_t *)0xFFB402) /* Power control */
/*
 * The first (most significant) byte of MMU_POWER_REG on its own.
 * MMU_$INSTALL_ASID restores it with a byte store, not a word read-modify-
 * write: `move.b (0x00e218d5).l,(0x00ffb402).l` (0x00E2421C).
 */
#define MMU_POWER_REG_BYTE (*(volatile uint8_t *)0xFFB402)
#define MMU_STATUS_REG (*(volatile uint8_t *)0xFFB403) /* Status */
#define MMU_MCR_M68010 (*(volatile uint8_t *)0xFFB405) /* MCR for 68010 */
#define MMU_MCR_MASK (*(volatile uint8_t *)0xFFB407)   /* MCR mask */
#define MMU_MCR_M68020 (*(volatile uint8_t *)0xFFB408) /* MCR for 68020 */
#define DN330_MMU_HARDWARE_REV (*(volatile uint8_t *)0xFFB409) /* HW revision  \
                                                                */

/*
 * MMU Parity Register [800A-800B] = 0xFFB40A, DN3xx only.
 *
 * Domain Engineering Handbook 002398-04 Rev4 (Jan87), DN3xx chapter, page
 * 7-27, gives the word layout:
 *
 *    15      14      13      12     11                             0
 *   +-------+-------+-------+-------+------------------------------+
 *   | WWP   | PFE   | PTTPE | PFTPE | PFTX (PFT Parity Error Index)|
 *   +-------+-------+-------+-------+------------------------------+
 *   |--R/W--|--R/W--|--CLR--|--CLR--|------------R/O---------------|
 *
 *   bit 15  Write Wrong MMU Parity (both PFT and PTT)
 *   bit 14  MMU Parity Fault Enable (MMU PFE)
 *   bit 13  PTT Parity Error
 *   bit 12  PFT Parity Error
 *   bits 11..0  PFTX, the PFT index of the failing entry
 *
 * The handbook adds: "Bus error occurs on parity operation if MMU PFE is
 * set, and bits 12 and 13 were clear."
 *
 * FIM_$BUS_ERR (fim/sau2/bus_err.s) is the only code in the image that
 * touches the register.  It reads the HIGH byte of the word - m68k is
 * big-endian, so the byte at 0xFFB40A carries word bits 15..8 - and tests
 * byte bit 5 (= word bit 13, PTT parity) then byte bit 4 (= word bit 12,
 * PFT parity), choosing status_$mmu_ptt_parity_error,
 * status_$mmu_pft_parity_error or, when neither is set,
 * status_$mmu_timeout.  Those three status texts ("ptt parity error",
 * "pft parity error", "mmu timeout" - SR10.4 status database, module 0x07)
 * confirm the bit assignment independently of the handbook.  It then
 * writes the word 0x4000 back: PFE set, WWP clear, and 0 into the two CLR
 * bits, which acknowledges the latched error and re-arms parity faults.
 */
#define MMU_PARITY_REG (*(volatile uint16_t *)0xFFB40A)

#define MMU_PARITY_PFTX_MASK 0x0FFF /* bits 11..0: failing PFT index */
#define MMU_PARITY_PFT_ERR 0x1000   /* bit 12: PFT parity error (CLR) */
#define MMU_PARITY_PTT_ERR 0x2000   /* bit 13: PTT parity error (CLR) */
#define MMU_PARITY_PFE 0x4000       /* bit 14: MMU parity fault enable */
#define MMU_PARITY_WRITE_WRONG 0x8000 /* bit 15: write wrong MMU parity */

/* Value FIM_$BUS_ERR writes to acknowledge a latched MMU parity error. */
#define MMU_PARITY_ACK MMU_PARITY_PFE

/* MMU status codes selected from this register (module 0x07). */
#define status_$mmu_ptt_parity_error 0x00070004
#define status_$mmu_pft_parity_error 0x00070005
#define status_$mmu_timeout 0x00070006

/*
 * MMU module globals.
 *
 * MMU_$INIT (0xE23D38) establishes its module base with
 * "lea (-0xe,PC),A5" at 0xE23D3A, giving A5 = 0xE23D2E; it then writes
 * (0x2,A5), (0x6,A5) and (0x8,A5).  Combined with the PC-relative reads in
 * MMU_$VTOP (0xE24118 -> 0xE23D30, 0xE24124 -> 0xE23D34, 0xE2413A ->
 * 0xE23D2C) this fixes the layout as:
 *
 *   0xE23D28  MMAP_$RMT_LIMIT (4 bytes, owned by mmap)
 *   0xE23D2C  MMU_$PID_PRIV          (word)
 *   0xE23D2E  M68020                 (word)
 *   0xE23D30  VA_TO_PTT_OFFSET_MASK  (long)
 *   0xE23D34  MMU_$VA_SHIFT          (word)
 *   0xE23D36  MMU_$PTT_SHIFT         (word)
 */
#define MMU_$PID_PRIV (*(uint16_t *)0xE23D2C)
#define M68020 (*(uint16_t *)0xE23D2E)
#define VA_TO_PTT_OFFSET_MASK (*(uint32_t *)0xE23D30)
#define MMU_$VA_SHIFT (*(uint16_t *)0xE23D34)
#define MMU_$PTT_SHIFT (*(uint16_t *)0xE23D36)
/* The byte MMU_$SET_SYSREV stores (0x00E24276 `move.b ...,(0x3,A0)` with
 * A0 = 0xE2426E): the LOW byte of the MMU_$SYSTEM_REV longword. */
#define MMU_SYSREV (*(uint8_t *)0xE24271)

/* Cache control MCR shadow (for 68010) */
#define MCR_SHADOW (*(uint8_t *)0xE242D2)
#else
/* For non-m68k platforms, these will be provided by platform init */
extern uint16_t *mmu_ptt_base;
extern uint32_t *mmu_pft_base;
extern uint16_t *mmu_asid_table_base;
extern volatile uint16_t *mmu_csr;
extern volatile uint16_t *mmu_power_reg;
extern volatile uint8_t *mmu_status_reg;
extern volatile uint8_t *mmu_mcr_m68010;
extern volatile uint8_t *mmu_mcr_mask;
extern volatile uint8_t *mmu_mcr_m68020;
extern volatile uint8_t *mmu_hw_rev;

extern uint16_t mmu_m68020;
extern uint16_t mmu_pid_priv;
extern uint32_t mmu_va_to_ptt_mask;
extern uint16_t mmu_va_shift;
extern uint16_t mmu_ptt_shift;
extern uint8_t mmu_sysrev;
extern uint16_t mmu_current_asid;
extern uint8_t mmu_mcr_shadow;

#define PTT_BASE mmu_ptt_base
#define PFT_BASE mmu_pft_base
#define ASID_TABLE_BASE mmu_asid_table_base
#define MMU_CSR (*mmu_csr)
#define MMU_POWER_REG (*mmu_power_reg)
#define MMU_POWER_REG_BYTE (*(volatile uint8_t *)mmu_power_reg)
#define MMU_STATUS_REG (*mmu_status_reg)
#define MMU_MCR_M68010 (*mmu_mcr_m68010)
#define MMU_MCR_MASK (*mmu_mcr_mask)
#define MMU_MCR_M68020 (*mmu_mcr_m68020)
#define DN330_MMU_HARDWARE_REV (*mmu_hw_rev)

#define M68020 mmu_m68020
#define MMU_$PID_PRIV mmu_pid_priv
#define VA_TO_PTT_OFFSET_MASK mmu_va_to_ptt_mask
#define MMU_$VA_SHIFT mmu_va_shift
#define MMU_$PTT_SHIFT mmu_ptt_shift
#define MMU_SYSREV mmu_sysrev
#define MCR_SHADOW mmu_mcr_shadow
#endif

/* MMU data */
extern uint32_t MMU_$SYSTEM_REV;

/*
 * The M68020 flag word is read two different ways by the original code:
 *
 *   - "tst.w M68020" (MMU_$INIT 0xE23D3E, MMU_$INSTALL 0xE24068,
 *     MMU_$INSTALL_LIST 0xE24004, MMU_$INSTALL_PRIVATE 0xE23FA2)
 *     tests the whole word;
 *   - "move.b (d,PC),Dn" (MMU_$PTOV 0xE241E0, MMU_$MCR_CHANGE 0xE242A4)
 *     and "tst.b M68020" (MST_$INIT) read only the HIGH byte of the word,
 *     which is where the Domain boolean actually lives.
 *
 * Both forms are provided so each call site can mirror its own instruction
 * without casting a pointer to a byte.
 */
#define M68020_IS_020_W() (M68020 != 0)
#define M68020_IS_020_B() (((M68020 >> 8) & 0xFF) != 0)

/* Get PTT entry for a virtual address */
#define PTT_FOR_VA(va)                                                         \
  ((uint16_t *)((uint32_t)PTT_BASE + ((va) & VA_TO_PTT_OFFSET_MASK)))

/* Get PFT entry for a physical page number */
#define PFT_FOR_PPN(ppn) ((uint32_t *)((char *)PFT_BASE + ((ppn) << 2)))

/* Get ASID entry for a physical page number */
#define ASID_FOR_PPN(ppn) (ASID_TABLE_BASE[(ppn)])

/*
 * PMAPE (Page Map Page Entry) macros
 * These access the PFT entries but with different pointer types:
 * - PMAPE_FOR_PPN returns uint32_t* for full 32-bit access
 * - PMAPE_FOR_VPN returns uint16_t* for accessing individual words
 * Note: In the mmap layer, "vpn" is actually the physical page index.
 */
#define PMAPE_FOR_PPN(ppn) ((uint32_t *)((char *)PFT_BASE + ((ppn) << 2)))
#define PMAPE_FOR_VPN(vpn) ((uint16_t *)((char *)PFT_BASE + ((vpn) << 2)))

/* PMAPE flag definitions (aliases for PFT flags) */
#define PMAPE_LINK_MASK 0x0FFF       /* Hash chain next link (PPN) */
#define PMAPE_FLAG_GLOBAL 0x1000     /* Global/shared mapping */
#define PMAPE_FLAG_REFERENCED 0x2000 /* Page has been accessed */
#define PMAPE_FLAG_MODIFIED 0x4000   /* Page has been modified */
#define PMAPE_FLAG_HEAD 0x8000       /* Head of hash chain */

/* CSR (Control/Status Register) bit definitions */
#define CSR_PID_MASK 0xFF00       /* Address space ID */
#define CSR_PRIV_BIT 0x0001       /* Privilege mode */
#define CSR_PTT_ACCESS_BIT 0x0002 /* Enable PTT access */

/*
 * Interrupt control macros (DISABLE_INTERRUPTS, ENABLE_INTERRUPTS,
 * GET_SR, SET_SR, SR_IPL_MASK, SR_IPL_DISABLE_ALL) are provided by
 * arch/arch.h via base/base.h. Do NOT redefine them here.
 */

/*
 * Note: Internal helper functions (mmu_$installi, mmu_$remove_internal, etc.)
 * are declared in mmu_internal.h. Include that header in .c files that need
 * access to internal MMU functions.
 *
 * CACHE_$CLEAR is declared in cache/cache.h.
 * MMAP_$LPPN/MMAP_$HPPN are declared in mmap/mmap.h.
 */

/*
 * Function prototypes - Public API
 */

/* Initialize MMU subsystem */
void MMU_$INIT(void);

/* Remove a mapping for a physical page number */
void MMU_$REMOVE(uint32_t ppn);

/* Remove mappings for a list of physical pages */
void MMU_$REMOVE_LIST(uint32_t *ppn_array, uint16_t count);

/* Remove virtual address mappings */
void MMU_$REMOVE_VIRTUAL(uint32_t va, uint16_t count, uint16_t asid,
                         uint32_t *ppn_array, uint16_t *removed_count);

/* Remove all mappings for an address space ID */
void MMU_$REMOVE_ASID(uint16_t asid);

/*
 * MMU install flags packing macro
 * The flags parameter encodes ASID and protection bits in a packed format:
 *   - Byte 1 (bits 16-23): ASID
 *   - Byte 3 (bits 0-7): Protection bits
 */
#define MMU_FLAGS(asid, prot) (((uint32_t)(asid) << 16) | (uint32_t)(prot))

/* Install a mapping (private, no global bit) */
void MMU_$INSTALL_PRIVATE(uint32_t ppn, uint32_t va, uint32_t flags);

/* Install mappings for a list of physical pages */
void MMU_$INSTALL_LIST(uint16_t count, uint32_t *ppn_array, uint32_t va,
                       uint32_t flags);

/* Install a mapping with global bit */
void MMU_$INSTALL(uint32_t ppn, uint32_t va, uint32_t flags);

/* Translate virtual address to physical page number */
uint32_t MMU_$VTOP(uint32_t va, status_$t *status);

/* Translate physical page number to virtual address */
uint32_t MMU_$PTOV(uint32_t ppn);

/* Set the Control/Status Register (CSR) privilege bits */
void MMU_$SET_CSR(uint16_t csr_val);

/* Install an Address Space ID (switch address spaces) */
void MMU_$INSTALL_ASID(uint16_t asid);

/* Set protection bits for a physical page */
uint16_t MMU_$SET_PROT(uint32_t ppn, uint16_t prot);

/* Clear the "used/referenced" bit for a physical page */
void MMU_$CLR_USED(uint32_t ppn);

/* Set the MMU system revision from hardware */
void MMU_$SET_SYSREV(void);

/* Check if MMU is in normal mode */
int8_t MMU_$NORMAL_MODE(void);

/* Check if power-off mode is active */
int8_t MMU_$POWER_OFF(void);

/* Mark virtual address as cache-inhibited
 * Original address: 0x00e2429e
 */
void MMU_$CACHE_INHIBIT_VA(uint32_t va);

/* Toggle MCR (Memory Control Register) bits */
void MMU_$MCR_CHANGE(uint16_t bit);

/* Translate VA to PA, crash if translation fails */
uint32_t mmu_$vtop_or_crash(uint32_t va);

/* Zero a physical page */
void ZERO_PAGE(uint32_t ppn);

#endif /* MMU_H */
