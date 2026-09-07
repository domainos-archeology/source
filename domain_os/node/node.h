/*
 * node/node.h - the NODE_$ cell of the NET_ASM assembly data module
 *
 * The SAU2 map places NODE_$ME in its own four-cell module:
 *
 *   D    E2459C  NET_ASM            size = C
 *        E2459C  HINT_$HINTFILE_PTR
 *        E245A0  REM_FILE_$2LONG1
 *        E245A2  NETWORK_$2LONG1
 *        E245A4  NODE_$ME                         MARKED
 *
 * Each of those four cells carries a different name prefix and is declared in
 * the public header for that prefix; this header is NODE_$'s.  There is no
 * node/ source file -- the storage for NODE_$ME is defined by uid/uid_data.c, next to
 * UID_$INIT, its principal reader.
 */

#ifndef NODE_NODE_H
#define NODE_NODE_H

#include "base/base.h"

/*
 * NODE_$ME - this node's ID.  UID_$GEN puts its low 20 bits into every UID
 * generated here (uid/gen.c), and every network path uses it as the local
 * node address.
 *
 * Original address: 0xE245A4
 */
extern uint32_t NODE_$ME;

#endif /* NODE_NODE_H */
