/*
 * disk/disk_data.c - DISK subsystem global data
 */

#include "disk/disk_internal.h"

/*
 * DISK_$DO_CHKSUM - Disk checksum enable flag (negative = checksums on)
 * Original address: 0xE7ACCC (1 byte; image value 0x00)
 */
int8_t DISK_$DO_CHKSUM = 0;
