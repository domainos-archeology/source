/*
 * DISK_INIT - Winchester disk drive initialization
 *
 * Initializes a Winchester (hard disk) drive by communicating with it
 * via ANSI standard commands. Identifies the drive type and returns
 * its geometry parameters.
 *
 * From: 0x00e19986
 *
 * Supported drive types:
 *   0x103 - Micropolis 1203 (5 heads, 525 tracks, 12 blocks/track)
 *   0x104 - PRIAM 3450 (5 heads, 525 tracks, 12 blocks/track)
 *   0x105 - PRIAM 7050 (5 heads, 1049 tracks, 12 blocks/track)
 *
 * Parameters:
 *   drive_num       - Drive number (only 0 supported)
 *   unit_num        - Unit number (must be 0)
 *   total_blocks    - Pointer to receive total block count (or pre-set)
 *   blocks_per_track- Pointer to receive blocks per track
 *   heads           - Pointer to receive head count
 *   param_6         - Additional geometry info (tracks per zone, etc.)
 *   disk_id         - Pointer to receive disk ID (0x1xx where xx is type)
 *
 * Returns:
 *   status_$ok on success
 *   status_$invalid_unit_number if unit_num != 0
 *   status_$unrecognized_drive_id if drive type unknown
 */

#include "disk/disk_internal.h"
#include "win/win.h"
#include "ec/ec.h"
#include "time/time.h"
#include "math/math.h"

/* Status codes */
#define status_$invalid_unit_number     0x00080018
#define status_$unrecognized_drive_id   0x00080024

/* ANSI drive commands */
#define ANSI_CMD_REPORT_GENERAL_STATUS  0x0F
#define ANSI_CMD_REPORT_DRIVE_ATTRIBUTE 0x10
#define ANSI_CMD_WRITE_CONTROL          0x41
#define ANSI_CMD_LOAD_ATTRIBUTE_NUMBER  0x50
#define ANSI_CMD_SPIN_CONTROL           0x55

/* Drive type IDs (low nibble of drive attribute) */
#define DRIVE_TYPE_MICROPOLIS_1203  0x03
#define DRIVE_TYPE_PRIAM_3450       0x04
#define DRIVE_TYPE_PRIAM_7050       0x05

/* Disk base data at 0xe2b89c */
#if defined(M68K)
#define DISK_WIN_BASE       ((uint8_t *)0xe2b89c)
#define DISK_WIN_DATA       ((uint32_t *)0xe2b8a0)  /* Array of 3-word entries */
#define DISK_WIN_EC         ((uint32_t *)0xe2b0d4)
#define DISK_WIN_FLAG       (*(uint32_t *)0xe2b8fc)
#else
extern uint8_t *disk_win_base;
extern uint32_t *disk_win_data;
extern uint32_t *disk_win_ec;
extern uint32_t disk_win_flag;
#define DISK_WIN_BASE       disk_win_base
#define DISK_WIN_DATA       disk_win_data
#define DISK_WIN_EC         disk_win_ec
#define DISK_WIN_FLAG       disk_win_flag
#endif

/* Bit patterns for ANSI commands */
static const uint8_t BIT7_SET = 0x80;

/* Attribute number to load for drive ID */
static const uint8_t ATTR_DRIVE_ID = 0x00;  /* From 0xe19bbe */

/* Empty data for status commands */
static const uint8_t ANSI_EMPTY_DATA = 0x0A;  /* From 0xe1940a */

extern void FUN_00e190bc(uint16_t drive);  /* Drive init helper */

status_$t DISK_INIT(uint16_t drive_num, int16_t unit_num,
                    int32_t *total_blocks, uint16_t *blocks_per_track,
                    uint16_t *heads, uint16_t *param_6, int16_t *disk_id)
{
    status_$t status;
    int16_t retry;
    uint8_t status_byte;
    uint8_t drive_attr;
    uint16_t drive_type;
    uint16_t tracks;
    uint32_t entry_offset;
    uint8_t *entry_ptr;

    /* Only unit 0 is supported */
    if (unit_num != 0) {
        return status_$invalid_unit_number;
    }

    /* Clear global flag */
    DISK_WIN_FLAG = 0;

    /* Calculate entry offset for this drive (3 words per entry) */
    entry_offset = (uint32_t)drive_num * 12;  /* 3 * 4 bytes */
    entry_ptr = (uint8_t *)(DISK_WIN_DATA) + entry_offset;

    /*
     * Retry loop - try up to 10 times to initialize the drive
     */
    for (retry = 9; retry >= 0; retry--) {
        /* Set initial drive parameters */
        entry_ptr[2] = 1;    /* +0x02: Ready flag */
        entry_ptr[14] = 6;   /* +0x0E: Retry count */

        /* Call drive init helper */
        FUN_00e190bc(drive_num);

        /* Report general status */
        status = WIN_$ANSI_COMMAND(drive_num, ANSI_CMD_REPORT_GENERAL_STATUS,
                                   &ANSI_EMPTY_DATA, &status_byte);
        WIN_$CHECK_DISK_STATUS(drive_num);

        if (status != status_$ok) {
            continue;
        }

        /* Check if drive needs spin-up (bit 0 of status) */
        if ((status_byte & 0x01) != 0) {
            /* Set spin timeout */
            entry_ptr[12] = 10;  /* +0x0C: Timeout value */

            /* Send spin-up command */
            WIN_$ANSI_COMMAND(drive_num, ANSI_CMD_SPIN_CONTROL,
                              &BIT7_SET, &status_byte);

            /* Wait for spin-up using event counter */
            {
                uint32_t wait_value = DISK_WIN_EC[entry_offset / 4 + 12] + 1;
                ec_$eventcount_t *ec_array[3];
                ec_array[0] = (ec_$eventcount_t *)&DISK_WIN_EC[entry_offset / 4 + 12];
                ec_array[1] = (ec_$eventcount_t *)DISK_WIN_EC;
                ec_array[2] = (ec_$eventcount_t *)&TIME_$CLOCKH;
                EC_$WAIT(ec_array, (uint32_t *)&wait_value);
            }

            status = WIN_$CHECK_DISK_STATUS(drive_num);
            if (status != status_$ok) {
                continue;
            }
        }

        /* Enable write operations */
        WIN_$ANSI_COMMAND(drive_num, ANSI_CMD_WRITE_CONTROL,
                          &BIT7_SET, &status_byte);
        status = WIN_$CHECK_DISK_STATUS(drive_num);

        if (status == status_$ok) {
            break;  /* Success - exit retry loop */
        }
    }

    if (status != status_$ok) {
        return status;
    }

    /*
     * Read drive identification
     */
    /* Load attribute number for drive ID */
    WIN_$ANSI_COMMAND(drive_num, ANSI_CMD_LOAD_ATTRIBUTE_NUMBER,
                      &ATTR_DRIVE_ID, &drive_attr);
    status = WIN_$CHECK_DISK_STATUS(drive_num);

    if (status == status_$ok) {
        /* Read drive attribute */
        WIN_$ANSI_COMMAND(drive_num, ANSI_CMD_REPORT_DRIVE_ATTRIBUTE,
                          &ANSI_EMPTY_DATA, &drive_attr);
    }

    status = WIN_$CHECK_DISK_STATUS(drive_num);

    /* Extract drive type from low nibble and form ID */
    drive_type = drive_attr & 0x0F;
    *disk_id = 0x100 + drive_type;

    if (status != status_$ok) {
        return status;
    }

    /*
     * If total_blocks is already set (> 0), just return success.
     * Otherwise, set geometry based on drive type.
     */
    if (*total_blocks > 0) {
        return status_$ok;
    }

    /* Clear first param_6 word */
    *param_6 = 0;
    status = status_$ok;

    /*
     * Set geometry based on drive type
     */
    switch (drive_type) {
    case DRIVE_TYPE_PRIAM_3450:
        /* PRIAM 3450: 5 heads, 525 tracks, 12 blocks/track */
        *blocks_per_track = 12;
        *heads = 5;
        tracks = 525;
        param_6[1] = 1120;  /* Tracks per zone */
        break;

    case DRIVE_TYPE_MICROPOLIS_1203:
        /* Micropolis 1203: 5 heads, 525 tracks, 12 blocks/track */
        *blocks_per_track = 12;
        *heads = 5;
        tracks = 525;
        param_6[1] = 1181;  /* Different zone size */
        break;

    case DRIVE_TYPE_PRIAM_7050:
        /* PRIAM 7050: 5 heads, 1049 tracks, 12 blocks/track */
        *blocks_per_track = 12;
        *heads = 5;
        tracks = 1049;
        param_6[1] = 1120;
        break;

    default:
        /* Unrecognized drive type */
        status = status_$unrecognized_drive_id;
        tracks = 0;
        break;
    }

    /*
     * Calculate total blocks: heads * tracks * blocks_per_track
     * Uses math library for 32-bit multiplication
     */
    if (tracks != 0) {
        uint32_t total_tracks = M_MIS_LLW((uint32_t)*heads, tracks);
        *total_blocks = M_MIS_LLL(total_tracks, (uint32_t)*blocks_per_track);
    }

    return status;
}
