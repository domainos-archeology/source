/*
 * BAT - Block Allocation Table Data
 *
 * The BAT module's data segment.  The SAU2 link map has
 *
 *   D    E79478  BAT_     size = D54
 *        E79478  BAT_DATA
 *   D    E7A1CC  DISK_    size = B90
 *
 * so the segment is 0xE79478..0xE7A1CB.  Six 0x234-byte volume records fill
 * 0xE79478..0xE7A1AF and the module scalars occupy the remaining 0x1C bytes
 * at 0xE7A1B0.  Every routine loads A5 = 0xE79478 and reaches the records at
 * negative displacements from A5 + vol_idx*0x234, so volume indices run 1..6
 * and the arrays below carry a phantom element 0.  The scalar addresses and
 * their extent asserts live in bat/bat_internal.h.
 */

#include "bat/bat_internal.h"

/*
 * Volume BAT data array
 * Biased base 0xE79244; entries 1..6 occupy 0xE79478..0xE7A1AF.
 */
bat_$volume_t bat_$volumes[BAT_MAX_VOLUMES];

/*
 * Cached BAT bitmap buffer -- 0xE7A1B0, reached as (0xd38,A5).
 * A longword: 0x00E3B2E2 `move.l A0,(0xd38,A5)`, 0x00E3B2EE `clr.l`.
 */
void *bat_$cached_buffer = NULL;

/*
 * Block number of the cached BAT bitmap -- 0xE7A1B4, reached as (0xd3c,A5).
 * A longword: 0x00E3B300 `move.l D5,(0xd3c,A5)`, 0x00E3B294 `cmp.l`.
 */
uint32_t bat_$cached_block = 0;

/*
 * Per-volume new-format flag -- biased base 0xE7A1B7, reached as (0xd3f,An)
 * with An = A5 + vol_idx*1 (0x00E3BAA6 `lea (0x0,A5,D2w*0x1),A1`), so the
 * live entries are the six bytes 0xE7A1B8..0xE7A1BD.  Entry 0 is a phantom
 * that overlaps the last byte of bat_$cached_block in the image; nothing
 * ever touches it.
 *
 * Byte-wide at every site: 0x00E3B764 `move.b D0b,(0xd3f,A2)`,
 * 0x00E3B140 `move.b (0xd3f,A0),D3b`, 0x00E3BAAE, and the `tst.b` at
 * 0x00E3B14A / 0x00E3B768 / 0x00E3B96C / 0x00E3BAC2.
 */
int8_t bat_$volume_flags[BAT_MAX_VOLUMES] = {0};

/*
 * Per-volume mount flag -- biased base 0xE7A1BF, reached as (0xd47,An) with
 * An = A5 + vol_idx*1 (0x00E3BA2E), so the live entries are the six bytes
 * 0xE7A1C0..0xE7A1C5.  0xFF = mounted (0x00E3B792 `st (0xd47,A2)`),
 * 0 = not mounted (0x00E3B72A `clr.b (0xd47,A2)`).
 */
int8_t bat_$mounted[BAT_MAX_VOLUMES] = {0};

/*
 * Dirty state of the cached buffer -- 0xE7A1C6, reached as (0xd4e,A5).
 * A word: 0x00E3B304 `move.w #0x8,(0xd4e,A5)`, 0x00E3B35C `move.w #0x9`.
 */
int16_t bat_$cached_dirty = 0;

/*
 * Volume index owning the cached buffer -- 0xE7A1C8, reached as (0xd50,A5).
 * A word: 0x00E3B2FA `move.w (0x8,A6),(0xd50,A5)`, 0x00E3B2F2 `clr.w`.
 */
int16_t bat_$cached_vol = 0;

/*
 * The BAT manager's view of DISK_$DVTBL.  This storage belongs to the DISK
 * module (map: `E7A290  DISK_$DVTBL` inside `D E7A1CC  DISK_ size = B90`);
 * BAT_$MOUNT only reads it, with the same 0x48 bias DISK_VOL() uses, so
 * entry 1 is DISK_$DVTBL itself at 0xE7A290 and entry 0 is a phantom at
 * 0xE7A248.
 *
 * TODO(source-9ddf, 0x00E3B820): this definition duplicates disk_$volume_t
 * in disk/disk_internal.h.  When disk/ can be edited, promote that record to
 * disk/disk.h and drop both this object and bat_$disk_info_t.
 */
bat_$disk_info_t bat_$disk_info[BAT_MAX_VOLUMES];

/*
 * UID constants for buffer management
 * These are used to identify different block types in the disk cache.
 * Note: LV_LABEL_$UID is defined in uid/uid_data.c
 * Note: VTOC_$UID is defined in vtoc/vtoc_data.c
 */

/* BAT bitmap UID - Address: 0xE173A4 */
uid_t BAT_$UID = UID_CONST(0x00000203, 0);
