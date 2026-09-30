/*
 * IO Internal - I/O Subsystem Internal Definitions
 *
 * This header contains internal definitions used within the IO subsystem.
 * External code should use io/io.h instead.
 */

#ifndef IO_INTERNAL_H
#define IO_INTERNAL_H

#include "io/io.h"

/*
 * ============================================================================
 * Architecture-Specific Constants
 * ============================================================================
 */

/*
 * The interrupt stack (top 0x00EB2BE8, in the SAU2 map's STACK segment) is
 * the block OS_$STACK (os/os.h; source-4k71).  It is reached only by the
 * hand-written io/sau2/use_int_stack.s and proc1/sau2/int_handler.s, as
 * `OS_$STACK + 0x2BE8`.  source-702z removed an unused C spelling of it and
 * a host-only stand-in buffer.
 */


#include "ml/ml.h"

/*
 * ============================================================================
 * IO_ data segment cells touched by IO_$INIT (map "D E2C368 IO_ size = 554";
 * the module is not a MODULE_DATA block yet, so like IO_$DCTE_LIST these are
 * individual objects, each with its address and IO_ offset).
 * ============================================================================
 */

/* A bus's entry-point vector: IO_$INIT calls every non-nil `init'
 * (0x00E3292C-0x00E3293A, `lea (0x24,A3),A3`, five entries). */
typedef void (*io_$proc_t)(void);

typedef struct io_$bus_epv_t {
    io_$proc_t init;            /* +0x00 */
    uint32_t   _04[2];          /* +0x04 */
    io_$proc_t define_int;      /* +0x0C */
    uint16_t   words_10[10];    /* +0x10 */
} io_$bus_epv_t;

#define IO_BUS_EPV_COUNT    5   /* moveq #0x4,D2 / dbf */
#if defined(ARCH_M68K)
/* Pointer-bearing: target-only (design section 3). */
_Static_assert(sizeof(io_$bus_epv_t) == 0x24, "IO_$BUS_EPV stride 0x24");
_Static_assert(__builtin_offsetof(io_$bus_epv_t, define_int) == 0x0C, "define_int");
#endif

/* IO_$BUS_EPV, 0xE2C3C8 (IO_ +0x60) */
extern io_$bus_epv_t IO_$BUS_EPV[IO_BUS_EPV_COUNT];

/* 0xE2C880 (IO_ +0x518, no map symbol) and IO_$WIRING_EXCLUSION 0xE2C898
 * (+0x530): the two exclusion locks IO_$INIT initialises. */
extern ml_$exclusion_t io_$exclusion;
extern ml_$exclusion_t IO_$WIRING_EXCLUSION;

/* 0xE2C8AC (+0x544) and 0xE2C8B0 (+0x548), no map symbols: the end and the
 * start of the linker's DCTES area (map: DCTE_START = DCTE_END = 0xE2E0A0,
 * size 0 on the SAU2), walked by io_$build_dcte_list. */
extern uint32_t io_$dcte_area_end;
extern uint32_t io_$dcte_area_start;

/* IO_$IN_INIT, 0xE2C8BA (+0x552): true while IO_$INIT runs. */
extern int8_t IO_$IN_INIT;

/*
 * DCTES - the DCTES data segment (map "D E2C8BC DCTES size = D0"), defined
 * in io/dctes_data.c with the image contents.  Four objects, each a map
 * symbol:
 *
 *   +0x00  RING_DCTE  0xE2C8BC  the token ring controller's DCTE
 *   +0x40  FLP_DCTE   0xE2C8FC  the floppy controller's DCTE
 *   +0x80  WIN_DCTE   0xE2C93C  the Winchester controller's DCTE
 *   +0xC0  DEV_DCTES  0xE2C97C  the nil-terminated list of the three, as
 *                               32-bit VAs: WIN_DCTE, FLP_DCTE, RING_DCTE, 0
 *
 * io_$build_dcte_list (0x00E328A4-0x00E328CA) walks DEV_DCTES and appends
 * each DCTE to IO_$DCTE_LIST.  The records hold native pointers (nextp,
 * csrsytr), so the layout asserts are target-only.
 */
#define IO_DCTES_SIZE       0xD0
#define IO_DEV_DCTES_COUNT  4

typedef struct io_$dctes_t {
    dcte_t   ring_dcte;                         /* +0x00 RING_DCTE */
    dcte_t   flp_dcte;                          /* +0x40 FLP_DCTE */
    dcte_t   win_dcte;                          /* +0x80 WIN_DCTE */
    uint32_t dev_dctes[IO_DEV_DCTES_COUNT];     /* +0xC0 DEV_DCTES (VAs) */
} io_$dctes_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(io_$dctes_t, flp_dcte) == 0x40, "FLP_DCTE (0xE2C8FC)");
_Static_assert(offsetof(io_$dctes_t, win_dcte) == 0x80, "WIN_DCTE (0xE2C93C)");
_Static_assert(offsetof(io_$dctes_t, dev_dctes) == 0xC0, "DEV_DCTES (0xE2C97C)");
_Static_assert(sizeof(io_$dctes_t) == IO_DCTES_SIZE, "DCTES: map size D0");
#endif

MODULE_DATA_DECLARE(io_$dctes_t, DCTES, 0x00E2C8BC);

/* The map's names for the four objects */
#define RING_DCTE   (DCTES.ring_dcte)
#define FLP_DCTE    (DCTES.flp_dcte)
#define WIN_DCTE    (DCTES.win_dcte)
#define DEV_DCTES   (DCTES.dev_dctes)

/*
 * io_$build_dcte_list (0x00E32834, 170 bytes, was FUN_00e32834; the IO_ code
 * segment's first routine, no map symbol) - build IO_$DCTE_LIST from the
 * dynamic DCTE area and DEV_DCTES.  Called only by IO_$INIT (0x00E32920).
 * (io/build_dcte_list.c)
 */
void io_$build_dcte_list(void);

#endif /* IO_INTERNAL_H */
