---
name: net-io-driver-record
description: The 0x50-byte net_io_$driver_t record, its 16 procedure-variable slots, NET_IO_UNWIRED, and the NET_IO_$CREATE_PORT frame
metadata:
  type: project
---

# net_io_$driver_t (0x50 bytes) — recovered 2026-09-07

`route_$port_t.driver_info` (+0x48) points at one of these.  Three instances
exist in the image; the ring one (`RING_$DRIVER`, RING_$CTL + 0x518 =
**0xE86918**, passed to NET_IO_$CREATE_PORT at 0x00E2FB84) has SAU2 map symbols
in every slot and is what named the fields.

Head: `_unknown0` word (2 in all three), `max_data_len` word (+0x02),
`mtu` word (+0x04), `_unknown6` byte, `flags` byte (+0x07, bit 1 = routable).
Tail: 8-byte `network_uid` at +0x48 (NET_IO_$BOOT_DEVICE fills it from
NIL_$NETWORK_UID 0xE1748C / USER_$NETWORK_UID 0xE1749C; UNKNOWN_$NETWORK_UID
0xE174A4 is NET_IO_$DEVICE_STAT's fallback).

Slot / ring symbol / dispatcher:
08 SENDP NET_IO_$SEND · 0C GET_STATS NET_IO_$DEVICE_STAT · 10 GET_STATS
NET_IO_$DEVICE_STAT2 · 14 START and 18 STOP and 1C (nil) ROUTE_$SERVICE ·
20 PROC2_CLEANUP NET_IO_$FREE_ASID · 24 IOCTL NETWORK_$SET_SERVICE (route.h
calls this slot `set_service`) · 28/2C/30/34/38 SVC_OPEN/CLOSE/IOCTL/WRITE/READ
NET_$OPEN/CLOSE/IOCTL/SEND/RCV · 3C/40/44 OPEN_OS/CLOSE_OS/SEND_OS
MAC_OS_$OPEN/CLOSE/SEND.

**How the NET_$ dispatchers give the slot away**: each computes
`lea (0xe2451c+k).l,A0 / sub.l #0xe244f4,D0` — the difference is the field
offset handed to NET_$FIND_HANDLER, which does `add.l (0x48,A0),D3` and calls
`(A1)`, reporting 0x11001D when the slot is nil.  Any "constant address inside
a data block minus that block's base" is a field offset, not a pointer.

# NET_IO_UNWIRED (0xE81668, map size 0x14)

`port_asid[8]` at 0x00, `boot_unit` word at 0x10 (image value **0x3E7 = 999**,
the "no network boot device" sentinel), `boot_port_type` word at 0x12
(0 for boot devices 2/3, 4 for 6, 5 for 8).  NET_IO_$CREATE_PORT gives the port
matching (boot_port_type, boot_unit) index 0.

# NET_IO_$CREATE_PORT (0x00E5A4A4, 538 bytes)

Port types: 0 = hardware (RING_$INIT), 1 = NIL/local, 2 = USER routing.
`moveq #6,D0 / btst.l D2,D0` is the "type is 1 or 2" test.
Port record stores: +0x4C = 2, +0x50 = TIME_$CURRENT_CLOCKH, +0x54 = 0,
+0x34 longword = queue_length (its low half is route.h's `socket2` at 0x36).
The ROUTE_$USER_STAT clear loop at 0x00E5A66C runs 0x91 times over a 0x90-byte
record (bead source-2km0, reproduced in net_io/create_port.c).

Emitted files: `domain_os/net_io/{net_io.h,net_io_internal.h,net_io_data.c,
create_port.c,test/test_create_port.c}`.
