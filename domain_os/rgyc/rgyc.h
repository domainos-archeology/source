/*
 * RGYC - Registry Constants
 *
 * Well-known UIDs used throughout Domain/OS for system accounts,
 * special permissions, and default values.
 *
 * These are data constants (not functions) loaded at system boot.
 */

#ifndef RGYC_H
#define RGYC_H

#include "base/base.h"

/*
 * System user/project/org UIDs - defaults for new processes
 */
extern uid_t RGYC_$P_SYS_USER_UID;  /* 0xE1741C: System user UID */
extern uid_t RGYC_$G_SYS_PROJ_UID;  /* 0xE17424: System project UID */
extern uid_t RGYC_$O_SYS_ORG_UID;   /* 0xE1743C: System org UID */

/*
 * Special privilege UIDs
 */
extern uid_t RGYC_$G_LOCKSMITH_UID; /* 0xE17434: Locksmith UID (superuser) */
extern uid_t RGYC_$G_LOGIN_UID;     /* 0xE1742C: Login UID */

#endif /* RGYC_H */
