/*
 * WIN Internal - Winchester Disk Driver Internal Definitions
 *
 * This header contains internal definitions used within the win subsystem.
 * External code should use win.h instead.
 */

#ifndef WIN_INTERNAL_H
#define WIN_INTERNAL_H

#include "win/win.h"

#include "time/time.h"
#include "dma/dma.h"   /* check_dma_error */

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

/* WIN_DATA_BASE + 0x60: the request the driver is currently working on */
#define WIN_CUR_REQ (*(void **)(WIN_DATA_BASE + WIN_REQ_PTR_OFFSET))

/*
 * Internal functions (nested Pascal procedures of the WIN module)
 */
status_$t SEEK(uint16_t unit, uint16_t cylinder, void *req, uint8_t flags);
status_$t read_or_write_disk_record(uint16_t unit);
/* check_dma_error is declared in dma/dma.h (bead source-3uo). */

/*
 * WAIT_FOR_CONTROLLER (0x00E190BC, was FUN_00e190bc) - spin until the drive's
 * status word clears its busy bit, then report whether the drive is ready.
 * Called by WIN_$ANSI_COMMAND (0x00E1916A), SEEK (0x00E195D8) and DISK_INIT
 * (0x00E199EE), always right after the caller has stored a command and
 * written WIN_REG_GO.
 */
status_$t WAIT_FOR_CONTROLLER(uint16_t unit);
status_$t FUN_00e194b4(uint16_t param_1, uint16_t cylinder);
void FUN_00e19186(uint16_t unit, char status, uint16_t *out);

/*
 * The disk subsystem's per-volume record table.
 *
 * DISK's A5 is 0x00E7A1E8, so its A5 + 0x378 is 0x00E7A560, and
 * DISK_$WRITE_MULTI addresses one record with `lea (0x0,A5,D0w*0x1),A3` plus
 * `(0x378,A3)` / `(0x384,A3)` (0x00E3CE3E / 0x00E3CE46) where D0 = 0x1C *
 * index -- a 0x1C-byte record, entry n at 0x00E7A560 + 0x1C * n.
 *
 * WIN_$FORMAT_TRACK (0x00E19764) and WIN_$DO_IO (0x00E1994A) invalidate a
 * volume with `clr.b (-0x4,A1,D1w*0x1)`, A1 = 0x00E7A560 and D1 = 0x1C *
 * req->volume -- that is entry (volume - 1) + 0x18, so a request's volume
 * number is 1-BASED against this table.  FLP_FORMAT_TRACK (0x00E3DDB0) and
 * 0x00E3DFBC use the identical five instructions.
 *
 * TODO(source-8uxv): only this one byte is named; the 0x1C-byte record itself
 * has not been recovered and belongs in disk/disk.h.
 */
#if defined(ARCH_M68K)
#define WIN_VOLUME_TABLE ((volatile uint8_t *)0x00E7A560)
#else
#define WIN_VOLUME_TABLE_SIZE 0x1C0
extern uint8_t WIN_$VOLUME_TABLE[WIN_VOLUME_TABLE_SIZE];
#define WIN_VOLUME_TABLE (WIN_$VOLUME_TABLE)
#endif

#define WIN_VOLUME_ENTRY_SIZE 0x1C

/* entry (volume - 1) + 0x18 */
#define WIN_VOLUME_MOUNTED(volume)                                             \
    (WIN_VOLUME_TABLE[(uint32_t)(volume) * WIN_VOLUME_ENTRY_SIZE - 4])

#endif /* WIN_INTERNAL_H */
