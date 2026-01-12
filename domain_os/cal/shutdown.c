#include "cal.h"

// External functions for disk buffer management
extern void *DBUF_$GET_BLOCK(ushort vol_idx, int block, void *uid, int param4, int param5, status_$t *status);
extern void DBUF_$SET_BUFF(void *buffer, short flags, status_$t *status);
extern void *LV_LABEL_$UID;

// Called during system shutdown to write the current time to the volume label.
// This allows the system to detect time drift on next boot.
//
// Writes the current time (TIME_$CLOCKH) to offsets 0xB0 and 0xE6 in the volume label.
void CAL_$SHUTDOWN(status_$t *status) {
    void *buffer;

    buffer = DBUF_$GET_BLOCK(CAL_$BOOT_VOLX, 0, &LV_LABEL_$UID, 0, 0, status);
    if (*status == status_$ok) {
        // Write current time to both locations
        *(uint *)((char *)buffer + 0xB0) = TIME_$CLOCKH;
        *(uint *)((char *)buffer + 0xE6) = *(uint *)((char *)buffer + 0xB0);

        DBUF_$SET_BUFF(buffer, 0xB, status);
    }
}
