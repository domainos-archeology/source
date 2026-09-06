/*
 * WIN Internal - Winchester Disk Driver Internal Definitions
 *
 * This header contains internal definitions used within the win subsystem.
 * External code should use win.h instead.
 */

#ifndef WIN_INTERNAL_H
#define WIN_INTERNAL_H

#include "win/win.h"

/*
 * Internal functions (nested Pascal procedures of the WIN module)
 */
status_$t SEEK(uint16_t unit, uint16_t cylinder, void *req, uint8_t flags);
status_$t read_or_write_disk_record(uint16_t unit);
status_$t check_dma_error(uint16_t param);
status_$t FUN_00e190bc(uint16_t unit);
status_$t FUN_00e194b4(uint16_t param_1, uint16_t cylinder);
void FUN_00e196aa(void *dev_entry);
void FUN_00e19186(uint16_t unit, char status, uint16_t *out);

#endif /* WIN_INTERNAL_H */
