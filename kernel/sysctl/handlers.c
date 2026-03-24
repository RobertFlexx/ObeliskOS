/*
 * Obelisk OS - sysctl Handlers
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <sysctl/sysctl.h>

/* ==========================================================================
 * Generic Handlers
 * ========================================================================== */

int sysctl_handle_int(struct sysctl_node *node, void *buf,
                      size_t *len, loff_t *ppos, bool write) {
    int *data = (int *)node->data;
    (void)ppos;
    
    if (!data) {
        return -EINVAL;
    }
    
    if (write) {
        if (node->flags & SYSCTL_RO) {
            return -EPERM;
        }
        
        if (*len != sizeof(int)) {
            return -EINVAL;
        }
        
        *data = *(int *)buf;
        return 0;
    }
    
    if (*len < sizeof(int)) {
        return -EINVAL;
    }
    
    *(int *)buf = *data;
    *len = sizeof(int);
    
    return 0;
}

int sysctl_handle_uint(struct sysctl_node *node, void *buf,
                       size_t *len, loff_t *ppos, bool write) {
    unsigned int *data = (unsigned int *)node->data;
    (void)ppos;
    
    if (!data) {
        return -EINVAL;
    }
    
    if (write) {
        if (node->flags & SYSCTL_RO) {
            return -EPERM;
        }
        
        if (*len != sizeof(unsigned int)) {
            return -EINVAL;
        }
        
        *data = *(unsigned int *)buf;
        return 0;
    }
    
    if (*len < sizeof(unsigned int)) {
        return -EINVAL;
    }
    
    *(unsigned int *)buf = *data;
    *len = sizeof(unsigned int);
    
    return 0;
}

int sysctl_handle_long(struct sysctl_node *node, void *buf,
                       size_t *len, loff_t *ppos, bool write) {
    long *data = (long *)node->data;
    (void)ppos;
    
    if (!data) {
        return -EINVAL;
    }
    
    if (write) {
        if (node->flags & SYSCTL_RO) {
            return -EPERM;
        }
        
        if (*len != sizeof(long)) {
            return -EINVAL;
        }
        
        *data = *(long *)buf;
        return 0;
    }
    
    if (*len < sizeof(long)) {
        return -EINVAL;
    }
    
    *(long *)buf = *data;
    *len = sizeof(long);
    
    return 0;
}

int sysctl_handle_ulong(struct sysctl_node *node, void *buf,
                        size_t *len, loff_t *ppos, bool write) {
    unsigned long *data = (unsigned long *)node->data;
    (void)ppos;
    
    if (!data) {
        return -EINVAL;
    }
    
    if (write) {
        if (node->flags & SYSCTL_RO) {
            return -EPERM;
        }
        
        if (*len != sizeof(unsigned long)) {
            return -EINVAL;
        }
        
        *data = *(unsigned long *)buf;
        return 0;
    }
    
    if (*len < sizeof(unsigned long)) {
        return -EINVAL;
    }
    
    *(unsigned long *)buf = *data;
    *len = sizeof(unsigned long);
    
    return 0;
}

int sysctl_handle_string(struct sysctl_node *node, void *buf,
                         size_t *len, loff_t *ppos, bool write) {
    char *data = (char *)node->data;
    (void)ppos;
    
    if (!data) {
        return -EINVAL;
    }
    
    if (write) {
        if (node->flags & SYSCTL_RO) {
            return -EPERM;
        }
        
        size_t copylen = MIN(*len, node->maxlen - 1);
        memcpy(data, buf, copylen);
        data[copylen] = '\0';
        
        return 0;
    }
    
    size_t datalen = strlen(data) + 1;
    if (*len < datalen) {
        datalen = *len;
    }
    
    memcpy(buf, data, datalen);
    *len = datalen;
    
    return 0;
}

int sysctl_handle_bool(struct sysctl_node *node, void *buf,
                       size_t *len, loff_t *ppos, bool write) {
    bool *data = (bool *)node->data;
    (void)ppos;
    
    if (!data) {
        return -EINVAL;
    }
    
    if (write) {
        if (node->flags & SYSCTL_RO) {
            return -EPERM;
        }
        
        if (*len == sizeof(bool)) {
            *data = *(bool *)buf;
        } else if (*len == sizeof(int)) {
            *data = (*(int *)buf) != 0;
        } else {
            return -EINVAL;
        }
        
        return 0;
    }
    
    if (*len < sizeof(bool)) {
        return -EINVAL;
    }
    
    *(bool *)buf = *data;
    *len = sizeof(bool);
    
    return 0;
}

/* ==========================================================================
 * Range-Checked Handlers
 * ========================================================================== */

struct sysctl_int_range {
    int min;
    int max;
    int *value;
};

int sysctl_handle_int_minmax(struct sysctl_node *node, void *buf,
                             size_t *len, loff_t *ppos, bool write) {
    struct sysctl_int_range *range = (struct sysctl_int_range *)node->data;
    (void)ppos;
    
    if (!range || !range->value) {
        return -EINVAL;
    }
    
    if (write) {
        if (node->flags & SYSCTL_RO) {
            return -EPERM;
        }
        
        if (*len != sizeof(int)) {
            return -EINVAL;
        }
        
        int val = *(int *)buf;
        
        if (val < range->min || val > range->max) {
            return -ERANGE;
        }
        
        *range->value = val;
        return 0;
    }
    
    if (*len < sizeof(int)) {
        return -EINVAL;
    }
    
    *(int *)buf = *range->value;
    *len = sizeof(int);
    
    return 0;
}