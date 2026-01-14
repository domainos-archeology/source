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
 * - PMAPE (Physical Memory Attribute Page Entry) at 0xFFB800 - 4 bytes per physical page
 * - ASID table at 0xEC2800 - Address Space Identifier per physical page
 *
 * MMU Control Registers (0xFFB400-0xFFB409):
 * - 0xFFB400: PID/Privilege/Power register (CSR)
 * - 0xFFB402: Power/status control
 * - 0xFFB403: Status register (bit 4 = normal mode)
 * - 0xFFB405: MCR control (M68010)
 * - 0xFFB407: MCR mask
 * - 0xFFB408: MCR control (M68020)
 * - 0xFFB409: Hardware revision
 *
 * Original source was likely Pascal, converted to C.
 */

#ifndef MMU_H
#define MMU_H

#include "base/base.h"

/* MMU status codes (module 0x07) */
#define status_$mmu_miss        0x00070001

/*
 * PMAPE (Physical Memory Attribute Page Entry)
 *
 * Located at 0xFFB800, 4 bytes per physical page.
 * Format:
 *   Bytes 0-1: Hash chain link (bits 0-11) + flags (bits 12-15)
 *   Bytes 2-3: Virtual address info for this physical page
 *
 * The PMAPE implements a hash table for reverse translation.
 * Multiple physical pages can map to the same PTT slot via hash chaining.
 */

/* PMAPE flags in low word at offset +2 */
#define PMAPE_LINK_MASK         0x0FFF  /* Hash chain next link (PPN) */
#define PMAPE_FLAG_GLOBAL       0x1000  /* Global/shared mapping */
#define PMAPE_FLAG_REFERENCED   0x2000  /* Page has been accessed */
#define PMAPE_FLAG_MODIFIED     0x4000  /* Page has been modified */
#define PMAPE_FLAG_HEAD         0x8000  /* Head of hash chain */

/* PMAPE protection/flags in high word */
#define PMAPE_PROT_MASK         0x01F0  /* Protection bits */
#define PMAPE_PROT_SHIFT        4

/*
 * PTT (Page Translation Table) Entry
 *
 * Located at 0x700000, indexed by virtual address.
 * Each entry is 2 bytes containing the PPN (physical page number) of
 * the head of the hash chain for this virtual address.
 */
#define PTT_PPN_MASK            0x0FFF  /* Physical page number */

/*
 * ASID (Address Space Identifier) Table
 *
 * Located at 0xEC2800, 2 bytes per physical page.
 * Contains the virtual address that maps to this physical page.
 */

/*
 * MMU Global Variables
 */
typedef struct mmu_globals_t {
    uint16_t    m68020;             /* 0x00: Non-zero if 68020+ */
    uint16_t    pid_priv;           /* 0x02: PID and privilege bits */
    uint32_t    va_ptt_mask;        /* 0x04: VA to PTT offset mask */
    uint8_t     ptt_shift;          /* 0x08: PTT shift value */
    uint8_t     asid_shift;         /* 0x09: ASID shift value */
    uint8_t     mmu_sysrev;         /* 0x0A: MMU hardware revision */
    uint8_t     reserved;           /* 0x0B: Padding */
} mmu_globals_t;

/*
 * Architecture-independent macros for MMU access
 * These isolate m68k-specific memory layout
 */
#if defined(M68K)
    /* PTT - Page Translation Table (indexed by virtual address) */
    #define PTT_BASE                ((uint16_t*)0x700000)

    /* PMAPE - Physical Memory Attribute Page Entry (4 bytes per physical page) */
    #define MMU_PMAPE_BASE          ((uint32_t*)0xFFB800)

    /* ASID table - 2 bytes per physical page */
    #define ASID_TABLE_BASE         ((uint16_t*)0xEC2800)

    /* MMU control registers */
    #define MMU_CSR                 (*(volatile uint16_t*)0xFFB400)  /* PID/Priv/Power */
    #define MMU_POWER_REG           (*(volatile uint16_t*)0xFFB402)  /* Power control */
    #define MMU_STATUS_REG          (*(volatile uint8_t*)0xFFB403)   /* Status */
    #define MMU_MCR_M68010          (*(volatile uint8_t*)0xFFB405)   /* MCR for 68010 */
    #define MMU_MCR_MASK            (*(volatile uint8_t*)0xFFB407)   /* MCR mask */
    #define MMU_MCR_M68020          (*(volatile uint8_t*)0xFFB408)   /* MCR for 68020 */
    #define DN330_MMU_HARDWARE_REV  (*(volatile uint8_t*)0xFFB409)   /* HW revision */

    /* MMU global state (at 0xE23D2A and nearby) */
    #define M68020                  (*(uint16_t*)0xE23D2A)
    #define MMU_$PID_PRIV           (*(uint16_t*)0xE23D2A)  /* Note: overlaps M68020 */
    #define VA_TO_PTT_OFFSET_MASK   (*(uint32_t*)0xE23D2E)
    #define MMU_SYSREV              (*(uint8_t*)0xE2426F)

    /* Current address space ID (from PROC1 module) */
    #define PROC1_$AS_ID            (*(uint16_t*)0xE2060A)

    /* Cache control MCR shadow (for 68010) */
    #define MCR_SHADOW              (*(uint8_t*)0xE242D2)
#else
    /* For non-m68k platforms, these will be provided by platform init */
    extern uint16_t*        mmu_ptt_base;
    extern uint32_t*        mmu_pmape_base;
    extern uint16_t*        mmu_asid_table_base;
    extern volatile uint16_t* mmu_csr;
    extern volatile uint16_t* mmu_power_reg;
    extern volatile uint8_t*  mmu_status_reg;
    extern volatile uint8_t*  mmu_mcr_m68010;
    extern volatile uint8_t*  mmu_mcr_mask;
    extern volatile uint8_t*  mmu_mcr_m68020;
    extern volatile uint8_t*  mmu_hw_rev;

    extern uint16_t         mmu_m68020;
    extern uint16_t         mmu_pid_priv;
    extern uint32_t         mmu_va_to_ptt_mask;
    extern uint8_t          mmu_sysrev;
    extern uint16_t         mmu_current_asid;
    extern uint8_t          mmu_mcr_shadow;

    #define PTT_BASE                mmu_ptt_base
    #define MMU_PMAPE_BASE          mmu_pmape_base
    #define ASID_TABLE_BASE         mmu_asid_table_base
    #define MMU_CSR                 (*mmu_csr)
    #define MMU_POWER_REG           (*mmu_power_reg)
    #define MMU_STATUS_REG          (*mmu_status_reg)
    #define MMU_MCR_M68010          (*mmu_mcr_m68010)
    #define MMU_MCR_MASK            (*mmu_mcr_mask)
    #define MMU_MCR_M68020          (*mmu_mcr_m68020)
    #define DN330_MMU_HARDWARE_REV  (*mmu_hw_rev)

    #define M68020                  mmu_m68020
    #define MMU_$PID_PRIV           mmu_pid_priv
    #define VA_TO_PTT_OFFSET_MASK   mmu_va_to_ptt_mask
    #define MMU_SYSREV              mmu_sysrev
    #define PROC1_$AS_ID            mmu_current_asid
    #define MCR_SHADOW              mmu_mcr_shadow
#endif

/* Get PTT entry for a virtual address */
#define PTT_FOR_VA(va)          ((uint16_t*)((uint32_t)PTT_BASE + ((va) & VA_TO_PTT_OFFSET_MASK)))

/* Get PMAPE entry for a physical page number */
#define PMAPE_FOR_PPN(ppn)      ((uint32_t*)((char*)MMU_PMAPE_BASE + ((ppn) << 2)))

/* Get ASID entry for a physical page number */
#define ASID_FOR_PPN(ppn)       (ASID_TABLE_BASE[(ppn)])

/* CSR (Control/Status Register) bit definitions */
#define CSR_PID_MASK            0xFF00  /* Address space ID */
#define CSR_PRIV_BIT            0x0001  /* Privilege mode */
#define CSR_PTT_ACCESS_BIT      0x0002  /* Enable PTT access */

/*
 * Status Register (SR) access macros
 * These are used to disable/restore interrupts during critical sections.
 */
#if defined(M68K)
    /* M68K inline assembly for SR manipulation */
    static inline uint16_t GET_SR(void) {
        uint16_t sr;
        __asm__ volatile("move.w %%sr, %0" : "=d"(sr));
        return sr;
    }
    static inline void SET_SR(uint16_t sr) {
        __asm__ volatile("move.w %0, %%sr" : : "d"(sr));
    }
#else
    /* Non-m68k platforms: provide stubs or platform-specific implementations */
    extern uint16_t platform_get_sr(void);
    extern void platform_set_sr(uint16_t sr);
    #define GET_SR()        platform_get_sr()
    #define SET_SR(sr)      platform_set_sr(sr)
#endif

/* Interrupt priority level mask */
#define SR_IPL_MASK             0x0700  /* Interrupt priority level bits */
#define SR_IPL_DISABLE_ALL      0x0700  /* Disable all interrupts */

/*
 * External references
 */
extern uint32_t MMAP_$LPPN;             /* Lowest pageable page number */
extern uint32_t MMAP_$HPPN;             /* Highest pageable page number */

/*
 * System functions
 */
extern void CRASH_SYSTEM(const char *msg);
extern void CACHE_$CLEAR(void);

/*
 * Internal helper functions (static in implementation)
 */

/* Remove a PPN from the hash chain (called with interrupts disabled) */
/* void mmu_$remove_internal(uint16_t ppn); */

/* Remove PMAPE entry and update hash chain */
/* void mmu_$remove_pmape(uint16_t ppn); */

/* Unlink entry from hash chain */
/* void mmu_$unlink_from_hash(uint16_t ppn, uint16_t prev_offset); */

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

/* Install a mapping (private, no global bit) */
void MMU_$INSTALL_PRIVATE(uint32_t ppn, uint32_t va, uint8_t asid, uint8_t prot);

/* Install mappings for a list of physical pages */
void MMU_$INSTALL_LIST(uint16_t count, uint32_t *ppn_array, uint32_t va,
                       uint8_t asid, uint8_t prot);

/* Install a mapping with global bit */
void MMU_$INSTALL(uint32_t ppn, uint32_t va, uint8_t asid, uint8_t prot);

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

/* Mark virtual address as cache-inhibited (stub) */
void MMU_$CACHE_INHIBIT_VA(void);

/* Toggle MCR (Memory Control Register) bits */
void MMU_$MCR_CHANGE(uint16_t bit);

/* Translate VA to PA, crash if translation fails */
uint32_t mmu_$vtop_or_crash(uint32_t va);

#endif /* MMU_H */
