/*
 * NETBUF - Internal Header
 *
 * This file contains internal data structures and functions used only
 * within the NETBUF subsystem.
 */

#ifndef NETBUF_INTERNAL_H
#define NETBUF_INTERNAL_H

#include "misc/crash_system.h"
#include "ml/ml.h"
#include "mmap/mmap.h"
#include "mmu/mmu.h"
#include "netbuf/netbuf.h"
#include "proc1/proc1.h"
#include "time/time.h"
#include "wp/wp.h"

/*
 * NETBUF global data structure
 *
 * Located at 0xE245A8 on m68k. Contains the VA slot array, free lists,
 * counters, and synchronization primitives.
 *
 * Size: 0x338 bytes (824 bytes)
 */
typedef struct netbuf_globals_t {
  /* VA slot array - stores physical addresses when slot is in use,
   * or next free index when slot is free */
  uint32_t va_slots[NETBUF_VA_SLOTS]; /* 0x000: 192 * 4 = 768 bytes */

  /*
   * 0x300: the 48-bit delay NETBUF_$GET_HDR and NETBUF_$GET_DAT hand to
   * TIME_$WAIT.  Both push its address directly out of the globals
   * (0x00E0EE18 and 0x00E0EFE2 "pea (0x300,A5)"); it is a clock_t, not a
   * timer queue.  0x306/0x307 are the alignment gap before the spin lock.
   */
  clock_t delay_time; /* 0x300 (long) + 0x304 (word) */
  uint16_t pad_306;   /* 0x306 */

  /* Spin lock for protecting all netbuf data */
  uint32_t spin_lock; /* 0x308: Spin lock */

  /* Allocation statistics */
  uint32_t dat_allocs; /* 0x30C: Data buffer allocations (fallback) */
  uint32_t hdr_allocs; /* 0x310: Header buffer allocations (fallback) */
  uint32_t dat_delays; /* 0x314: Data buffer delay waits */
  uint32_t hdr_delays; /* 0x318: Header buffer delay waits */

  /* Data buffer pool management */
  uint32_t dat_lim; /* 0x31C: Maximum data buffers to cache */
  uint32_t dat_cnt; /* 0x320: Current cached data buffer count */
  uint32_t dat_top; /* 0x324: Data buffer free list head (page number) */

  /* Header buffer pool management */
  uint32_t hdr_top; /* 0x328: Header buffer free list head (VA) */

  /* VA slot free list */
  int32_t va_top; /* 0x32C: VA slot free list head index (-1 = empty) */

  /* VA base address */
  uint32_t va_base; /* 0x330: Base VA for netbuf space (0xD64C00) */

  /* Header buffer allocation count */
  int16_t hdr_alloc; /* 0x334: Total header buffers allocated */

  /*
   * 0x336: two bytes of segment tail with no accessor.
   *
   * The SR10.2 SAU2 link map sizes the whole data segment as
   * "D    E245A8  NETBUF_   size = 338" (E245A8..E248E0) and its last
   * interior symbol is "E248DC  NETBUF_HDR_ALLOC", i.e. the hdr_alloc field
   * above at 0x334.  Every reference to that cell is a word
   * ("cmpi.w #0xb0,(0x334,A5)" at 0x00E0E94E, "move.w (0x334,A5),D4w" at
   * 0x00E0E95C, "add.w D5w,(0x334,A5)" at 0x00E0E99A), so the named data ends
   * at 0x336, and `gsk xrefs to 0x00E248DE` reports no references at all -
   * neither absolute nor A5-biased, even though Ghidra does resolve the
   * A5-biased accesses to 0xE248DC.  The remaining two bytes are the
   * longword round-up of the module's data segment: 0x336 -> 0x338.
   */
  uint8_t pad_336[2]; /* 0x336 */
} netbuf_globals_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(netbuf_globals_t, va_slots) == 0x00, "netbuf_globals_t.va_slots");
_Static_assert(__builtin_offsetof(netbuf_globals_t, pad_306) == 0x306, "netbuf_globals_t.pad_306");
_Static_assert(__builtin_offsetof(netbuf_globals_t, dat_allocs) == 0x30C, "netbuf_globals_t.dat_allocs");
_Static_assert(__builtin_offsetof(netbuf_globals_t, hdr_allocs) == 0x310, "netbuf_globals_t.hdr_allocs");
_Static_assert(__builtin_offsetof(netbuf_globals_t, dat_delays) == 0x314, "netbuf_globals_t.dat_delays");
_Static_assert(__builtin_offsetof(netbuf_globals_t, hdr_delays) == 0x318, "netbuf_globals_t.hdr_delays");
#endif

/*
 * NETBUF_$DATA - the NETBUF_ data segment (SAU2 map "D E245A8 NETBUF_ size =
 * 338") as a MODULE_DATA block; NETBUF_$ADD_PAGES, _GET_HDR, _GET_DAT and
 * the rest establish A5 = 0xE245A8 (0x00E0E930 "lea (0xe245a8).l,A5").
 * source-702z: the target used an absolute-address macro, the host a
 * pointer variable.  NETBUF_GLOBALS keeps the pointer spelling the NETBUF
 * code uses.
 */
MODULE_DATA_DECLARE(netbuf_globals_t, NETBUF_$DATA, 0x00E245A8);
#define NETBUF_GLOBALS (&NETBUF_$DATA)

/*
 * NETBUF_$VA_BASE - the base of the virtual-address window the network
 * buffers are mapped into, an immediate in the image (NETBUF_$INIT stores
 * #0xD64C00 into va_base), not the address of a linked object.
 */
#define NETBUF_$VA_BASE 0x00D64C00u

_Static_assert(offsetof(netbuf_globals_t, delay_time) == 0x300, "netbuf.delay_time");
_Static_assert(offsetof(netbuf_globals_t, spin_lock) == 0x308, "netbuf.spin_lock");
_Static_assert(offsetof(netbuf_globals_t, dat_lim) == 0x31C, "netbuf.dat_lim");
_Static_assert(offsetof(netbuf_globals_t, dat_cnt) == 0x320, "netbuf.dat_cnt");
_Static_assert(offsetof(netbuf_globals_t, dat_top) == 0x324, "netbuf.dat_top");
_Static_assert(offsetof(netbuf_globals_t, hdr_top) == 0x328, "netbuf.hdr_top");
_Static_assert(offsetof(netbuf_globals_t, va_top) == 0x32C, "netbuf.va_top");
_Static_assert(offsetof(netbuf_globals_t, va_base) == 0x330, "netbuf.va_base");
_Static_assert(offsetof(netbuf_globals_t, hdr_alloc) == 0x334, "netbuf.hdr_alloc");
_Static_assert(offsetof(netbuf_globals_t, pad_336) == 0x336, "netbuf.pad_336");
/*
 * Whole-segment size from the SAU2 map ("NETBUF_ size = 338").  Checked on
 * every target: clock_t carries an explicit packed spelling (base/base.h), so
 * delay_time occupies 0x300..0x305 on a 64-bit host too and nothing after it
 * shifts.  (source-no75)
 */
_Static_assert(sizeof(netbuf_globals_t) == 0x338, "netbuf_globals_t must be 0x338 bytes");

/* Convenience macros for global access */
#define NETBUF_$VA_SLOTS (NETBUF_GLOBALS->va_slots)
#define NETBUF_$SPIN_LOCK (NETBUF_GLOBALS->spin_lock)
#define NETBUF_$DAT_ALLOCS (NETBUF_GLOBALS->dat_allocs)
#define NETBUF_$HDR_ALLOCS (NETBUF_GLOBALS->hdr_allocs)
#define NETBUF_$DAT_DELAYS (NETBUF_GLOBALS->dat_delays)
#define NETBUF_$HDR_DELAYS (NETBUF_GLOBALS->hdr_delays)
#define NETBUF_$DAT_LIM (NETBUF_GLOBALS->dat_lim)
#define NETBUF_$DAT_CNT (NETBUF_GLOBALS->dat_cnt)
#define NETBUF_$DAT_TOP (NETBUF_GLOBALS->dat_top)
#define NETBUF_$HDR_TOP (NETBUF_GLOBALS->hdr_top)
#define NETBUF_$VA_TOP (NETBUF_GLOBALS->va_top)
#define NETBUF_$VA_BASE_ADDR (NETBUF_GLOBALS->va_base)
#define NETBUF_$HDR_ALLOC (NETBUF_GLOBALS->hdr_alloc)
#define NETBUF_$DELAY_TIME (NETBUF_GLOBALS->delay_time)

/*
 * Data buffer next pointer access
 *
 * Data buffers keep their free-list link in the MMAPE word at offset 0x06 of
 * the 16-byte entry - mmap.h calls that field prev_vpn.  Every netbuf site
 * spells it "movea.l #0xeb4800,A0 / lsl.l #0x4,Dn / lea (0,A0,Dn),A1" and
 * then addresses (-0x1ffa,A1), i.e. 0xEB4800 - 0x1FFA = 0xEB2806 = MMAPE base
 * + 6 (0x00E0EAA2, 0x00E0EADA, 0x00E0EF64, 0x00E0F086).  It is NOT next_vpn
 * at 0x0A.
 */
#define NETBUF_DAT_NEXT(ppn) (MMAPE_BASE[(ppn)].prev_vpn)

/*
 * Header buffer structure access
 *
 * Header buffers are 1KB each.  The free-list link is the longword at offset
 * 0x3E4 (0x00E0ED84 "movea.l (0x328,A5),A0 / move.l (0x3e4,A0),(0x328,A5)",
 * 0x00E0EA4A) and the buffer's physical address is the longword at 0x3FC
 * (0x00E0EA26, 0x00E0EEA4).
 *
 * The buffer is addressed by target virtual address, so the field accessors
 * go through ARCH_VA_TO_PTR: on m68k that is the identity cast, and a host
 * test can point ARCH_HOST_VA_BASE at its own arena.
 */
#define NETBUF_HDR_FIELD(va, off)                                              \
  (*(uint32_t *)((uint8_t *)ARCH_VA_TO_PTR(va) + (off)))
#define NETBUF_HDR_NEXT(va) NETBUF_HDR_FIELD(va, NETBUF_HDR_NEXT_OFF)
#define NETBUF_HDR_PHYS(va) NETBUF_HDR_FIELD(va, NETBUF_HDR_PHYS_OFF)

/*
 * Process type 7 is the network process type that can wait on delays
 */
#define NETBUF_NETWORK_PROC_TYPE 7

/*
 * Internal helper function prototypes
 */

/*
 * netbuf_rtnva_locked - Return VA slot to free list (caller holds lock)
 *
 * @param va_ptr  Pointer to virtual address to return
 *
 * @return Physical page address that was stored in the slot
 *
 * Original address: 0x00E0E8C4
 */
uint32_t netbuf_rtnva_locked(uint32_t *va_ptr);

/*
 * NETBUF_$DELAY_TYPE - TIME_$WAIT's delay-type argument, a Pascal `const`
 * passed by reference out of the code region: both waiters resolve to the
 * zero word at 0x00E0EEB2 (0x00E0EE1C "pea (0x94,PC)" and 0x00E0EFE6
 * "pea (-0x136,PC)").  Zero is the relative-delay type.
 */
extern uint16_t NETBUF_$DELAY_TYPE;

/*
 * Error status for crash
 */
extern status_$t netbuf_err;

#endif /* NETBUF_INTERNAL_H */
