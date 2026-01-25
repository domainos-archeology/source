/*
 * IIC - Inter-Integrated Circuit Bus Interface Data
 *
 * Global data definitions for the IIC subsystem.
 */

#include "iic/iic.h"

/*
 * IIC Network UIDs
 *
 * These UIDs identify different network interfaces in the system.
 */

/* IIC network UID - Address: 0xE17484 */
uid_t IIC_$NETWORK_UID = {0x00000701, 0};

/* Nil network UID - Address: 0xE1748C */
uid_t NIL_$NETWORK_UID = {0x00000702, 0};

/* User network UID - Address: 0xE1749C */
uid_t USER_$NETWORK_UID = {0x00000704, 0};
