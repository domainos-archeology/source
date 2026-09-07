/*
 * mac_os/mac_os_data.c - MAC_OS module data
 *
 * MAC_OS_$DATA is the module data block at 0x00E22990 (the A5 base every
 * MAC_ and MAC_OS_ routine loads with `lea (0xe22990).l,A5`).  The three
 * objects defined here are the parts of it that MAC_$OPEN, MAC_$CLOSE,
 * MAC_$DEMUX and MAC_OS_$INIT address by A5 displacement:
 *
 *   +0x7A0  MAC_OS_$CHANNEL_TABLE  10 x 20 bytes  (0x00E23130..0x00E231F8)
 *   +0x868  MAC_OS_$EXCLUSION      ml_$exclusion_t (0x00E231F8, labelled
 *                                  MAC_OS_$EXCLUSION in the image)
 *
 * The channel table base is fixed by two accesses that use different fields
 * of the same entry, both relative to A5 + channel * 20:
 *
 *   0x00E0BA26  move.w D3w,(0x7a8,A0)          -> .socket, field offset 0x08
 *   0x00E0BA30  andi.b #-0x2,(0x7b2,A0)        -> .flags,  field offset 0x12
 *
 * 0x7A8 - 0x08 = 0x7A0 = 0x7B2 - 0x12, so the table starts at A5+0x7A0 and
 * the ten entries MAC_$CLOSE's bound allows (`cmpi.w #0xa,(A2)` / `bcc` at
 * 0x00E0BA90) end at A5+0x868 -- exactly where MAC_OS_$EXCLUSION begins.
 *
 * MAC_OS_$PORT_PKT_TABLES and MAC_OS_$PORT_INFO_TABLE are declared in
 * mac_os/mac_os.h but their A5 displacements are not yet pinned, so they are
 * not defined here.
 */

#include "mac_os/mac_os_internal.h"

/*
 * MAC_OS_$CHANNEL_TABLE - per-channel receive state
 *
 * Address: 0x00E23130 (MAC_OS_$DATA + 0x7A0)
 */
mac_os_$channel_t MAC_OS_$CHANNEL_TABLE[MAC_OS_MAX_CHANNELS];

/*
 * MAC_OS_$EXCLUSION - the lock held while the channel table is updated
 *
 * MAC_OS_$INIT calls ML_$EXCLUSION_INIT on it at 0x00E2F50A.
 *
 * Address: 0x00E231F8 (MAC_OS_$DATA + 0x868)
 */
ml_$exclusion_t MAC_OS_$EXCLUSION;
