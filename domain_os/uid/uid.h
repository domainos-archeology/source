/*
 * UID - Unique Identifier Module
 *
 * UIDs are 64-bit unique identifiers used throughout Domain/OS for
 * identifying objects (files, processes, etc.). They are designed to
 * be globally unique across all nodes in a network.
 *
 * UID structure:
 *   - high (32 bits): Timestamp-based value
 *   - low (32 bits): Node ID and counter
 *
 * The low word encodes:
 *   - Bits 0-19: Node ID (from NODE_$ME)
 *   - Bits 20-31: Counter (incremented each generation)
 */

#ifndef UID_H
#define UID_H

#include "base/base.h"
#include "node/node.h"   /* NODE_$ME */

/*
 * UID generator state
 *
 * Located at 0xE2C008 on M68K.
 * Contains the last generated UID value and is protected by a spin lock.
 */
extern uid_t UID_$GENERATOR_STATE;      /* 0xE2C008: Last generated UID */
extern uint16_t UID_$GENERATOR_LOCK;    /* 0xE2C010: Spin lock for generator */

/*
 * Well-known UIDs
 *
 * These are UIDs for system objects that need to be referenced by name.
 */
extern uid_t UID_$NIL;                  /* 0xE1737C: NIL UID (all zeros) */
extern uid_t PV_LABEL_$UID;             /* 0xE1738C: Physical volume label UID */
extern uid_t LV_LABEL_$UID;             /* 0xE17394: Logical volume label UID */
extern uid_t DISKLESS_$UID;             /* 0xE173F4: Diskless node UID pattern */
extern uid_t OS_WIRED_$UID;             /* OS wired/pinned memory UID */
extern uid_t DISPLAY1_$UID;             /* 0xE173D4: Display 1 object UID (OS_$INSTALL_DISPLAY_ASTE) */
extern uid_t UNSTRUCT_$UID;             /* 0xE173C4: Unstructured file type UID */
extern uid_t NIL_$NETWORK_UID;          /* 0xE1748C: Nil network UID
                                         * (defined in iic/iic_data.c) */
extern uid_t USER_$NETWORK_UID;         /* 0xE1749C: User network UID
                                         * (defined in iic/iic_data.c) */
extern uid_t UNKNOWN_$NETWORK_UID;      /* 0xE174A4: network UID of a port
                                         * that does not exist (UID_LIST,
                                         * defined in uid/uid_data.c) */
extern uid_t PPO_$NIL_USER_UID;         /* 0xE174EC: Nil user UID
                                         * (defined in vtoc/vtoc_data.c) */
extern uid_t PPO_$NIL_ORG_UID;          /* 0xE17574: Nil org UID
                                         * (defined in vtoc/vtoc_data.c) */

/*
 * ============================================================================
 * Function Prototypes
 * ============================================================================
 */

/*
 * UID_$INIT - Initialize the UID generator
 *
 * Sets the node ID portion of the UID generator state from NODE_$ME.
 * Must be called after NODE_$ME is set during system initialization.
 *
 * Original address: 0x00e30950
 */
void UID_$INIT(void);

/*
 * UID_$GEN - Generate a new unique identifier
 *
 * Generates a globally unique 64-bit identifier. Uses the system clock
 * for the high word and a node ID + counter for the low word.
 * Thread-safe via spin lock.
 *
 * Parameters:
 *   uid_ret - Pointer to receive the generated UID
 *
 * Original address: 0x00e1a018
 */
void UID_$GEN(uid_t *uid_ret);

/*
 * UID_$HASH - Hash a UID for table indexing
 *
 * Computes a hash value from a UID for use in hash tables.
 * Returns both quotient and remainder for chained hashing.
 *
 * Parameters:
 *   uid - Pointer to the UID to hash
 *   table_size - Pointer to the hash table size (divisor)
 *
 * Returns:
 *   Packed result: high word = quotient, low word = remainder (index)
 *
 * Original address: 0x00e17360
 */
uint32_t UID_$HASH(uid_t *uid, uint16_t *table_size);

/*
 * NODE_$ME is a cell of the NET_ASM module (SAU2 map, 0xE2459C size 0xC), not
 * of UID_LIST, so it is declared in node/node.h (included above -- bead
 * source-3uo).  Its storage is still defined by uid/uid_data.c, and UID_$INIT
 * reads it.
 */

#endif /* UID_H */
