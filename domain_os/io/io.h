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

/*
 * ============================================================================
 * Device Controller Table Entry (DCTE) Structure
 * ============================================================================
 *
 * The DCTE structure contains information about each disk/device controller.
 * Size: 72 bytes (0x48)
 */
typedef struct dcte_t {
    uint32_t    no_clue;        /* 0x00: Unknown field */
    uint16_t    ctype;          /* 0x04: Controller type (0, 1, or 2) */
    uint16_t    cnum;           /* 0x06: Controller number */
    struct dcte_t *nextp;       /* 0x08: Next DCTE in list */
    uint32_t    csrsytr;        /* 0x0C: Unknown */
    status_$t   cstatus;        /* 0x10: Controller status */
    uint32_t    blk_hdr_ptr;    /* 0x14: Block header pointer */
    uint32_t    blk_hdr_pa;     /* 0x18: Block header physical address */
    uint8_t     reserved_1c[12];/* 0x1C-0x27: Reserved */
    uint32_t    vector_ptr;     /* 0x28: Vector pointer */
    uint32_t    int_entry;      /* 0x2C: Interrupt entry */
    uint32_t    int_routine;    /* 0x30: Interrupt routine */
    uint32_t    disk_dinit;     /* 0x34: Disk initialization structure */
    uint32_t    disk_do_io;     /* 0x38: Disk I/O function pointer */
    uint32_t    disk_error_que; /* 0x3C: Disk error queue */
    uint16_t    dflags;         /* 0x40: Device flags */
    uint16_t    d_unit_irq;     /* 0x42: Device unit IRQ */
    uint32_t    pdvte_index;    /* 0x44: PDVTE index */
} dcte_t;

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
    void        (*type2_do_io)(dcte_t *dcte);   /* 0x00 */
    dcte_t      *type2_dcte;                     /* 0x04 */
    void        (*type2_dinit)(dcte_t *dcte);   /* 0x08 */
    uint32_t    reserved_0c;                     /* 0x0C */

    /* Controller type 1 */
    void        (*type1_do_io)(dcte_t *dcte);   /* 0x10 */
    dcte_t      *type1_dcte;                     /* 0x14 */
    void        (*type1_dinit)(dcte_t *dcte);   /* 0x18 */
    uint32_t    reserved_1c;                     /* 0x1C */

    /* Controller type 0 */
    void        (*type0_do_io)(dcte_t *dcte);   /* 0x20 */
    dcte_t      *type0_dcte;                     /* 0x24 */
    void        (*type0_dinit)(dcte_t *dcte);   /* 0x28 */
} io_int_ctrl_t;

/*
 * ============================================================================
 * M68K Interrupt Vector Numbers
 * ============================================================================
 */
#define IO_VECTOR_RING      0x1b    /* Ring network interrupt */
#define IO_VECTOR_DISK      0x1d    /* Disk interrupt */

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
void IO_$TRAP(int16_t m68k_vector_num, void *handler_addr);

#endif /* IO_H */
