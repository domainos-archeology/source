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
 * - MMU_$PTTX at 0xEC2800 - the PTT index of every physical page (formerly
 *   called the ASID table)
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
 * MMU_$PTTX - the PTT index of every physical page (map MMU_$PTTX 0xEC2800
 * in "D EB4800 OS_PMAPS size = 10000", up to AUDIT_LIST's OS_PAGE_END
 * 0xEC4800: 0x2000 bytes).
 * Module data block MMU_$PTTX: Claude Opus 5.5 (source-o56c).
 *
 * One word per physical page, indexed by the ppn itself from 0 (no Pascal
 * bias): mmu_$installi stores the LOW word of its packed argument there
 * (`lea (0xec2800).l,A0 / adda.w D2w,A0 / adda.w D2w,A0 / move.w D4w,(A0)',
 * 0x00E240B6-0x00E240C0) - the page's virtual-address bits that select its
 * PTT slot, (va & VA_TO_PTT_OFFSET_MASK) >> 6 after MMU_$INSTALL's
 * shift-and-rotate packing (0x00E24054-0x00E24074); the high word, ASID
 * and protection, goes into the PFT entry instead.  mmu_$remove_pmape
 * turns an entry back into that slot as
 * PTT_BASE + (entry << 6) (`clr.l D0 / move.w (A2),D0w / lsl.l #6,D0',
 * 0x00E23DD8-0x00E23DF0) and MMU_$PTOV into the page's VA ((PFT bits
 * 16..19 | entry) << 6 on a 68020, 0x00E241CE-0x00E241E6).  The stride is
 * 2 (the doubled adda.w; MMU_$PTOV indexes with ppn*4 >> 1), and ppn runs
 * to 0xFFF (the PFT's 12-bit links), so the table is 0x1000 words - exactly
 * the rest of OS_PMAPS after the MMAP page table (mmap/mmap.h).  Earlier
 * trees called it the "ASID table"; it holds no ASID.
 *
 * The image carries no bytes for OS_PMAPS (the entries are written as
 * pages are installed), so the block is zero-filled.
 */
#define MMU_PTTX_COUNT  0x1000          /* ppn 0..0xFFF */
#define MMU_$PTTX_SIZE  0x2000          /* MMU_$PTTX 0xEC2800 .. 0xEC4800 */

typedef struct mmu_$pttx_t {
    uint16_t entry[MMU_PTTX_COUNT];     /* entry[ppn] */
} mmu_$pttx_t;

_Static_assert(sizeof(((mmu_$pttx_t *)0)->entry[0]) == 2,
               "MMU_$PTTX stride 2 (adda.w D2w,A0 twice, 0x00E240BC)");
_Static_assert(sizeof(mmu_$pttx_t) == MMU_$PTTX_SIZE,
               "MMU_$PTTX: 0xEC2800..0xEC4800 in OS_PMAPS");

MODULE_DATA_DECLARE(mmu_$pttx_t, MMU_$PTTX, 0x00EC2800);

/*
 * MMU_$GLOBALS - the data run at the head of the MMU_ASM segment.
 * Module data block MMU_$GLOBALS: Claude Opus 5.5 (source-o56c).
 *
 * SAU2 map: "D E23D2C MMU_ASM size = 5B8" opens with MMU_$PID_PRIV
 * (0xE23D2C) and M68020 (0xE23D2E) and its first routine, MMU_$INIT, is at
 * 0xE23D38, so the run is 0xE23D2C..0xE23D37, 0xC bytes - data inside the
 * hand-written code segment, like FIM_$WIRED_DATA inside FIM_WIRED.  The
 * cells between M68020 and MMU_$INIT have no map symbols; MMU_$INIT names
 * them by writing (0x2,A5), (0x6,A5) and (0x8,A5) with A5 = 0xE23D2E
 * (`lea (-0xe,PC),A5' at 0x00E23D3A), and the other MMU_ASM routines read
 * them PC-relative (MMU_$VTOP 0x00E24118 -> 0xE23D30, 0x00E24124 ->
 * 0xE23D34, 0x00E2413A -> 0xE23D2C).  Since the code and the block are
 * separate objects in our link, the mmu/sau2 files reach the cells as
 * `.set NAME, MMU_$GLOBALS + off' aliases (absolute long operands where
 * the image had (d16,PC); tools/asm_compare.py verifies each one).
 *
 * Image contents (`gsk read 0xe23d2c 12`):
 *   00e23d2c  00 00 00 00 00 0f fc 00  00 03 00 08
 * - the 68010 defaults: MMU_$INIT rewrites the mask and both shifts on a
 * 68020.  Pointer-free, so the asserts hold on every build.
 */
typedef struct mmu_$globals_t {
    uint16_t pid_priv;              /* +0x0 0xE23D2C MMU_$PID_PRIV: the CSR
                                     * image - PID in the HIGH byte
                                     * (MMU_$SET_CSR / MMU_$INSTALL_ASID
                                     * store it with move.b), privilege and
                                     * PTT-access bits in the low byte */
    uint16_t m68020;                /* +0x2 0xE23D2E M68020: 68020+ boolean,
                                     * in the HIGH byte (see M68020_IS_*) */
    uint32_t va_to_ptt_offset_mask; /* +0x4 0xE23D30 VA_TO_PTT_OFFSET_MASK:
                                     * 0x0FFC00 (68010), 0x3FFC00 (68020) */
    uint16_t va_shift;              /* +0x8 0xE23D34 MMU_$VA_SHIFT: 3 / 1 */
    uint16_t ptt_shift;             /* +0xA 0xE23D36 MMU_$PTT_SHIFT: 8 / 6 */
} mmu_$globals_t;

#define MMU_$GLOBALS_SIZE 0xC           /* 0xE23D2C .. MMU_$INIT 0xE23D38 */

_Static_assert(__builtin_offsetof(mmu_$globals_t, pid_priv) == 0x0,
               "MMU_$PID_PRIV at 0xE23D2C");
_Static_assert(__builtin_offsetof(mmu_$globals_t, m68020) == 0x2,
               "M68020 at 0xE23D2E (A5 of MMU_$INIT)");
_Static_assert(__builtin_offsetof(mmu_$globals_t, va_to_ptt_offset_mask) == 0x4,
               "VA_TO_PTT_OFFSET_MASK at 0xE23D30 = (0x2,A5)");
_Static_assert(__builtin_offsetof(mmu_$globals_t, va_shift) == 0x8,
               "MMU_$VA_SHIFT at 0xE23D34 = (0x6,A5)");
_Static_assert(__builtin_offsetof(mmu_$globals_t, ptt_shift) == 0xA,
               "MMU_$PTT_SHIFT at 0xE23D36 = (0x8,A5)");
_Static_assert(sizeof(mmu_$globals_t) == MMU_$GLOBALS_SIZE,
               "MMU_ASM data run: 0xE23D2C..0xE23D37");

MODULE_DATA_DECLARE(mmu_$globals_t, MMU_$GLOBALS, 0x00E23D2C);

/* The cells under the map's names (and the unnamed ones under the names the
 * mmu/sau2 files give them). */
#define MMU_$PID_PRIV           (MMU_$GLOBALS.pid_priv)
#define M68020                  (MMU_$GLOBALS.m68020)
#define VA_TO_PTT_OFFSET_MASK   (MMU_$GLOBALS.va_to_ptt_offset_mask)
#define MMU_$VA_SHIFT           (MMU_$GLOBALS.va_shift)
#define MMU_$PTT_SHIFT          (MMU_$GLOBALS.ptt_shift)

/*
 * The MMU's hardware: the register page (map MMU 0xFFB400) and the PTT and
 * PFT windows are SAU2 hardware addresses, arch/m68k/sau2/hw.h.  A host
 * test that runs one of the host models below defines the SAU2_ names it
 * needs as its own cells or arrays before including the code.
 */
#define PTT_BASE                SAU2_PTT_BASE   /* 0x700000 */
#define PFT_BASE                SAU2_PFT_BASE   /* 0xFFB800 */

#define MMU_CSR                 (*SAU2_MMU_CSR)         /* 0xFFB400 PID/Priv/Power */
#define MMU_POWER_REG           (*SAU2_MMU_POWER_REG)   /* 0xFFB402 power control */
/*
 * The first (most significant) byte of MMU_POWER_REG on its own, the
 * handbook's FPU Owner Register.  MMU_$INSTALL_ASID stores it with a byte
 * store, not a word read-modify-write: `move.b (0x00e218d5).l,(0x00ffb402).l'
 * (0x00E2421C).
 */
#define MMU_POWER_REG_BYTE      (*SAU2_MMU_FPU_OWNER_REG)
#define MMU_STATUS_REG          (*SAU2_MMU_STATUS_REG)  /* 0xFFB403 status */
#define MMU_MCR_M68010          (*SAU2_MMU_MCR_M68010)  /* 0xFFB405 MCR, 68010 */
#define MMU_MCR_MASK            (*SAU2_MMU_MCR_MASK)    /* 0xFFB407 MCR mask */
#define MMU_MCR_M68020          (*SAU2_MMU_MCR_M68020)  /* 0xFFB408 MCR, 68020 */
#define DN330_MMU_HARDWARE_REV  (*SAU2_MMU_HW_REV)      /* 0xFFB409 HW revision */

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
#define MMU_PARITY_REG (*SAU2_MMU_PARITY_REG)

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
 * MMU_$SYSTEM_REV - map 0xE2426E, the longword just before MMU_$SET_SYSREV
 * inside the MMU_ASM code (image 00 00 00 00).  Defined by the hand-written
 * mmu/sau2/set_sysrev.s, which reaches it with `lea (-0x6,PC),A0' and
 * stores the hardware revision byte into its LOW byte (3,A0) = 0xE24271;
 * a host test that runs the model defines it.
 */
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

/* Get the MMU_$PTTX entry of a physical page number */
#define PTTX_FOR_PPN(ppn) (MMU_$PTTX.entry[(ppn)])

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

/*
 * Word and byte parameters of the hand-assembled MMU_ASM routines
 * (the mmu/sau2 .s files) are Pascal 2-byte stack slots; each prototype below takes
 * the gcc slots that reproduce the Pascal frame and a same-name macro packs
 * the natural arguments (arch/arch.h, "Pascal parameter slots",
 * source-nxtd).  Offsets are from SP at entry (return address at 0).
 * Host models (the mmu .c files) define the parenthesised name and unpack.
 */

/* Remove mappings for a list of physical pages.
 * Pascal frame (0xE23D92, after the 0x1C-byte movem: (0x20)/(0x24,SP)):
 *   (4) ppn_array.l, (8) count.w; callers `subq.l #2,sp; move.w count;
 *   pea array'.  gcc slot 2 = count in its first word. */
void MMU_$REMOVE_LIST(uint32_t *ppn_array, uint32_t count_slot);
#define MMU_$REMOVE_LIST(ppn_array, count) \
    (MMU_$REMOVE_LIST)((ppn_array), ARCH_PASCAL_WORD_SLOT(count))

/* Remove virtual address mappings.
 * Pascal frame (0xE23E38, after the 0x24-byte movem: (0x28..0x34,SP)):
 *   (4) va.l, (8) count.w, (0xA) asid.w, (0xC) ppn_array, (0x10)
 *   removed_count.  gcc slot 2 = count then asid (one pair slot). */
void MMU_$REMOVE_VIRTUAL(uint32_t va, uint32_t count_asid_slot,
                         uint32_t *ppn_array, uint16_t *removed_count);
#define MMU_$REMOVE_VIRTUAL(va, count, asid, ppn_array, removed_count) \
    (MMU_$REMOVE_VIRTUAL)((va), ARCH_PASCAL_WORD_PAIR_SLOT(count, asid), \
                          (ppn_array), (removed_count))

/* Remove all mappings for an address space ID.
 * Pascal frame (0xE23F0C, after the 0x1C-byte movem: (0x20,SP)):
 *   (4) asid.w.  gcc slot 1 = asid in its first word. */
void MMU_$REMOVE_ASID(uint32_t asid_slot);
#define MMU_$REMOVE_ASID(asid) (MMU_$REMOVE_ASID)(ARCH_PASCAL_WORD_SLOT(asid))

/*
 * MMU_$INSTALL / MMU_$INSTALL_PRIVATE - one translation.
 * Pascal frame (0xE24048 / 0xE23F82, after the 0x24-byte movem:
 * (0x28..0x33,SP)):
 *   (4) ppn.l, (8) va.l, (0xC) asid.w, (0xE) prot.w
 * the routine reading the LOW byte of each word (`move.b (0x33,sp)' =
 * prot, `move.b (0x31,sp)' = asid).  The image's callers push either
 * `move.w prot; move.w asid' (e.g. AST_$COPY_AREA, call 0xE03BDC: prot 0x16,
 * asid PROC1_$AS_ID) or the constant pair as one longword (`pea (0x16).w'
 * = asid 0, prot 0x16).  gcc slot 3 = asid then prot (one pair slot).
 */
void MMU_$INSTALL_PRIVATE(uint32_t ppn, uint32_t va, uint32_t asid_prot_slot);
#define MMU_$INSTALL_PRIVATE(ppn, va, asid, prot) \
    (MMU_$INSTALL_PRIVATE)((ppn), (va), ARCH_PASCAL_WORD_PAIR_SLOT(asid, prot))

/*
 * MMU_$INSTALL_LIST - a run of pages.
 * Pascal frame (0xE23FDE, after the 0x28-byte movem: (0x2C..0x39,SP)):
 *   (4) count.w, (6) ppn_array.l, (0xA) va.l, (0xE) asid.w, (0x10) prot.w
 * - the count word is NOT padded (callers: `move.w prot; move.w asid;
 * move.l va; pea array; move.w count', AST_$COPY_AREA, call 0xE03B42), so the
 * two longwords straddle gcc's slots:
 *   slot 1 = count | array.hi    slot 2 = array.lo | va.hi
 *   slot 3 = va.lo | asid        slot 4 = prot | (pad)
 * mmu_$install_list_slots packs them; the macro evaluates each argument
 * once.
 */
void MMU_$INSTALL_LIST(uint32_t count_array_slot, uint32_t array_va_slot,
                       uint32_t va_asid_slot, uint32_t prot_slot);
static inline void mmu_$install_list_slots(uint16_t count, uint32_t array_va,
                                           uint32_t va, uint16_t asid,
                                           uint16_t prot)
{
    (MMU_$INSTALL_LIST)(ARCH_PASCAL_WORD_PAIR_SLOT(count, array_va >> 16),
                        ARCH_PASCAL_WORD_PAIR_SLOT(array_va, va >> 16),
                        ARCH_PASCAL_WORD_PAIR_SLOT(va, asid),
                        ARCH_PASCAL_WORD_SLOT(prot));
}
#define MMU_$INSTALL_LIST(count, ppn_array, va, asid, prot)                  \
    mmu_$install_list_slots((count), ARCH_PTR_TO_VA(ppn_array), (va),        \
                            (asid), (prot))

/* Install a mapping with global bit (frame: see MMU_$INSTALL_PRIVATE) */
void MMU_$INSTALL(uint32_t ppn, uint32_t va, uint32_t asid_prot_slot);
#define MMU_$INSTALL(ppn, va, asid, prot) \
    (MMU_$INSTALL)((ppn), (va), ARCH_PASCAL_WORD_PAIR_SLOT(asid, prot))

/* Translate virtual address to physical page number */
uint32_t MMU_$VTOP(uint32_t va, status_$t *status);

/* Translate physical page number to virtual address */
uint32_t MMU_$PTOV(uint32_t ppn);

/* Set the Control/Status Register (CSR) privilege bits.
 * Pascal frame (0xE241F4 `move.b (5,sp),(a0)'): (4) csr_val.w, of which
 * only the low byte (5) is read.  gcc slot 1 = the word in its first
 * word, so the byte lands at (5,SP). */
void MMU_$SET_CSR(uint32_t csr_val_slot);
#define MMU_$SET_CSR(csr_val) (MMU_$SET_CSR)(ARCH_PASCAL_WORD_SLOT(csr_val))

/* Install an Address Space ID (switch address spaces).
 * Pascal frame (0xE24204 `move.w (4,sp),d1'): (4) asid.w; callers
 * `subq.l #2,sp; move.w asid' (call 0xE14910).  gcc slot 1 = asid first. */
void MMU_$INSTALL_ASID(uint32_t asid_slot);
#define MMU_$INSTALL_ASID(asid) (MMU_$INSTALL_ASID)(ARCH_PASCAL_WORD_SLOT(asid))

/* Set protection bits for a physical page; returns the old word in D0.w.
 * Pascal frame (0xE2422A, after one saved register: (8)/(0xC,SP)):
 *   (4) ppn.l, (8) prot.w; callers `subq.l #2,sp; move.w #prot;
 *   move.l ppn' (call 0xE33BBA).  gcc slot 2 = prot first. */
uint16_t MMU_$SET_PROT(uint32_t ppn, uint32_t prot_slot);
#define MMU_$SET_PROT(ppn, prot) \
    (MMU_$SET_PROT)((ppn), ARCH_PASCAL_WORD_SLOT(prot))

/*
 * Clear the "used/referenced" bit for a physical page.
 * NOT slot-packed: the routine (0xE2425A) reads the WORD at (4,SP), and
 * both image callers (CHKSUM 0xE0A2FC, NETWORK_$GET_CHKSUM's page_chksum
 * 0xE0F366) push the ppn as a LONGWORD (`move.l'), so the image clears the
 * PFT entry indexed by the ppn's HIGH word (0 for every real ppn).  A gcc
 * longword reproduces that exactly; packing would "fix" the image.
 * TODO(source-qhu6): image quirk preserved; see mmu/clr_used.c.
 */
void MMU_$CLR_USED(uint32_t ppn);

/* Set the MMU system revision from hardware */
void MMU_$SET_SYSREV(void);

/* Check if MMU is in normal mode */
int8_t MMU_$NORMAL_MODE(void);

/* Check if power-off mode is active */
int8_t MMU_$POWER_OFF(void);

/*
 * MMU_$INIT_BSR - map 0xE24294: the immediate word of MMU_$POWER_OFF's
 * `eori.w #imm,%d0' (mmu/sau2/power_off.s), where COLD_START stores the
 * power word (0xFFB402) it read at boot (cold/sau2/cold_start.s 0x1016C0).
 * It lives in code; only the hand-written cold start writes it.
 */
extern uint16_t MMU_$INIT_BSR;

/* Mark virtual address as cache-inhibited
 * Original address: 0x00e2429e
 */
void MMU_$CACHE_INHIBIT_VA(uint32_t va);

/* Toggle MCR (Memory Control Register) bits.
 * Pascal frame (0xE242A0 `sub.w (4,sp),d0' / `move.w (4,sp),d0'):
 * (4) bit.w; callers `subq.l #2,sp; move.w #n' (call 0xE3D182).  gcc slot 1 =
 * bit first. */
void MMU_$MCR_CHANGE(uint32_t bit_slot);
#define MMU_$MCR_CHANGE(bit) (MMU_$MCR_CHANGE)(ARCH_PASCAL_WORD_SLOT(bit))

/* Translate VA to PA, crash if translation fails */
uint32_t mmu_$vtop_or_crash(uint32_t va);

/* Zero a physical page */
void ZERO_PAGE(uint32_t ppn);

#endif /* MMU_H */
