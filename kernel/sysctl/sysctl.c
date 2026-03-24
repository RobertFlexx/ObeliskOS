/*
 * Obelisk OS - sysctl Implementation
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <sysctl/sysctl.h>
#include <mm/kmalloc.h>

/* Root node */
struct sysctl_node *sysctl_root = NULL;

/* Node cache */
static struct kmem_cache *sysctl_cache = NULL;

/* ==========================================================================
 * Node Management
 * ========================================================================== */

static struct sysctl_node *sysctl_alloc_node(const char *name) {
    struct sysctl_node *node;
    
    if (sysctl_cache) {
        node = kmem_cache_zalloc(sysctl_cache);
    } else {
        node = kzalloc(sizeof(struct sysctl_node));
    }
    
    if (!node) return NULL;
    
    strncpy(node->name, name, SYSCTL_NAME_MAX - 1);
    node->lock = (spinlock_t)SPINLOCK_INIT;
    
    return node;
}

/* Find or create path to node */
struct sysctl_node *sysctl_register_node(const char *path) {
    struct sysctl_node *node = sysctl_root;
    char *pathcopy, *component;
    
    if (!path || path[0] == '\0') {
        return NULL;
    }
    
    pathcopy = strdup(path);
    if (!pathcopy) return NULL;
    
    component = strtok(pathcopy, ".");
    
    /* If root exists and first component matches root name, skip it. */
    if (component && node && strcmp(node->name, component) == 0) {
        component = strtok(NULL, ".");
        if (!component) {
            kfree(pathcopy);
            return node;
        }
    }

    while (component) {
        struct sysctl_node *child = NULL;
        
        /* Search for existing child */
        if (node) {
            for (child = node->children; child; child = child->next) {
                if (strcmp(child->name, component) == 0) {
                    break;
                }
            }
        }
        
        if (!child) {
            /* Create new node */
            child = sysctl_alloc_node(component);
            if (!child) {
                kfree(pathcopy);
                return NULL;
            }
            
            child->type = SYSCTL_TYPE_NODE;
            child->parent = node;
            
            /* Add to parent's children */
            if (node) {
                child->next = node->children;
                node->children = child;
            } else {
                /* This is the new root */
                sysctl_root = child;
            }
        }
        
        node = child;
        component = strtok(NULL, ".");
    }
    
    kfree(pathcopy);
    return node;
}

struct sysctl_node *sysctl_lookup(const char *path) {
    struct sysctl_node *node = sysctl_root;
    char *pathcopy, *component;
    
    if (!path || !sysctl_root) {
        return NULL;
    }
    
    pathcopy = strdup(path);
    if (!pathcopy) return NULL;
    
    component = strtok(pathcopy, ".");
    
    /* Skip root name if it matches. */
    if (component && strcmp(node->name, component) == 0) {
        component = strtok(NULL, ".");
        if (!component) {
            kfree(pathcopy);
            return node;
        }
    }

    while (component && node) {
        struct sysctl_node *child = NULL;
        
        for (child = node->children; child; child = child->next) {
            if (strcmp(child->name, component) == 0) {
                break;
            }
        }
        
        if (!child) {
            kfree(pathcopy);
            return NULL;
        }

        node = child;
        component = strtok(NULL, ".");
    }
    
    kfree(pathcopy);
    return node;
}

/* ==========================================================================
 * Registration Functions
 * ========================================================================== */

int sysctl_register_int(const char *path, int *data, uint32_t flags) {
    struct sysctl_node *node = sysctl_register_node(path);
    if (!node) return -ENOMEM;
    
    node->type = SYSCTL_TYPE_INT;
    node->flags = flags;
    node->data = data;
    node->maxlen = sizeof(int);
    node->mode = (flags & SYSCTL_RO) ? 0444 : 0644;
    
    return 0;
}

int sysctl_register_uint(const char *path, unsigned int *data, uint32_t flags) {
    struct sysctl_node *node = sysctl_register_node(path);
    if (!node) return -ENOMEM;
    
    node->type = SYSCTL_TYPE_UINT;
    node->flags = flags;
    node->data = data;
    node->maxlen = sizeof(unsigned int);
    node->mode = (flags & SYSCTL_RO) ? 0444 : 0644;
    
    return 0;
}

int sysctl_register_long(const char *path, long *data, uint32_t flags) {
    struct sysctl_node *node = sysctl_register_node(path);
    if (!node) return -ENOMEM;
    
    node->type = SYSCTL_TYPE_LONG;
    node->flags = flags;
    node->data = data;
    node->maxlen = sizeof(long);
    node->mode = (flags & SYSCTL_RO) ? 0444 : 0644;
    
    return 0;
}

int sysctl_register_ulong(const char *path, unsigned long *data, uint32_t flags) {
    struct sysctl_node *node = sysctl_register_node(path);
    if (!node) return -ENOMEM;
    
    node->type = SYSCTL_TYPE_ULONG;
    node->flags = flags;
    node->data = data;
    node->maxlen = sizeof(unsigned long);
    node->mode = (flags & SYSCTL_RO) ? 0444 : 0644;
    
    return 0;
}

int sysctl_register_string(const char *path, char *data, size_t maxlen, uint32_t flags) {
    struct sysctl_node *node = sysctl_register_node(path);
    if (!node) return -ENOMEM;
    
    node->type = SYSCTL_TYPE_STRING;
    node->flags = flags;
    node->data = data;
    node->maxlen = maxlen;
    node->mode = (flags & SYSCTL_RO) ? 0444 : 0644;
    
    return 0;
}

int sysctl_register_bool(const char *path, bool *data, uint32_t flags) {
    struct sysctl_node *node = sysctl_register_node(path);
    if (!node) return -ENOMEM;
    
    node->type = SYSCTL_TYPE_BOOL;
    node->flags = flags;
    node->data = data;
    node->maxlen = sizeof(bool);
    node->mode = (flags & SYSCTL_RO) ? 0444 : 0644;
    
    return 0;
}

int sysctl_register_handler(const char *path, sysctl_handler_t handler,
                            void *data, uint32_t flags) {
    struct sysctl_node *node = sysctl_register_node(path);
    if (!node) return -ENOMEM;
    
    node->type = SYSCTL_TYPE_PROC;
    node->flags = flags;
    node->data = data;
    node->handler = handler;
    node->mode = (flags & SYSCTL_RO) ? 0444 : 0644;
    
    return 0;
}

/* ==========================================================================
 * Read/Write Operations
 * ========================================================================== */

int sysctl_read(const char *path, void *buf, size_t *len) {
    struct sysctl_node *node = sysctl_lookup(path);
    
    if (!node) {
        return -ENOENT;
    }
    
    if (node->type == SYSCTL_TYPE_NODE) {
        return -EISDIR;
    }
    
    if (node->handler) {
        return node->handler(node, buf, len, NULL, false);
    }
    
    if (!node->data) {
        return -EINVAL;
    }
    
    size_t copylen = MIN(*len, node->maxlen);
    memcpy(buf, node->data, copylen);
    *len = copylen;
    
    return 0;
}

int sysctl_write(const char *path, const void *buf, size_t len) {
    struct sysctl_node *node = sysctl_lookup(path);
    
    if (!node) {
        return -ENOENT;
    }
    
    if (node->type == SYSCTL_TYPE_NODE) {
        return -EISDIR;
    }
    
    if (node->flags & SYSCTL_RO) {
        return -EPERM;
    }
    
    if (node->handler) {
        return node->handler(node, (void *)buf, &len, NULL, true);
    }
    
    if (!node->data) {
        return -EINVAL;
    }
    
    size_t copylen = MIN(len, node->maxlen);
    memcpy(node->data, buf, copylen);
    
    return 0;
}

static void fmt_ulong(char *buf, size_t cap, unsigned long v) {
    char tmp[24];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v > 0 && n < (int)sizeof(tmp)) {
            tmp[n++] = '0' + (char)(v % 10);
            v /= 10;
        }
    }
    size_t w = 0;
    while (n > 0 && w + 1 < cap) {
        buf[w++] = tmp[--n];
    }
    buf[w] = '\0';
}

static void fmt_long(char *buf, size_t cap, long v) {
    if (v < 0 && cap > 1) {
        buf[0] = '-';
        fmt_ulong(buf + 1, cap - 1, (unsigned long)(-v));
    } else {
        fmt_ulong(buf, cap, (unsigned long)v);
    }
}

int sysctl_read_string(const char *path, char *buf, size_t buflen) {
    struct sysctl_node *node = sysctl_lookup(path);

    if (!node) {
        return -ENOENT;
    }

    if (node->type == SYSCTL_TYPE_PROC) {
        uint8_t raw[256];
        size_t len = sizeof(raw);
        int ret;
        if (!node->handler) {
            return -EINVAL;
        }
        ret = node->handler(node, raw, &len, NULL, false);
        if (ret < 0) {
            return ret;
        }
        /*
         * Detect C-string: NUL terminator must be at exactly len-1, and
         * every preceding byte must be printable and non-NUL.  This avoids
         * misinterpreting binary integers whose low byte is printable.
         */
        if (len >= 1 && raw[len - 1] == '\0') {
            bool is_cstr = true;
            for (size_t i = 0; i + 1 < len; i++) {
                if (raw[i] == '\0' || raw[i] < 32 || raw[i] > 126) {
                    is_cstr = false;
                    break;
                }
            }
            if (is_cstr) {
                strncpy(buf, (const char *)raw, buflen - 1);
                buf[buflen - 1] = '\0';
                return 0;
            }
        }
        if (len == sizeof(unsigned long)) {
            unsigned long v;
            memcpy(&v, raw, sizeof(v));
            fmt_ulong(buf, buflen, v);
        } else if (len == sizeof(unsigned int) && len != sizeof(unsigned long)) {
            unsigned int v;
            memcpy(&v, raw, sizeof(v));
            fmt_ulong(buf, buflen, (unsigned long)v);
        } else {
            strncpy(buf, "(raw)", buflen - 1);
            buf[buflen - 1] = '\0';
        }
        return 0;
    }

    if (!node->data) {
        return -ENOENT;
    }

    switch (node->type) {
        case SYSCTL_TYPE_INT:
            fmt_long(buf, buflen, (long)(*(int *)node->data));
            break;
        case SYSCTL_TYPE_UINT:
            fmt_ulong(buf, buflen, (unsigned long)(*(unsigned int *)node->data));
            break;
        case SYSCTL_TYPE_LONG:
            fmt_long(buf, buflen, *(long *)node->data);
            break;
        case SYSCTL_TYPE_ULONG:
            fmt_ulong(buf, buflen, *(unsigned long *)node->data);
            break;
        case SYSCTL_TYPE_STRING:
            strncpy(buf, (char *)node->data, buflen - 1);
            buf[buflen - 1] = '\0';
            break;
        case SYSCTL_TYPE_BOOL:
            strncpy(buf, *(bool *)node->data ? "true" : "false", buflen - 1);
            buf[buflen - 1] = '\0';
            break;
        default:
            return -EINVAL;
    }

    return 0;
}

/* ==========================================================================
 * Debug
 * ========================================================================== */

static void sysctl_dump_node(struct sysctl_node *node, int depth) {
    for (int i = 0; i < depth; i++) {
        printk("  ");
    }
    
    printk("%s", node->name);
    
    if (node->type != SYSCTL_TYPE_NODE && node->data) {
        printk(" = ");
        switch (node->type) {
            case SYSCTL_TYPE_INT:
                printk("%d", *(int *)node->data);
                break;
            case SYSCTL_TYPE_UINT:
                printk("%u", *(unsigned int *)node->data);
                break;
            case SYSCTL_TYPE_LONG:
                printk("%ld", *(long *)node->data);
                break;
            case SYSCTL_TYPE_ULONG:
                printk("%lu", *(unsigned long *)node->data);
                break;
            case SYSCTL_TYPE_STRING:
                printk("\"%s\"", (char *)node->data);
                break;
            case SYSCTL_TYPE_BOOL:
                printk("%s", *(bool *)node->data ? "true" : "false");
                break;
            default:
                printk("(unknown)");
        }
    }
    printk("\n");
    
    for (struct sysctl_node *child = node->children; child; child = child->next) {
        sysctl_dump_node(child, depth + 1);
    }
}

void sysctl_dump_tree(void) {
    printk("sysctl tree:\n");
    if (sysctl_root) {
        sysctl_dump_node(sysctl_root, 0);
    }
}

/* ==========================================================================
 * Initialization
 * ========================================================================== */

/* System values */
static char kernel_version[64] = OBELISK_VERSION_STRING;
static char kernel_ostype[64] = "Obelisk";
static unsigned long boot_time = 0;
static int panic_on_oops = 1;

void sysctl_init(void) {
    printk(KERN_INFO "Initializing sysctl...\n");
    
    /* Create cache */
    sysctl_cache = kmem_cache_create("sysctl_node", sizeof(struct sysctl_node),
                                     sizeof(void *), 0, NULL);
    
    /* Create root */
    sysctl_root = sysctl_alloc_node("system");
    sysctl_root->type = SYSCTL_TYPE_NODE;
    
    /* Register kernel sysctls */
    sysctl_register_string("system.kernel.version", kernel_version, 
                          sizeof(kernel_version), SYSCTL_RO);
    sysctl_register_string("system.kernel.ostype", kernel_ostype,
                          sizeof(kernel_ostype), SYSCTL_RO);
    sysctl_register_ulong("system.kernel.boot_time", &boot_time, SYSCTL_RO);
    sysctl_register_int("system.kernel.panic_on_oops", &panic_on_oops, SYSCTL_RW);

    /* Register dynamic/system nodes after base kernel keys are present. */
    extern void sysctl_init_cpu_info(void);
    extern void sysctl_register_system_nodes(void);
    sysctl_init_cpu_info();
    sysctl_register_system_nodes();
    
    printk(KERN_INFO "sysctl initialized\n");
}