#include "cal.h"
#include "proc1/proc1.h"

// External functions for disk buffer management and locking
extern void *DBUF_$GET_BLOCK(ushort vol_idx, int block, void *uid, int param4, int param5, status_$t *status);
extern void DBUF_$SET_BUFF(void *buffer, short flags, status_$t *status);
extern void *LV_LABEL_$UID;

#define CAL_LOCK_ID 0xe

// Writes timezone information to the boot volume's label block.
// Validates that the timezone name contains only printable characters.
//
// The timezone info is stored at offset 0xE0 in the volume label:
//   0xE0: utc_delta (2 bytes)
//   0xE2: tz_name (4 bytes)
//   0xB0: current time high word (also written to 0xE6)
void CAL_$WRITE_TIMEZONE(cal_$timezone_rec_t *tz_in, status_$t *status) {
    ushort vol_idx;
    void *buffer;
    status_$t local_status;
    int i;
    unsigned char c;

    vol_idx = CAL_$BOOT_VOLX;

    // Validate timezone name characters (must be printable ASCII or high chars)
    for (i = 0; i < 4; i++) {
        c = (unsigned char)tz_in->tz_name[i];
        if (c < 0x20 || (c > 0x7E && c < 0xA1)) {
            *status = status_$cal_date_or_time_invalid;
            return;
        }
    }

    // Copy input to global timezone record
    CAL_$TIMEZONE = *tz_in;

    // If not diskless, write to disk
    if (NETWORK_$DISKLESS >= 0) {
        PROC1_$SET_LOCK(CAL_LOCK_ID);

        buffer = DBUF_$GET_BLOCK(vol_idx, 0, &LV_LABEL_$UID, 0, 0, &local_status);
        if (local_status != status_$ok) {
            PROC1_$CLR_LOCK(CAL_LOCK_ID);
            *status = local_status;
            return;
        }

        // Write timezone data to disk buffer
        *(short *)((char *)buffer + 0xE0) = CAL_$TIMEZONE.utc_delta;
        *((char *)buffer + 0xE2) = CAL_$TIMEZONE.tz_name[0];
        *((char *)buffer + 0xE3) = CAL_$TIMEZONE.tz_name[1];
        *((char *)buffer + 0xE4) = CAL_$TIMEZONE.tz_name[2];
        *((char *)buffer + 0xE5) = CAL_$TIMEZONE.tz_name[3];

        // Write current time to both 0xB0 and 0xE6
        *(uint *)((char *)buffer + 0xB0) = TIME_$CLOCKH;
        *(uint *)((char *)buffer + 0xE6) = *(uint *)((char *)buffer + 0xB0);

        DBUF_$SET_BUFF(buffer, 0xB, &local_status);
        PROC1_$CLR_LOCK(CAL_LOCK_ID);

        if (local_status != status_$ok) {
            *status = local_status;
            return;
        }
    }

    *status = status_$ok;
}
