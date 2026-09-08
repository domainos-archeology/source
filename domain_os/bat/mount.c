/*
 * BAT_$MOUNT - Mount a volume's BAT
 *
 * Initializes the BAT data structures for a volume by reading the
 * volume label and partition information from disk.
 *
 * Original address: 0x00E3B6F8
 */

#include "bat/bat_internal.h"

/*
 * BAT_$MOUNT
 *
 * Parameters:
 *   vol_idx    - Volume index (1-6)
 *   salvage_ok - If negative, skip salvage check; otherwise require clean volume
 *   status     - Output status code
 *
 * Assembly analysis:
 *   - Gets current time from TIME_$CURRENT_CLOCKH
 *   - Takes ML_LOCK_BAT for thread safety
 *   - Clears mount status, then reads volume label (block 0)
 *   - Determines volume format (old vs new) based on label.version
 *   - Checks salvage flag unless salvage_ok is negative
 *   - Copies volume statistics from label to bat_$volumes array
 *   - Copies partition table from label to bat_$volumes array
 *   - For old format volumes, sets up single partition covering all blocks
 *   - Calculates allocation chunk parameters from disk geometry
 *   - Updates label with mount time and marks buffer dirty
 */
void BAT_$MOUNT(int16_t vol_idx, int8_t salvage_ok, status_$t *status)
{
    uint32_t current_time;
    bat_$label_t *label;
    bat_$volume_t *vol;
    disk_$volume_t *dvol;
    int16_t i;
    boolean needs_salvage;
    boolean is_new_format;
    uint32_t chunk_size;
    uint32_t chunk_offset;

    /* Capture current time before taking lock */
    current_time = TIME_$CURRENT_CLOCKH;

    ML_$LOCK(ML_LOCK_BAT);

    /* Clear mount status for this volume */
    bat_$mounted[vol_idx] = 0;

    /* Read volume label (block 0) */
    label = (bat_$label_t *)DBUF_$GET_BLOCK(vol_idx, 0, (void *)&LV_LABEL_$UID,
                                             0, 0, status);
    if (*status != status_$ok) {
        goto done;
    }

    vol = &bat_$volumes[vol_idx];

    /*
     * Determine volume format from the label version word and record it as a
     * whole byte:
     *
     *   0x00E3B760  tst.w (A0)              ; label->version
     *   0x00E3B762  sne D0b                 ; 0xFF if non-zero, else 0
     *   0x00E3B764  move.b D0b,(0xd3f,A2)   ; bat_$volume_flags[vol_idx]
     *   0x00E3B768  tst.b (0xd3f,A2)        ; re-read for the branch below
     */
    bat_$volume_flags[vol_idx] = (label->version != 0) ? (int8_t)-1 : (int8_t)0;
    is_new_format = (bat_$volume_flags[vol_idx] < 0);

    /* Check salvage flag */
    if (is_new_format) {
        needs_salvage = ((label->volume_trouble & 0x1000) != 0);
    } else {
        needs_salvage = ((int16_t)label->volume_trouble < 0);
    }

    /* Also check if salvage_flag field indicates salvage needed */
    if (label->salvage_flag == 1) {
        needs_salvage = true;
    }

    /* If salvage needed and not allowed, return error */
    if (needs_salvage && salvage_ok >= 0) {
        DBUF_$SET_BUFF(label, BAT_BUF_CLEAN, status);
        *status = status_$disk_needs_salvaging;
        goto done;
    }

    /* Mark volume as mounted */
    bat_$mounted[vol_idx] = (int8_t)0xFF;

    /* Update label timestamps */
    label->mount_time_high = current_time;

    /*
     * 0x00E3B79C-0x00E3B7A6: `andi.b #-0x11,(0x3c,A0)` then `or.b D6b` with
     * D6b = needs_salvage << 4 address the HIGH byte of the volume_trouble
     * word on big-endian m68k, so the bit rewritten is bit 12 of the word --
     * the same bit the new-format test reads with `btst.l #0xc`.
     */
    label->volume_trouble &= 0xEFFF;
    if (needs_salvage) {
        label->volume_trouble |= 0x1000;
    }

    /*
     * 0x00E3B7AA..0x00E3B7B4: the (step_blocks, bat_step) pair at label
     * +0x3E is tested and defaulted as ONE longword, `tst.l (0x3e,A0)` /
     * `move.l D6,(0x3e,A0)` with D6 = 3, which leaves step_blocks 0 and
     * bat_step 3 on a big-endian machine.
     */
    if (BAT_LABEL_STEP_LONG(label) == 0) {
        label->step_blocks = 0;
        label->bat_step = 3;
    }

    /*
     * 0x00E3B7B6..0x00E3B7C6: keep the label's top 12 bits and OR in
     * NODE_$ME.  The image applies NO mask to NODE_$ME:
     *
     *   0x00E3B7B6  andi.l #-0x100000,(0xb4,A0)
     *   0x00E3B7BE  move.l (0x00e245a4).l,D6      ; NODE_$ME, whole longword
     *   0x00E3B7C4  or.l D6,(0xb4,A0)
     *
     * so a node id with bits above 20 set does reach the preserved field.
     */
    label->mount_time_low &= 0xFFF00000;
    label->mount_time_low |= NODE_$ME;

    /* Record boot time and current time */
    label->boot_time = TIME_$BOOT_TIME;
    label->dismount_time = current_time;

    /* Mark volume as needing salvage on disk until clean dismount */
    label->salvage_flag = 1;

    /*
     * Copy the BAT header out of the label: BAT_HEADER_LONGWORDS longwords
     * from label +0x2C into bat_$volume_t +0x00.
     *
     *   0x00E3B7DA  lea (0x2c,A0),A4        ; source = label + 0x2C
     *   0x00E3B7DE  lea (-0x234,A1),A2      ; dest   = volume record + 0x00
     *   0x00E3B7E2  moveq #0x7,D6
     *   0x00E3B7E4  move.l (A4)+,(A2)+
     *   0x00E3B7E6  dbf D6w,0x00e3b7e4
     */
    {
        const uint32_t *src = (const uint32_t *)&label->total_blocks;
        uint32_t *dst = (uint32_t *)&vol->total_blocks;

        for (i = 0; i < BAT_HEADER_LONGWORDS; i++) {
            *dst++ = *src++;
        }
    }

    /*
     * Copy the partition table out of the label: BAT_PART_TABLE_LONGWORDS
     * longwords from label +0xFC into bat_$volume_t +0x20, i.e. 0x20C bytes
     * ending at +0x22B -- the two cells at +0x22C and +0x230 are computed
     * below and are NOT part of the copy.
     *
     *   0x00E3B7EA  lea (0xfc,A0),A2        ; source = label + 0xFC
     *   0x00E3B7EE  lea (-0x214,A1),A4      ; dest   = volume record + 0x20
     *   0x00E3B7F2  move.w #0x82,D6w
     *   0x00E3B7F8  move.l (A2)+,(A4)+
     *   0x00E3B7FA  dbf D6w,0x00e3b7f8
     */
    {
        const uint32_t *src = (const uint32_t *)&label->num_partitions;
        uint32_t *dst = (uint32_t *)&vol->num_partitions;

        for (i = 0; i < BAT_PART_TABLE_LONGWORDS; i++) {
            *dst++ = *src++;
        }
    }

    /* Handle old format volumes - set up single partition */
    if (!is_new_format) {
        vol->partition_size = 0x7FFFFFFF;  /* Maximum size */
        vol->num_partitions = 1;
        vol->partitions[0].free_count = vol->free_blocks - 0xB;
    }

    /*
     * Calculate the allocation-chunk (track) parameters from the drive's
     * geometry and store them at +0x22C / +0x230, just past the partition
     * table (0x00E3B81E-0x00E3B874).
     */
    /*
     *   0x00E3B820  movea.l #0xe7a290,A2    ; DISK_$DVTBL
     *   0x00E3B830  lea (0x0,A2,D6w*0x1),A2 ; A2 = DISK_VOL(vol_idx) + 0x48
     *   0x00E3B82E  clr.l D7
     *   0x00E3B834  move.w (-0x24,A2),D7w   ; blocks_per_cyl, zero-extended
     *   0x00E3B838  move.l D7,(-0x8,A1)     ; vol->alloc_chunk_size
     *   0x00E3B83C  cmpi.w #0x1,(-0x12,A2)  ; interleave_mode == 1 ?
     *   0x00E3B846  move.w (-0x1c,A2),-(SP) ; num_parts  (2nd argument)
     *   0x00E3B84A  move.l D7,-(SP)         ; blocks_per_cyl (1st argument)
     *   0x00E3B84C  jsr M$MIU$LLW
     *   0x00E3B858  move.l (-0x40,A2),D0    ; lv_start
     *   0x00E3B85C  add.l (-0x228,A1),D0    ; + vol->first_data_block
     */
    dvol = DISK_VOL(vol_idx);
    /* blocks_per_cyl, +0x24 = (-0x24,A2) at 0x00E3B834 */
    chunk_size = (uint32_t)dvol->blocks_per_cyl;
    vol->alloc_chunk_size = chunk_size;

    /* part_volx[0], +0x36 = (-0x12,A2) at 0x00E3B83C, is the interleave mode */
    if (dvol->part_volx[0] == 1) {
        /* Striped volume: one chunk spans every member's cylinder */
        /* num_parts, +0x2c = (-0x1c,A2) at 0x00E3B846 */
        chunk_size = M$MIU$LLW(chunk_size, dvol->num_parts);
        vol->alloc_chunk_size = chunk_size;
    }

    /* Calculate chunk offset: (first_data_block + lv_start) % chunk_size */
    /* lv_start, +0x08 = (-0x40,A2) at 0x00E3B858 */
    chunk_offset = M$OIS$LLL(vol->first_data_block + dvol->lv_start,
                             chunk_size);
    vol->alloc_chunk_offset = chunk_size - chunk_offset;

    /* Write back label with updated info */
    DBUF_$SET_BUFF(label, BAT_BUF_WRITEBACK, status);

done:
    ML_$UNLOCK(ML_LOCK_BAT);
}
