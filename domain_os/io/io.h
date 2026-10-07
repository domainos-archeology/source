/*
 * io/io.h - I/O Subsystem Public API
 *
 * The IO subsystem provides low-level I/O services including
 * interrupt vector management and device controller table entries.
 *
 * Original addresses: 0x00e2e800 (IO_$TRAP), 0x00e2c8b4 (IO_$DCTE_LIST)
 */

#ifndef IO_H
#define IO_H

#include "base/base.h"
#include "os/os.h"      /* OS_$STACK: IO_$SAVED_INT_SR is one of its fields */

/*
 * Status codes (module 0x10 = OS / I/O manager)
 *
 * Single definition; flp/, ring/, scsi/ and win/ include this header.
 */
#define status_$io_controller_not_found     0x00100001  /* "controller not found" */
#define status_$io_controller_not_in_system 0x00100002
#define status_$io_bad_dcte_length          0x00100007  /* io_$build_dcte_list */

/*
 * ============================================================================
 * Device Controller Table Entry (DCTE) Structure
 * ============================================================================
 *
 * The DCTE structure contains information about each disk/device controller.
 * Size: 0x40 bytes, the length the image's static DCTEs (RING_DCTE, FLP_DCTE,
 * WIN_DCTE in the DCTES segment, io/dctes_data.c) are apart and give in
 * their length word.  (Three words at +0x40..+0x47 that no code reads were
 * dropped when the DCTES segment was defined: they would overlay the next
 * static DCTE.)
 *
 * What IO_$INIT (0x00E328E0) and io_$build_dcte_list (0x00E32834) read:
 *   +0x00 kind    `cmpi.w #0x1,(A3)`: kind 1 has a VA at +0x2C to translate
 *   +0x02 length  record length; 0 or not a multiple of 8 is "bad dcte
 *                 length" (0x00E32852-0x00E3286A)
 *   +0x0C csrsytr the controller's init procedure, called with the DCTE,
 *                 status in D0 (`jsr (A0)` 0x00E329D2)
 *   +0x10 cstatus
 *   +0x14 name    eight characters printed "%m8a"; a zero first byte means
 *                 the DCTE is not listed (`tst.b (0x14,A3)`)
 *   +0x2C io_va / +0x30 io_pa  (0x00E3297C-0x00E329B8)
 */
/* dcte_t.kind 1: IO_$INIT translates io_va (`cmpi.w #0x1,(A3)` 0x00E32976) */
#define IO_DCTE_KIND_MAPPED     1

struct dcte_t;
typedef status_$t (*io_$dcte_init_fn_t)(struct dcte_t *dcte);

typedef struct dcte_t {
  uint16_t kind;           /* 0x00: 1 = io_va must be translated */
  uint16_t length;         /* 0x02: record length (multiple of 8) */
  uint16_t ctype;          /* 0x04: Controller type (0, 1, or 2) */
  uint16_t cnum;           /* 0x06: Controller number */
  struct dcte_t *nextp;    /* 0x08: Next DCTE in list */
  io_$dcte_init_fn_t csrsytr; /* 0x0C: controller init procedure */
  status_$t cstatus;       /* 0x10: Controller status */
  uint8_t name[8];         /* 0x14: device name ("%m8a") */
  uint8_t reserved_1c[12]; /* 0x1C-0x27: Reserved */
  uint32_t vector_ptr;     /* 0x28: Vector pointer */
  uint32_t io_va;          /* 0x2C: VA translated by IO_$INIT (kind 1) */
  uint32_t io_pa;          /* 0x30: its PA: ppn << 10 | (io_va & 0x3FF) */
  uint32_t disk_dinit;     /* 0x34: Disk initialization structure */
  uint32_t disk_do_io;     /* 0x38: Disk I/O function pointer */
  uint32_t disk_error_que; /* 0x3C: Disk error queue; FLP_DO_IO takes the
                            *       high word as its ML lock and WIN_$CINIT
                            *       reads it too (0x0017 in the static
                            *       FLP/WIN DCTEs, 0x0018 in RING_DCTE) */
} dcte_t;

/* Layout recovered from the disassembly -- see the field comments above. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(dcte_t, kind) == 0x00, "dcte_t.kind");
_Static_assert(__builtin_offsetof(dcte_t, length) == 0x02, "dcte_t.length");
_Static_assert(__builtin_offsetof(dcte_t, ctype) == 0x04, "dcte_t.ctype");
_Static_assert(__builtin_offsetof(dcte_t, cnum) == 0x06, "dcte_t.cnum");
_Static_assert(__builtin_offsetof(dcte_t, nextp) == 0x08, "dcte_t.nextp");
_Static_assert(__builtin_offsetof(dcte_t, csrsytr) == 0x0C, "dcte_t.csrsytr");
_Static_assert(__builtin_offsetof(dcte_t, cstatus) == 0x10, "dcte_t.cstatus");
_Static_assert(__builtin_offsetof(dcte_t, name) == 0x14, "dcte_t.name");
_Static_assert(__builtin_offsetof(dcte_t, reserved_1c) == 0x1C, "dcte_t.reserved_1c");
_Static_assert(__builtin_offsetof(dcte_t, vector_ptr) == 0x28, "dcte_t.vector_ptr");
_Static_assert(__builtin_offsetof(dcte_t, io_va) == 0x2C, "dcte_t.io_va");
_Static_assert(__builtin_offsetof(dcte_t, io_pa) == 0x30, "dcte_t.io_pa");
_Static_assert(__builtin_offsetof(dcte_t, disk_dinit) == 0x34, "dcte_t.disk_dinit");
_Static_assert(__builtin_offsetof(dcte_t, disk_do_io) == 0x38, "dcte_t.disk_do_io");
_Static_assert(__builtin_offsetof(dcte_t, disk_error_que) == 0x3C, "dcte_t.disk_error_que");
_Static_assert(sizeof(dcte_t) == 0x40, "dcte_t size: the static DCTEs' length word");
#endif

/*
 * ============================================================================
 * Interrupt Controller Data Structure
 * ============================================================================
 *
 * Located at 0xe22904, this structure holds function pointers and DCTE
 * references for each controller type.
 *
 * Layout (indexed from base 0xe22904):
 *   +0x00: Type 2 - do_io function pointer
 *   +0x04: Type 2 - DCTE pointer
 *   +0x08: Type 2 - dinit function pointer
 *   +0x10: Type 1 - do_io function pointer
 *   +0x14: Type 1 - DCTE pointer
 *   +0x18: Type 1 - dinit function pointer
 *   +0x20: Type 0 - do_io function pointer
 *   +0x24: Type 0 - DCTE pointer
 *   +0x28: Type 0 - dinit function pointer
 */
typedef struct io_int_ctrl_t {
  /* Controller type 2 */
  void (*type2_do_io)(dcte_t *dcte); /* 0x00 */
  dcte_t *type2_dcte;                /* 0x04 */
  void (*type2_dinit)(dcte_t *dcte); /* 0x08 */
  uint32_t reserved_0c;              /* 0x0C */

  /* Controller type 1 */
  void (*type1_do_io)(dcte_t *dcte); /* 0x10 */
  dcte_t *type1_dcte;                /* 0x14 */
  void (*type1_dinit)(dcte_t *dcte); /* 0x18 */
  uint32_t reserved_1c;              /* 0x1C */

  /* Controller type 0 */
  void (*type0_do_io)(dcte_t *dcte); /* 0x20 */
  dcte_t *type0_dcte;                /* 0x24 */
  void (*type0_dinit)(dcte_t *dcte); /* 0x28 */
} io_int_ctrl_t;

/* Layout recovered from the disassembly -- see the field comments above. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(io_int_ctrl_t, type2_dcte) == 0x04, "io_int_ctrl_t.type2_dcte");
_Static_assert(__builtin_offsetof(io_int_ctrl_t, reserved_0c) == 0x0C, "io_int_ctrl_t.reserved_0c");
_Static_assert(__builtin_offsetof(io_int_ctrl_t, type1_dcte) == 0x14, "io_int_ctrl_t.type1_dcte");
_Static_assert(__builtin_offsetof(io_int_ctrl_t, reserved_1c) == 0x1C, "io_int_ctrl_t.reserved_1c");
_Static_assert(__builtin_offsetof(io_int_ctrl_t, type0_dcte) == 0x24, "io_int_ctrl_t.type0_dcte");
#endif

/*
 * ============================================================================
 * M68K Interrupt Vector Numbers
 * ============================================================================
 */
#define IO_VECTOR_RING 0x1b /* Ring network interrupt */
#define IO_VECTOR_DISK 0x1d /* Disk interrupt */

/*
 * ============================================================================
 * Global Data
 * ============================================================================
 */

/*
 * IO_$DCTE_LIST - Head of device controller table entry list
 *
 * Points to the first DCTE in a linked list of all device controllers.
 *
 * Original address: 0x00e2c8b4
 */
extern dcte_t *IO_$DCTE_LIST;

/*
 * IO_$INT_CTRL - Interrupt controller data structure
 *
 * Contains function pointers and DCTE references organized by controller type.
 *
 * Original address: 0x00e22904
 */
extern io_int_ctrl_t IO_$INT_CTRL;

/*
 * IO_$FLIH_TAB - First-Level Interrupt Handler Table
 *
 * Table of function pointers indexed by interrupt vector number.
 * Used by IO_$TRAP to register interrupt handlers.
 *
 * Original address: 0x00e2e876 (relative to IO_$TRAP + 0x76)
 */
extern void *IO_$FLIH_TAB[];

/*
 * IO_$SAVED_OS_SP - Saved OS stack pointer during interrupt processing
 *
 * When IO_$USE_INT_STACK switches to the interrupt stack, the previous
 * (OS) stack pointer is saved here. Non-zero means we are currently on
 * the interrupt stack. Cleared when switching back to the OS stack.
 *
 * Original address: 0x00E2E822
 */
extern void *IO_$SAVED_OS_SP;

/*
 * IO_$SAVED_INT_SR - Saved status register from interrupted context
 *
 * Holds the SR from the exception frame at the time of the interrupt
 * stack switch, used during interrupt exit to determine the interrupted
 * priority level.
 *
 * Original address: 0x00EB2BF8, 8 bytes below INT_STACK_BASE in the SAU2
 * map's STACK segment, so it is a field of that block, OS_$STACK (os/os.h;
 * source-4k71).  io/sau2/use_int_stack.s reaches it as
 * `.set IO_$SAVED_INT_SR, OS_$STACK + 0x2BF8`.
 */
#define IO_$SAVED_INT_SR (OS_$STACK.saved_int_sr)


/*
 * ============================================================================
 * Function Prototypes
 * ============================================================================
 */

/*
 * IO_$TRAP - Install an interrupt handler
 *
 * Installs a handler function for the specified M68K interrupt vector.
 * The handler address is stored in IO_$FLIH_TAB and the vector table
 * (at address 0) is updated to point to dispatch_vector_irq.
 *
 * Parameters:
 *   m68k_vector_num - M68K interrupt vector number (e.g., 0x1b, 0x1d)
 *   handler_addr - Address of the handler function
 *
 * Original address: 0x00e2e800
 */
void IO_$TRAP(uint32_t vector_handler_slot, uint32_t handler_pad_slot);
/*
 * Pascal frame (io/sau2/trap.s 0xE2E800 `move.w (4,sp),d0;
 * movea.l (6,sp),a0'): (4) m68k_vector_num.w, (6) handler_addr.l
 * UNPADDED; the image's callers (SYSBUS_$INIT, call 0xE0AB40) push
 * `subq.l #2,sp; move.l #handler; move.w #vector'.  The handler straddles
 * two gcc slots (arch/arch.h "Pascal parameter slots", source-nxtd):
 *   slot 1 = vector | handler.hi,  slot 2 = handler.lo | (pad)
 * io_$trap_slots packs them; the macro evaluates each argument once.
 */
static inline void io_$trap_slots(uint16_t vector, uint32_t handler_va)
{
    (IO_$TRAP)(ARCH_PASCAL_WORD_PAIR_SLOT(vector, handler_va >> 16),
               ARCH_PASCAL_WORD_SLOT(handler_va));
}
#define IO_$TRAP(m68k_vector_num, handler_addr) \
    io_$trap_slots((m68k_vector_num), ARCH_PTR_TO_VA(handler_addr))

/*
 * IO_$USE_INT_STACK - Switch from OS stack to interrupt stack
 *
 * Saves the current OS stack pointer and switches to the dedicated
 * interrupt stack. If already on the interrupt stack (IO_$SAVED_OS_SP
 * is non-zero), the switch is skipped.
 *
 * This is an assembly-only routine (cannot be expressed in C because
 * it directly manipulates the stack pointer). It does not use RTS to
 * return; instead it pops the return address into A1 and uses JMP (A1).
 *
 * Original address: 0x00e2e826
 */
void IO_$USE_INT_STACK(void);

/*
 * IO_$GET_DCTE - Find the DCTE for a controller type/number
 *
 * Walks IO_$DCTE_LIST looking for an entry whose ctype and cnum match
 * *ctypep / *cnump.  On a match *status_ret receives the controller's
 * cstatus and the DCTE pointer is returned (in A0); if no entry matches,
 * *status_ret is set to 0x00100001 and NULL is returned.
 *
 *   00e1a462    move.w (A0),D0w          ; *ctypep
 *   00e1a464    move.w (A2),D1w          ; *cnump
 *   00e1a480    move.l (0x10,A0),(A1)    ; *status_ret = dcte->cstatus
 *   00e1a48c    movea.l D2,A0            ; return dcte
 *
 * Original address: 0x00E1A448
 */
dcte_t *IO_$GET_DCTE(uint16_t *ctypep, uint16_t *cnump, status_$t *status_ret);

/*
 * IO_$INIT - Initialize the I/O subsystem
 *
 * Initializes the I/O exclusion locks and DMA, builds IO_$DCTE_LIST, runs
 * the bus init procedures of IO_$BUS_EPV, then walks the list translating
 * each kind-1 DCTE's VA and calling its init procedure.  When *verbose_flag
 * is negative, prints a line per named device.  (io/init.c)
 *
 * Parameters (all passed by address, pea'd by OS_$INIT):
 *   param1       - never read (OS_$INIT passes its status_$ok cell)
 *   verbose_flag - Domain boolean byte; negative => print device init status
 *   status_ret   - set to status_$ok on entry and never changed
 *
 * Original address: 0x00E328E0
 */
void IO_$INIT(void *param1, const int8_t *verbose_flag, status_$t *status_ret);

/*
 * IO_$GET_CONFIG - report which optional controllers are present
 *
 * Clears all four words and then sets bits in the LOW byte of each
 * (i.e. bits 0..7 of the word on m68k) according to what CHK (0x00E1A404,
 * misc/chk.c) reports for a controller type:
 *
 *   config1 bit 0 / bit 1   CHK(&0) / CHK(&1)      (0x00E72354 / 0x00E7236E)
 *   config2 bit 0           CHK(&2)                (0x00E7238E)
 *   config3                 cleared, never set     (0x00E72398)
 *   config4 bit 0           io_$config_flag, 0xE2C8B8 (0x00E723AC)
 *
 * ASKNODE_$INTERNET_INFO's request-0x27 arm passes reply+0x22, +0x24, +0x26
 * and +0x28 (0x00E64D3C-0x00E64D48).
 *
 * Original address: 0x00E72334 (134 bytes)
 */
void IO_$GET_CONFIG(uint16_t *config1, uint16_t *config2,
                    uint16_t *config3, uint16_t *config4);

/*
 * io_$probe - Hardware probe helper
 *
 * Probes for a controller by writing a signature and reading it back.
 * Returns a Domain boolean (0xFF = found).  Declared here because flp/,
 * prom/, peb/, win/ and smd/ all call it (bead source-3uo).
 */
int8_t io_$probe(void *type, void *addr, void *result);

#endif /* IO_H */
