#include "rip/rip_internal.h"

/* Main RIP data structure at 0xE26258 */
rip_$data_t RIP_$DATA;

/* RIP_$INFO (0xE263BC) is RIP_$DATA.entries; see rip_internal.h */

/* RIP_$STATS - Protocol statistics at 0xE262AC */
rip_$stats_t RIP_$STATS;

/* ROUTE_$STD_N_ROUTING_PORTS / ROUTE_$N_ROUTING_PORTS are defined in route/route_data.c */

/* Recent changes flags (signed bytes - negative means changes pending) */
int8_t RIP_$STD_RECENT_CHANGES;     /* 0xE26EDE */
int8_t RIP_$RECENT_CHANGES;         /* 0xE26EE0 */

/*
 * Data accessed at absolute addresses on m68k (see rip.h / rip_internal.h).
 */
#if !defined(ARCH_M68K)
/* RIP_$STD_IDP_CHANNEL - IDP channel for RIP packets (0xE26EBC), 0xFFFF = none */
int16_t RIP_$STD_IDP_CHANNEL = -1;

/* RIP_$NS_ANNOUNCEMENT - Name service announcement data (0xE26EBE) */
uint8_t RIP_$NS_ANNOUNCEMENT[2] = { 0x00, 0x03 };

/* RIP_$BCAST_CONTROL - Broadcast control packet template (0xE26EC0, 30 bytes) */
uint8_t RIP_$BCAST_CONTROL[30] = {
    0x00, 0x90, 0x00, 0x02, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* RIP_$ANNOUNCE_EXTRA - Extra (empty) data for RIP_$ANNOUNCE_NS (0xE68E28) */
uint8_t RIP_$ANNOUNCE_EXTRA[4];
#endif
