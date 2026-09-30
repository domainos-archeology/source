/*
 * io_data.c - IO Module Global Data Definitions
 *
 * This file defines the global variables used by the IO subsystem.
 *
 * Original M68K addresses:
 *   IO_$SAVED_OS_SP:    0x00E2E822 (4 bytes, void pointer)
 *   IO_$SAVED_INT_SR:   0x00EB2BF8 (2 bytes, uint16) - OS_$STACK field
 */

#include "io/io_internal.h"
#include "route/route.h"      /* ROUTE_$PORT_ARRAY: the DCTES area marker */
#include "sysbus/sysbus.h"    /* SYSBUS_$INIT, SYSBUS_$DEFINE_INT */

/*
 * ============================================================================
 * Interrupt Stack State
 * ============================================================================
 */

/*
 * IO_$SAVED_OS_SP - Saved OS stack pointer during interrupt processing
 *
 * When an interrupt switches to the dedicated interrupt stack, the
 * previous (OS) stack pointer is saved here. A non-zero value indicates
 * we are currently on the interrupt stack. Cleared when switching back
 * to the OS stack.
 *
 * Original address: 0x00E2E822
 */
void *IO_$SAVED_OS_SP = NULL;

/*
 * IO_$SAVED_INT_SR (0x00EB2BF8) is a field of the STACK segment block,
 * OS_$STACK (os/os.h, os/os_data.c; source-4k71), not an object of its own.
 */


/*
 * ============================================================================
 * Device controller tables
 * ============================================================================
 */

/*
 * IO_$DCTE_LIST - head of the singly linked list of device controller table
 * entries, 0x00E2C8B4 (map: symbol IO_$DCTE_LIST; the next map symbol,
 * IO_$IN_INIT, is at 0x00E2C8BA).  SYSBUS_$INIT walks it with
 * `movea.l (0x00e2c8b4).l,A0` / `movea.l (A0),A0`, so the cell is one 32-bit
 * pointer; the image value is 0 (the list is built at boot).
 */
dcte_t *IO_$DCTE_LIST = NULL;

/*
 * IO_$INT_CTRL - per-controller-type interrupt dispatch block, 0x00E22904.
 *
 * The map segment is "D E22904 ATBUS_ size = 2C" and it exports no interior
 * symbol, so the whole 0x2C bytes are this one module-local block; that is
 * exactly sizeof(io_int_ctrl_t).  DISK_$INTERRUPT runs with A5 pointing at it.
 * The image is all zeroes; SYSBUS_$INIT fills the slots in.
 */
io_int_ctrl_t IO_$INT_CTRL = { 0 };
#if defined(ARCH_M68K)
_Static_assert(sizeof(IO_$INT_CTRL) == 0x2C,
               "IO_$INT_CTRL: map segment ATBUS_ 0x00E22904 size = 2C");
#endif


/*
 * IO_$BUS_EPV (0xE2C3C8, IO_ +0x60): five bus entry-point vectors of 0x24
 * bytes.  Image (`gsk read 0xE2C3C8 0xB4`): entry 1 has word 0x0400 at
 * +0x20 (0xE2C40C), entries 2 and 3 word 0x0008 at +0x20 (0xE2C430,
 * 0xE2C454), entry 4 SYSBUS_$INIT (0x00E0AB28) at +0x00, SYSBUS_$DEFINE_INT
 * (0x00E0ABBC) at +0x0C and word 0x0008 at +0x20 (0xE2C478); everything
 * else zero.
 */
io_$bus_epv_t IO_$BUS_EPV[IO_BUS_EPV_COUNT] = {
    [1] = { .words_10 = { [8] = 0x0400 } },
    [2] = { .words_10 = { [8] = 0x0008 } },
    [3] = { .words_10 = { [8] = 0x0008 } },
    [4] = { .init = SYSBUS_$INIT, .define_int = SYSBUS_$DEFINE_INT,
            .words_10 = { [8] = 0x0008 } },
};

/* 0xE2C880 (IO_ +0x518) and IO_$WIRING_EXCLUSION 0xE2C898 (+0x530); zero in
 * the image, ML_$EXCLUSION_INIT'd by IO_$INIT. */
ml_$exclusion_t io_$exclusion;
ml_$exclusion_t IO_$WIRING_EXCLUSION;

/*
 * 0xE2C8AC / 0xE2C8B0: both hold 0x00E2E0A0, the (empty, size 0) linker DCTES
 * area, which the SAU2 map places at the start of NET_PORT_TABLE
 * (ROUTE_$PORT_ARRAY).  What matters is that end == start: there are no
 * dynamic DCTEs.
 */
uint32_t io_$dcte_area_end = ARCH_PTR_TO_VA_STATIC(&ROUTE_$PORT_ARRAY, 0x00E2E0A0);
uint32_t io_$dcte_area_start = ARCH_PTR_TO_VA_STATIC(&ROUTE_$PORT_ARRAY, 0x00E2E0A0);

/* 0xE2C8B8 (IO_ +0x550, no map symbol): read only by IO_$GET_CONFIG;
 * zero in the image (`gsk read 0xE2C8B8 2` = 00 00). */
int8_t io_$config_flag;

/* IO_$IN_INIT (0xE2C8BA): zero in the image. */
int8_t IO_$IN_INIT;
