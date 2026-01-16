/*
 * BAT - Block Allocation Table Data
 *
 * Global variables for the BAT subsystem.
 * Original m68k addresses shown in comments.
 */

#include "bat/bat_internal.h"

/*
 * Volume BAT data array
 * Base address: 0xE79244
 * Each entry is 0x234 (564) bytes
 */
bat_$volume_t bat_$volumes[BAT_MAX_VOLUMES];

/*
 * Cached BAT bitmap buffer
 * Address: 0xE7A1B0
 */
void *bat_$cached_buffer = NULL;

/*
 * Block number of cached BAT bitmap
 * Address: 0xE7A1B4
 */
uint32_t bat_$cached_block = 0;

/*
 * Mount status for each volume
 * Address: 0xE7A1BF
 * Value: 0xFF = mounted, 0 = not mounted
 */
int8_t bat_$mounted[BAT_MAX_VOLUMES] = {0};

/*
 * Volume flags
 * The high byte (byte 3) of each entry contains the partition type flag
 * Address: 0xE7A1B4 + 3 per volume
 */
uint32_t bat_$volume_flags[BAT_MAX_VOLUMES] = {0};

/*
 * Dirty flags for cached buffer
 * Address: 0xE7A1C6
 */
int16_t bat_$cached_dirty = 0;

/*
 * Volume index of cached buffer
 * Address: 0xE7A1C8
 */
int16_t bat_$cached_vol = 0;

/*
 * Disk info array for allocation calculations
 * Base address: 0xE7A290
 */
bat_$disk_info_t bat_$disk_info[BAT_MAX_VOLUMES];

/*
 * UID constants for buffer management
 * These are used to identify different block types in the disk cache.
 * Note: LV_LABEL_$UID is defined in uid/uid_data.c
 */

/* BAT bitmap UID - Address: 0xE173A4 */
uid_t BAT_$UID = { 0, 0 };       /* TODO: Determine actual UID values */

/* VTOCE block UID - Address: 0xE1739C */
uid_t VTOC_$UID = { 0, 0 };      /* TODO: Determine actual UID values */
