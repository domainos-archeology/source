#include "cal.h"
#include "dbuf/dbuf.h"
#include "uid/uid.h"

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
