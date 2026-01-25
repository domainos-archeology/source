/*
 * DISK_$REGISTER - Register a disk device driver
 *
 * Registers a disk device driver with the disk subsystem. The driver
 * provides a jump table with function pointers for device-specific
 * operations (init, I/O, etc.).
 *
 * @param type        Pointer to device type identifier
 * @param controller  Pointer to controller number
 * @param units       Pointer to unit count
 * @param flags       Pointer to device flags
 * @param jump_table  Pointer to pointer to jump table
 * @return 0xFF if registered successfully, 0 if failed
 */

#include "disk_internal.h"

/* Device registration table at 0xe7ad5c */
#define DISK_DEVICE_TABLE  ((uint32_t *)0x00e7ad5c)

uint8_t DISK_$REGISTER(uint16_t *type, uint16_t *controller, uint16_t *units,
                       uint16_t *flags, void **jump_table)
{
    uint16_t dev_type;
    uint16_t ctlr;
    uint16_t unit_count;
    uint16_t dev_flags;
    void *jtable;
    int16_t i;
    uint32_t *entry;

    /* Extract parameters */
    dev_type = *type;
    ctlr = *controller;
    unit_count = *units;
    dev_flags = *flags;
    jtable = *jump_table;

    /*
     * Validate jump table - must have DINIT (+0x08) and DO_IO (+0x10)
     * function pointers set.
     */
    if (*(uint32_t *)((uint8_t *)jtable + 0x08) == 0 ||
        *(uint32_t *)((uint8_t *)jtable + 0x10) == 0) {
        return 0;
    }

    /* Search for empty slot in device table */
    entry = DISK_DEVICE_TABLE;
    for (i = 0x1f; i >= 0; i--) {
        if (*entry == 0) {
            /* Found empty slot - register device */
            *(uint16_t *)((uint8_t *)entry + 4) = dev_type;
            *(uint16_t *)((uint8_t *)entry + 6) = ctlr;
            *(uint16_t *)((uint8_t *)entry + 8) = unit_count;
            *(uint16_t *)((uint8_t *)entry + 10) = dev_flags;
            *entry = (uint32_t)(uintptr_t)jtable;
            return 0xff;
        }
        entry += 3;  /* 12 bytes = 3 longs */
    }

    /* No empty slot found */
    return 0;
}
