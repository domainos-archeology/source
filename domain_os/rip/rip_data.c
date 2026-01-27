#include "rip_internal.h"

/* Main RIP data structure at 0xE26258 */
rip_$data_t RIP_$DATA;

/* RIP_$INFO - Base of routing table entries (separate from RIP_$DATA.entries) */
rip_$entry_t *RIP_$INFO;

/* RIP_$STATS - Protocol statistics at 0xE262AC */
rip_$stats_t RIP_$STATS;

/* Routing port counts */
int16_t ROUTE_$STD_N_ROUTING_PORTS;
int16_t ROUTE_$N_ROUTING_PORTS;

/* Recent changes flags (signed bytes - negative means changes pending) */
int8_t RIP_$STD_RECENT_CHANGES;
int8_t RIP_$RECENT_CHANGES;
