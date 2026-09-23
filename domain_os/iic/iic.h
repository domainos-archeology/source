/*
 * IIC - Internet Interface Controller
 *
 * Public API for the IIC subsystem.  IIC is Apollo's "internet interface
 * controller" board (status module 0x2C in the SR10.2 status-code database:
 * "OS / internet interface controller"; its codes talk about DMA, multibus
 * read timeouts and a transmitter underrun).  It is NOT the I2C serial bus.
 *
 * In this SAU2 image the whole module (SAU2 map: IIC at 0xE70A54, size 0xA0)
 * is a set of stubs: every entry point stores
 * status_$iic_device_not_in_system (0x2C000A) through its status argument
 * and returns.  The by-value/by-reference shape of the arguments that the
 * stubs never read cannot be recovered from the image; only the slot count
 * (each 4 bytes) and the position of the status pointer are known.
 */

#ifndef IIC_H
#define IIC_H

#include "base/base.h"
#include "uid/uid.h"   /* NIL_$NETWORK_UID, USER_$NETWORK_UID */

/* =============================================================================
 * IIC Network UIDs
 * =============================================================================
 */
extern uid_t IIC_$NETWORK_UID;          /* 0xE17484: IIC network UID */
/* NIL_$NETWORK_UID (0xE1748C) and USER_$NETWORK_UID (0xE1749C) are cells of
 * the UID_LIST module (SAU2 map, 0xE1737C size 0x210) that uid/uid.h owns;
 * they are declared there (bead source-3uo).  Their storage is still defined
 * by iic/iic_data.c. */

/* =============================================================================
 * IIC Status Codes (module 0x2C, "OS / internet interface controller")
 * =============================================================================
 */
#define status_$iic_device_not_in_system    0x2c000a   /* "device not in system" */

/* =============================================================================
 * IIC Function Prototypes
 *
 * Frame offsets below are (d,A6) after `link.w A6,...`; (0x8,A6) is the
 * first argument.  Arguments the stub never reads are typed uint32_t as a
 * placeholder for the 4-byte slot only.
 * =============================================================================
 */

/* IIC_$INIT (0x00E70A54, 2 bytes): a bare `rts`. */
void IIC_$INIT(void);

/* IIC_$ACQUIRE (0x00E70A56, 18 bytes): *(0xC,A6) = 0x2C000A. */
void IIC_$ACQUIRE(uint32_t arg1, status_$t *status_ret);

/* IIC_$RELEASE (0x00E70A68, 18 bytes): *(0xC,A6) = 0x2C000A. */
void IIC_$RELEASE(uint32_t arg1, status_$t *status_ret);

/* IIC_$STATISTICS (0x00E70A7A, 18 bytes): *(0x10,A6) = 0x2C000A. */
void IIC_$STATISTICS(uint32_t arg1, uint32_t arg2, status_$t *status_ret);

/* IIC_$SELF_TEST (0x00E70A8C, 18 bytes): *(0x10,A6) = 0x2C000A. */
void IIC_$SELF_TEST(uint32_t arg1, uint32_t arg2, status_$t *status_ret);

/* IIC_$SEND (0x00E70A9E, 18 bytes): *(0x1C,A6) = 0x2C000A. */
void IIC_$SEND(uint32_t arg1, uint32_t arg2, uint32_t arg3,
               uint32_t arg4, uint32_t arg5, status_$t *status_ret);

/* IIC_$EXISTS (0x00E70AB0, 10 bytes): `clr.b D0` - always false. */
boolean IIC_$EXISTS(void);

/*
 * IIC_$RECEIVE (0x00E70ABA, 36 bytes): *(0x1C,A6) = 0x2C000A, then
 * `clr.w` through (0x18,A6) and through (0x10,A6) - two 16-bit out
 * parameters (presumably a byte count in and a byte count out) are zeroed.
 */
void IIC_$RECEIVE(uint32_t arg1, uint32_t arg2, uint16_t *count_ret3,
                  uint32_t arg4, uint16_t *count_ret5, status_$t *status_ret);

/* IIC_$RECEIVE_CHECK (0x00E70ADE, 20 bytes): *(0xC,A6) = 0x2C000A; `clr.b D0`. */
boolean IIC_$RECEIVE_CHECK(uint32_t arg1, status_$t *status_ret);

#endif /* IIC_H */
