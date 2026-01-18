/*
 * NETBUF - Internal Header
 *
 * This file contains internal data structures and functions used only
 * within the NETBUF subsystem.
 */

#ifndef NETBUF_INTERNAL_H
#define NETBUF_INTERNAL_H

#include "netbuf/netbuf.h"
#include "ml/ml.h"
#include "time/time.h"
#include "mmap/mmap.h"
#include "mmu/mmu.h"
#include "wp/wp.h"
#include "proc1/proc1.h"
#include "misc/crash_system.h"

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
    uint32_t    va_slots[NETBUF_VA_SLOTS];  /* 0x000: 192 * 4 = 768 bytes */

    /* Padding to align to 0x2fc */
    uint32_t    reserved_300;               /* 0x300: delay queue header? */
    uint32_t    reserved_304;               /* 0x304: delay queue tail? */

    /* Spin lock for protecting all netbuf data */
    uint32_t    spin_lock;                  /* 0x308: Spin lock */

    /* Allocation statistics */
    uint32_t    dat_allocs;                 /* 0x30C: Data buffer allocations (fallback) */
    uint32_t    hdr_allocs;                 /* 0x310: Header buffer allocations (fallback) */
    uint32_t    dat_delays;                 /* 0x314: Data buffer delay waits */
    uint32_t    hdr_delays;                 /* 0x318: Header buffer delay waits */

    /* Data buffer pool management */
    uint32_t    dat_lim;                    /* 0x31C: Maximum data buffers to cache */
    uint32_t    dat_cnt;                    /* 0x320: Current cached data buffer count */
    uint32_t    dat_top;                    /* 0x324: Data buffer free list head (page number) */

    /* Header buffer pool management */
    uint32_t    hdr_top;                    /* 0x328: Header buffer free list head (VA) */

    /* VA slot free list */
    int32_t     va_top;                     /* 0x32C: VA slot free list head index (-1 = empty) */

    /* VA base address */
    uint32_t    va_base;                    /* 0x330: Base VA for netbuf space (0xD64C00) */

    /* Header buffer allocation count */
    int16_t     hdr_alloc;                  /* 0x334: Total header buffers allocated */
} netbuf_globals_t;

/*
 * Architecture-specific access to netbuf globals
 */
#if defined(M68K)
    #define NETBUF_GLOBALS      ((netbuf_globals_t *)0xE245A8)
    #define NETBUF_$DELAY_Q     ((time_queue_t *)0xE248A8)
    #define NETBUF_$VA_BASE     0xD64C00
#else
    extern netbuf_globals_t *netbuf_globals;
    extern time_queue_t *netbuf_delay_q;
    extern uint32_t netbuf_va_base;
    #define NETBUF_GLOBALS      netbuf_globals
    #define NETBUF_$DELAY_Q     netbuf_delay_q
    #define NETBUF_$VA_BASE     netbuf_va_base
#endif

/* Convenience macros for global access */
#define NETBUF_$VA_SLOTS        (NETBUF_GLOBALS->va_slots)
#define NETBUF_$SPIN_LOCK       (NETBUF_GLOBALS->spin_lock)
#define NETBUF_$DAT_ALLOCS      (NETBUF_GLOBALS->dat_allocs)
#define NETBUF_$HDR_ALLOCS      (NETBUF_GLOBALS->hdr_allocs)
#define NETBUF_$DAT_DELAYS      (NETBUF_GLOBALS->dat_delays)
#define NETBUF_$HDR_DELAYS      (NETBUF_GLOBALS->hdr_delays)
#define NETBUF_$DAT_LIM         (NETBUF_GLOBALS->dat_lim)
#define NETBUF_$DAT_CNT         (NETBUF_GLOBALS->dat_cnt)
#define NETBUF_$DAT_TOP         (NETBUF_GLOBALS->dat_top)
#define NETBUF_$HDR_TOP         (NETBUF_GLOBALS->hdr_top)
#define NETBUF_$VA_TOP          (NETBUF_GLOBALS->va_top)
#define NETBUF_$VA_BASE_ADDR    (NETBUF_GLOBALS->va_base)
#define NETBUF_$HDR_ALLOC       (NETBUF_GLOBALS->hdr_alloc)

/*
 * Data buffer next pointer access
 *
 * Data buffers use the MMAPE next_vpn field (at offset 0x06 in each 16-byte entry)
 * to store the free list link. MMAPE base is 0xEB2800.
 */
#define NETBUF_DAT_NEXT(ppn)    (MMAPE_BASE[(ppn)].next_vpn)

/*
 * Header buffer structure access
 *
 * Header buffers are 1KB each. The free list next pointer is at offset 0x3e4,
 * and the physical address is stored at offset 0x3fc.
 */
#define NETBUF_HDR_NEXT(va)     (*(uint32_t *)((va) + NETBUF_HDR_NEXT_OFF))
#define NETBUF_HDR_PHYS(va)     (*(uint32_t *)((va) + NETBUF_HDR_PHYS_OFF))

/*
 * Process type 7 is the network process type that can wait on delays
 */
#define NETBUF_NETWORK_PROC_TYPE    7

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
 * Delay time for waiting on buffers (constant at 0x00E0EEB2)
 */
extern uint16_t NETBUF_$DELAY_TYPE;
extern clock_t NETBUF_$DELAY_TIME;

/*
 * Error status for crash
 */
extern status_$t netbuf_err;

#endif /* NETBUF_INTERNAL_H */
