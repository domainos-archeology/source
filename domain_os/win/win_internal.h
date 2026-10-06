/*
 * WIN Internal - Winchester Disk Driver Internal Definitions
 *
 * This header contains internal definitions used within the win subsystem.
 * External code should use win.h instead.
 */

#ifndef WIN_INTERNAL_H
#define WIN_INTERNAL_H

#include "win/win.h"
#include "disk/disk.h"   /* disk_$per_proc_t: the pending-I/O byte the driver clears */

#include "time/time.h"
#include "dma/dma.h"   /* DMA_$CHECK */

/*
 * Reading the clock the WIN spin loops poll.
 *
 * TIME_$CLOCKH is advanced by the clock interrupt, so on the target every
 * read has to reach memory; time/time.h declares it as a plain uint32_t, so
 * the volatile qualifier is applied here rather than changing a declaration
 * the whole tree shares.  On a host build the unit tests supply the function
 * instead, which is what lets a spin loop terminate under test.
 */
#if defined(ARCH_M68K)
#define WIN_CLOCKH() (*(volatile uint32_t *)&TIME_$CLOCKH)
#else
uint32_t win_$host_clockh(void);
#define WIN_CLOCKH() win_$host_clockh()
#endif

/*
 * Per-unit accessors.
 *
 * The unit records are 12 bytes each starting at the module base; every
 * routine reaches them the same way, e.g. DISK_INIT at 0x00E199B6-0x00E199C4
 * builds unit*12 as ((unit*4)*2 + unit*4) and does `lea (0,A5,D4),A2`.
 * The 12-byte eventcounts share that stride from +0x30 (0x00E199C4 vs
 * 0x00E19A62 `pea (0x30,A5,D4)`).
 */
#define WIN_UNIT(unit)                                                         \
    (WIN_DATA_BASE + (uint32_t)(unit) * WIN_UNIT_ENTRY_SIZE)

/* unit + 0x04: the drive's command/status register block */
#define WIN_UNIT_REGS(unit)                                                    \
    (*(volatile uint8_t **)(WIN_UNIT(unit) + WIN_BASE_ADDR_OFFSET))

/* unit + 0x08: the ML lock id WIN_$DINIT takes around DISK_INIT */
#define WIN_UNIT_LOCK(unit)                                                    \
    (*(int16_t *)(WIN_UNIT(unit) + WIN_DEV_TYPE_OFFSET))

/* WIN_DATA_BASE + 0x30 + unit*0x0C: the per-unit eventcount */
#define WIN_UNIT_EC(unit)                                                      \
    ((ec_$eventcount_t *)(WIN_DATA_BASE + WIN_EC_ARRAY_OFFSET +                \
                          (uint32_t)(unit) * WIN_UNIT_ENTRY_SIZE))

/* WIN_DATA_BASE + 0x48: the "drive not ready" statistics word */
#define WIN_NOT_READY_COUNT (*(uint16_t *)(WIN_DATA_BASE + 0x48))


/*
 * Two WIN_ cells holding 32-bit target VAs: +0x5C the disk_$volume_t
 * WIN_$DO_IO was given and +0x60 the current request.  They are 4 bytes
 * apart, so on a 64-bit host they must not be read as native pointers;
 * WIN_$DO_IO and WIN_$INT use these with ARCH_VA_TO_PTR / ARCH_PTR_TO_VA.
 */
#define WIN_DEV_INFO_VA (*(uint32_t *)(WIN_DATA_BASE + WIN_DEV_INFO_OFFSET))
#define WIN_CUR_REQ_VA  (*(uint32_t *)(WIN_DATA_BASE + WIN_REQ_PTR_OFFSET))

/*
 * 0x00E1940A: the zero word every ANSI command in the module passes as its
 * input-parameter address (win/win_data.c).
 */
extern uint16_t WIN_ANSI_IN_PARAM;

/*
 * Internal functions (nested Pascal procedures of the WIN module)
 */
/* SEEK (0x00E19576): the cylinder and head come from the REQUEST (+0x04,
 * +0x06); argument 2 is the disk_$volume_t.dev_unit word the callers pass
 * (WIN_$DO_IO 0x00E1982C, WIN_$INT 0x00E19CA2, WIN_$FORMAT_TRACK).  `flags`
 * is a byte pushed in a word slot. */
status_$t SEEK(uint16_t unit, uint16_t dev_unit, void *req, uint8_t flags);
status_$t read_or_write_disk_record(uint16_t unit);
/* DMA_$CHECK is declared in dma/dma.h (bead source-3uo). */

/*
 * WAIT_FOR_CONTROLLER (0x00E190BC, was FUN_00e190bc) - spin until the drive's
 * status word clears its busy bit, then report whether the drive is ready.
 * Called by WIN_$ANSI_COMMAND (0x00E1916A), SEEK (0x00E195D8) and DISK_INIT
 * (0x00E199EE), always right after the caller has stored a command and
 * written WIN_REG_GO.
 */
status_$t WAIT_FOR_CONTROLLER(uint16_t unit);
/* win_$reinit_drive (0x00E194B4, was FUN_00e194b4): DISK_INIT the unit
 * again, REZERO it (ANSI 0x04) with a 0x28-tick wait, then
 * WIN_$CHECK_DISK_STATUS; bumps the counter at +0x64 and forgets the
 * current cylinder / head (+0x74, +0x72 = -1).  No result slot: the status
 * is simply D0. */
status_$t win_$reinit_drive(uint16_t unit, uint16_t dev_unit);
/* win_$sense_general_status (0x00E19186, was FUN_00e19186): argument 2 is a
 * BYTE (read from the high half of its word slot, 0x00E19194 `move.b
 * (0xa,A6)`); *out is the clearing command it wants sent (0, 1 or 2). */
status_$t win_$sense_general_status(uint16_t unit, uint8_t general_status,
                                    uint16_t *out);

/*
 * The pending-I/O byte the WIN driver clears as it retires a request.
 *
 * It is disk_$per_proc_t.io_pending (disk/disk.h): DISK_$DATA + 0x378 holds a
 * 0x1C-byte slot per process, and both WIN_$FORMAT_TRACK (0x00E19764) and
 * WIN_$DO_IO (0x00E1994A) reach the slot's +0x18 byte with the same five
 * instructions,
 *
 *   move.b (0x1e,A0),D1b        ; the request's process id
 *   lsl.w #0x2,D1w / move.w D1w,D7w / neg.w D1w / lsl.w #0x3,D7w
 *   movea.l #0xe7a560,A1
 *   add.w D7w,D1w               ; D1 = id * 0x1C
 *   clr.b (-0x4,A1,D1w*0x1)
 *
 * 0xE7A560 - 4 is 0x00E7A55C, which is (DISK_$DATA + 0x378) + 0x18: the index
 * is the 0-based process id the request carries at +0x1E, NOT a volume number
 * (the earlier "1-based volume table" reading came from taking 0x00E7A560 for
 * the array base).  FLP_FORMAT_TRACK 0x00E3DDB0 and 0x00E3DFBC are identical.
 */
#define WIN_IO_PENDING(proc_id) (DISK_$PER_PROC[(proc_id)].io_pending)

#endif /* WIN_INTERNAL_H */
