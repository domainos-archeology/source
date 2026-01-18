/*
 * OS_DISK_PROC - Clear disk process entries
 *
 * Iterates through the disk process table at 0xEB2C00, clearing entries
 * that match a specified process ID, or all entries if process ID is 0.
 *
 * The disk process table has 101 entries (0x65), each 64 bytes (0x40).
 * Each entry has 4 sub-entries of 16 bytes (0x10), with:
 *   - Offset 0x0C: process ID (2 bytes)
 *   - Offset 0x0E: status word (2 bytes, set to 0xFFFF when cleared)
 *
 * Original address: 0x00E3824C
 * Size: 92 bytes
 */

#include "os/os_internal.h"

/* Disk process table constants */
#define DISK_PROC_TABLE_BASE    0x00EB2C00
#define DISK_PROC_ENTRY_COUNT   101         /* 0x65 entries */
#define DISK_PROC_ENTRY_SIZE    0x40        /* 64 bytes per entry */
#define DISK_PROC_SUB_ENTRIES   4           /* 4 sub-entries per entry */
#define DISK_PROC_SUB_SIZE      0x10        /* 16 bytes per sub-entry */
#define DISK_PROC_PID_OFFSET    0x0C        /* Process ID offset */
#define DISK_PROC_STATUS_OFFSET 0x0E        /* Status word offset */

/*
 * OS_DISK_PROC - Clear disk process table entries
 *
 * Parameters:
 *   proc_id - Process ID to clear, or 0 to clear all entries
 *
 * When an entry's process ID matches (or proc_id == 0):
 *   - The process ID field is cleared to 0
 *   - The status field is set to 0xFFFF (invalid/unused)
 */
void OS_DISK_PROC(int16_t proc_id)
{
    int16_t entry_count;
    int16_t sub_count;
    int sub_offset;
    uint8_t *entry_base;

    entry_count = DISK_PROC_ENTRY_COUNT;
    entry_base = (uint8_t *)DISK_PROC_TABLE_BASE;

    do {
        sub_count = DISK_PROC_SUB_ENTRIES - 1;  /* 0-3 */
        sub_offset = 0;

        do {
            int16_t *pid_ptr = (int16_t *)(entry_base + DISK_PROC_PID_OFFSET + sub_offset);
            int16_t *status_ptr = (int16_t *)(entry_base + DISK_PROC_STATUS_OFFSET + sub_offset);

            if (*pid_ptr == proc_id || proc_id == 0) {
                *pid_ptr = 0;
                *status_ptr = (int16_t)0xFFFF;
            }

            sub_offset += DISK_PROC_SUB_SIZE;
            sub_count--;
        } while (sub_count != -1);

        entry_base += DISK_PROC_ENTRY_SIZE;
        entry_count--;
    } while (entry_count != -1);
}
