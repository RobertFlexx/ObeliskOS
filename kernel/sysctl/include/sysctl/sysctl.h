/*
 * Obelisk OS - sysctl Header
 * From Axioms, Order.
 */

#ifndef _SYSCTL_SYSCTL_H
#define _SYSCTL_SYSCTL_H

#include <obelisk/types.h>
#include <obelisk/limits.h>

/* sysctl node types */
#define SYSCTL_TYPE_NODE    0   /* Directory node */
#define SYSCTL_TYPE_INT     1   /* Integer value */
#define SYSCTL_TYPE_UINT    2   /* Unsigned integer */
#define SYSCTL_TYPE_LONG    3   /* Long integer */
#define SYSCTL_TYPE_ULONG   4   /* Unsigned long */
#define SYSCTL_TYPE_STRING  5   /* String value */
#define SYSCTL_TYPE_BOOL    6   /* Boolean */
#define SYSCTL_TYPE_PROC    7   /* Procedure (custom handler) */

/* sysctl flags */
#define SYSCTL_RO           0x0001  /* Read-only */
#define SYSCTL_RW           0x0002  /* Read-write */
#define SYSCTL_WO           0x0004  /* Write-only */
#define SYSCTL_ADMIN        0x0010  /* Requires admin privileges */
#define SYSCTL_RUNTIME      0x0020  /* Can change at runtime */
#define SYSCTL_BOOT         0x0040  /* Only settable at boot */

/* sysctl handler prototype */
struct sysctl_node;
typedef int (*sysctl_handler_t)(struct sysctl_node *node, void *buf,
                                size_t *len, loff_t *ppos, bool write);

/* sysctl node */
struct sysctl_node {
    char name[SYSCTL_NAME_MAX];     /* Node name */
    uint32_t type;                   /* Node type */
    uint32_t flags;                  /* Node flags */
    mode_t mode;                     /* Access mode */

    void *data;                      /* Pointer to data */
    size_t maxlen;                   /* Maximum length */

    sysctl_handler_t handler;        /* Custom handler */

    struct sysctl_node *parent;      /* Parent node */
    struct sysctl_node *children;    /* First child */
    struct sysctl_node *next;        /* Next sibling */

    const char *description;         /* Human-readable description */

    spinlock_t lock;                 /* Node lock */
};

/* sysctl table entry (for static registration) */
struct sysctl_table_entry {
    const char *path;               /* Full path */
    uint32_t type;                  /* Value type */
    uint32_t flags;                 /* Flags */
    void *data;                     /* Data pointer */
    size_t maxlen;                  /* Maximum length */
    sysctl_handler_t handler;       /* Custom handler */
    const char *description;        /* Description */
};

/* Root node */
extern struct sysctl_node *sysctl_root;

/* Initialization */
void sysctl_init(void);

/* Node registration */
struct sysctl_node *sysctl_register_node(const char *path);
int sysctl_register_int(const char *path, int *data, uint32_t flags);
int sysctl_register_uint(const char *path, unsigned int *data, uint32_t flags);
int sysctl_register_long(const char *path, long *data, uint32_t flags);
int sysctl_register_ulong(const char *path, unsigned long *data, uint32_t flags);
int sysctl_register_string(const char *path, char *data, size_t maxlen, uint32_t flags);
int sysctl_register_bool(const char *path, bool *data, uint32_t flags);
int sysctl_register_handler(const char *path, sysctl_handler_t handler,
                            void *data, uint32_t flags);
int sysctl_register_table(struct sysctl_table_entry *table);
void sysctl_unregister(const char *path);

/* Node lookup */
struct sysctl_node *sysctl_lookup(const char *path);

/* Read/write operations */
int sysctl_read(const char *path, void *buf, size_t *len);
int sysctl_write(const char *path, const void *buf, size_t len);

/* String formatting */
int sysctl_read_string(const char *path, char *buf, size_t buflen);
int sysctl_write_string(const char *path, const char *buf);

/* Debug */
void sysctl_dump_tree(void);

#endif /* _SYSCTL_SYSCTL_H */
