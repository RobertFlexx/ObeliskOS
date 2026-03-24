/*
 * Obelisk OS - System Limits
 * From Axioms, Order.
 */

#ifndef _OBELISK_LIMITS_H
#define _OBELISK_LIMITS_H

/* Path and filename limits */
#define PATH_MAX            4096
#define NAME_MAX            255
#define SYMLINK_MAX         4096

/* Process limits */
#define PID_MAX             32768
#define NGROUPS_MAX         65536
#define ARG_MAX             131072
#define ENV_MAX             131072

/* File descriptor limits */
#define OPEN_MAX            1024
#define FD_SETSIZE          1024
#define NR_OPEN             1048576

/* I/O limits */
#define PIPE_BUF            4096
#define IOV_MAX             1024
#define SSIZE_MAX           INT64_MAX

/* Link limits */
#define LINK_MAX            65000

/* Filesystem limits */
#define FILESIZEBITS        64
#define FS_MAGIC_MAX        32

/* Memory limits */
#define PAGESIZE            4096
#define PAGE_SIZE           4096

/* IPC limits */
#define MSGMAX              8192
#define MSGMNB              16384
#define MSGMNI              32000
#define SEMMNI              32000
#define SEMMSL              32000
#define SEMMNS              1024000000
#define SEMOPM              500
#define SEMVMX              32767
#define SHMMAX              UINT64_MAX
#define SHMMIN              1
#define SHMMNI              4096
#define SHMSEG              4096

/* sysctl limits */
#define SYSCTL_NAME_MAX     64
#define SYSCTL_PATH_MAX     256
#define SYSCTL_VALUE_MAX    1024
#define SYSCTL_DEPTH_MAX    16

/* AxiomFS limits */
#define AXIOMFS_BLOCK_SIZE_MIN      512
#define AXIOMFS_BLOCK_SIZE_MAX      65536
#define AXIOMFS_BLOCK_SIZE_DEFAULT  4096
#define AXIOMFS_NAME_LEN            252
#define AXIOMFS_MAX_POLICY_SIZE     8192
#define AXIOMFS_POLICY_TIMEOUT_MS   100
#define AXIOMFS_CACHE_MAX_ENTRIES   65536

/* Prolog/Axiom daemon limits */
#define AXIOMD_MAX_QUERY_SIZE       4096
#define AXIOMD_MAX_RESPONSE_SIZE    4096
#define AXIOMD_MAX_PENDING_QUERIES  1024
#define AXIOMD_QUERY_TIMEOUT_MS     100
#define AXIOMD_RECURSION_LIMIT      1000

#endif /* _OBELISK_LIMITS_H */
