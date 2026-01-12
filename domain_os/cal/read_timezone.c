#include "cal.h"

// External functions for disk buffer management and locking
extern void PROC1_$SET_LOCK(short lock_id);
extern void PROC1_$CLR_LOCK(short lock_id);
extern void *DBUF_$GET_BLOCK(ushort vol_idx, int block, void *uid, int param4, int param5, status_$t *status);
extern void DBUF_$SET_BUFF(void *buffer, short flags, status_$t *status);
extern void *LV_LABEL_$UID;

// Lock ID for calendar operations
#define CAL_LOCK_ID 0xe

// Reads the timezone information from the boot volume's label block.
// If diskless, just copies the in-memory timezone data.
//
// The timezone info is stored at offset 0xE0 in the volume label:
//   0xE0: utc_delta (2 bytes)
//   0xE2: tz_name (4 bytes)
//   0xE6: last_valid_time (4 bytes)
void CAL_$READ_TIMEZONE(cal_$timezone_rec_t *tz_out, status_$t *status) {
    ushort vol_idx;
    void *buffer;
    status_$t local_status;

    vol_idx = CAL_$BOOT_VOLX;

    // If not diskless, read from disk
    if (NETWORK_$DISKLESS >= 0) {
        PROC1_$SET_LOCK(CAL_LOCK_ID);

        buffer = DBUF_$GET_BLOCK(vol_idx, 0, &LV_LABEL_$UID, 0, 0, &local_status);
        if (local_status != status_$ok) {
            PROC1_$CLR_LOCK(CAL_LOCK_ID);
            *status = local_status;
            return;
        }

        // Copy timezone data from disk buffer to global
        CAL_$TIMEZONE.utc_delta = *(short *)((char *)buffer + 0xE0);
        CAL_$TIMEZONE.tz_name[0] = *((char *)buffer + 0xE2);
        CAL_$TIMEZONE.tz_name[1] = *((char *)buffer + 0xE3);
        CAL_$TIMEZONE.tz_name[2] = *((char *)buffer + 0xE4);
        CAL_$TIMEZONE.tz_name[3] = *((char *)buffer + 0xE5);
        CAL_$LAST_VALID_TIME = *(uint *)((char *)buffer + 0xE6);

        DBUF_$SET_BUFF(buffer, 8, &local_status);
        PROC1_$CLR_LOCK(CAL_LOCK_ID);
    }

    // Copy global timezone data to output
    *tz_out = CAL_$TIMEZONE;
    *status = status_$ok;
}
