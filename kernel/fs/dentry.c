/*
 * Obelisk OS - Directory Entry Operations
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <fs/vfs.h>
#include <mm/kmalloc.h>

/* Dentry cache */
extern struct kmem_cache *dentry_cache;

/* ==========================================================================
 * Dentry Allocation
 * ========================================================================== */

struct dentry *dentry_alloc(struct dentry *parent, const char *name) {
    struct dentry *dentry;
    
    dentry = kmem_cache_zalloc(dentry_cache);
    if (!dentry) {
        return NULL;
    }
    
    strncpy(dentry->d_name, name, NAME_MAX);
    dentry->d_name[NAME_MAX] = '\0';
    
    dentry->d_inode = NULL;
    dentry->d_parent = parent ? dget(parent) : dentry;
    dentry->d_sb = parent ? parent->d_sb : NULL;
    dentry->d_op = NULL;
    
    INIT_LIST_HEAD(&dentry->d_child);
    INIT_LIST_HEAD(&dentry->d_subdirs);
    INIT_LIST_HEAD(&dentry->d_alias);
    
    dentry->d_count.counter = 1;
    dentry->d_flags = 0;
    dentry->d_lock = (spinlock_t)SPINLOCK_INIT;
    
    /* Add to parent's subdirs */
    if (parent && parent != dentry) {
        list_add(&dentry->d_child, &parent->d_subdirs);
    }
    
    return dentry;
}

void dentry_free(struct dentry *dentry) {
    if (!dentry) return;
    
    /* Remove from parent's subdirs */
    list_del(&dentry->d_child);
    list_del(&dentry->d_alias);
    
    /* Release parent */
    if (dentry->d_parent && dentry->d_parent != dentry) {
        dput(dentry->d_parent);
    }
    
    /* Release inode */
    if (dentry->d_inode) {
        iput(dentry->d_inode);
    }
    
    kmem_cache_free(dentry_cache, dentry);
}

/* ==========================================================================
 * Reference Counting
 * ========================================================================== */

struct dentry *dget(struct dentry *dentry) {
    if (dentry) {
        dentry->d_count.counter++;
    }
    return dentry;
}

void dput(struct dentry *dentry) {
    if (!dentry) return;
    
    if (--dentry->d_count.counter == 0) {
        dentry_free(dentry);
    }
}

/* ==========================================================================
 * Dentry Operations
 * ========================================================================== */

void d_instantiate(struct dentry *dentry, struct inode *inode) {
    if (!list_empty(&dentry->d_alias)) {
        list_del(&dentry->d_alias);
        INIT_LIST_HEAD(&dentry->d_alias);
    }

    dentry->d_inode = inode;
    
    if (inode) {
        list_add(&dentry->d_alias, &inode->i_dentry);
    }
}

void d_add(struct dentry *dentry, struct inode *inode) {
    d_instantiate(dentry, inode);
}

struct dentry *d_make_root(struct inode *root_inode) {
    struct dentry *dentry;
    
    if (!root_inode) {
        return NULL;
    }
    
    dentry = dentry_alloc(NULL, "/");
    if (!dentry) {
        iput(root_inode);
        return NULL;
    }
    
    dentry->d_sb = root_inode->i_sb;
    d_instantiate(dentry, root_inode);
    
    return dentry;
}

void d_delete(struct dentry *dentry) {
    if (dentry && dentry->d_inode) {
        if (!list_empty(&dentry->d_alias)) {
            list_del(&dentry->d_alias);
            INIT_LIST_HEAD(&dentry->d_alias);
        }
        dentry->d_inode = NULL;
    }
}

/* Find a child dentry by name */
struct dentry *d_lookup(struct dentry *parent, const char *name) {
    struct dentry *dentry;
    
    list_for_each_entry(dentry, &parent->d_subdirs, d_child) {
        if (strcmp(dentry->d_name, name) == 0) {
            return dget(dentry);
        }
    }
    
    return NULL;
}

/* Get the full path of a dentry */
char *d_path(const struct dentry *dentry, char *buf, int buflen) {
    char *p = buf + buflen - 1;
    *p = '\0';
    
    while (dentry && dentry->d_parent != dentry) {
        int len = strlen(dentry->d_name);
        p -= len;
        if (p < buf) {
            return NULL;
        }
        memcpy(p, dentry->d_name, len);
        
        if (dentry->d_parent && dentry->d_parent != dentry) {
            if (--p < buf) {
                return NULL;
            }
            *p = '/';
        }
        
        dentry = dentry->d_parent;
    }
    
    if (p == buf + buflen - 1) {
        if (--p < buf) {
            return NULL;
        }
        *p = '/';
    }
    
    return p;
}