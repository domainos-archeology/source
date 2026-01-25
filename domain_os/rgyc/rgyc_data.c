/*
 * RGYC - Registry Constants Data
 *
 * Well-known UIDs used throughout Domain/OS for system accounts,
 * special permissions, and default values.
 *
 * These are data constants loaded at system boot.
 */

#include "rgyc/rgyc.h"

/*
 * System user/project/org UIDs - defaults for new processes
 */

/* System user UID - Address: 0xE1741C */
uid_t RGYC_$P_SYS_USER_UID = {0x00000500, 0};

/* System project UID - Address: 0xE17424 */
uid_t RGYC_$G_SYS_PROJ_UID = {0x00000540, 0};

/* System org UID - Address: 0xE1743C */
uid_t RGYC_$O_SYS_ORG_UID = {0x00000580, 0};

/*
 * Special privilege UIDs
 */

/* Login UID - Address: 0xE1742C */
uid_t RGYC_$G_LOGIN_UID = {0x00000541, 0};

/* Locksmith (superuser) UID - Address: 0xE17434 */
uid_t RGYC_$G_LOCKSMITH_UID = {0x00000542, 0};

/*
 * Additional user/group UIDs
 */

/* User UID base - Address: 0xE174F4 */
uid_t RGYC_$P_USER_UID = {0x00800001, 0};

/* Daemon UID - Address: 0xE17514 */
uid_t RGYC_$P_DAEMON_UID = {0x00800005, 0};

/* System admin group UID - Address: 0xE1752C */
uid_t RGYC_$G_SYS_ADMIN_UID = {0x00800041, 0};

/*
 * Note: RGYC_$G_NIL_UID is defined in vtoc/vtoc_data.c since it is
 * used primarily by VTOC/ACL subsystems alongside PPO_$NIL_USER_UID
 * and PPO_$NIL_ORG_UID.
 */
