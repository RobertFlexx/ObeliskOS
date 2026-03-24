/*
 * Obelisk OS - AxiomFS File Operations
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <proc/process.h>
#include <fs/vfs.h>
#include <fs/inode.h>
#include <axiomfs/axiomfs.h>
#include <axiomfs/policy.h>
#include <mm/kmalloc.h>

/* Simple in-memory file data storage */
struct axiomfs_file_data {
    void *data;
    size_t size;
    size_t capacity;
};

/* Get or create file data */
static struct axiomfs_file_data *get_file_data(struct inode *inode) {
    struct axiomfs_inode_info *info = inode->i_private;
    
    if (!info) {
        return NULL;
    }
    
    struct axiomfs_inode *ai = info->ai_inode;
    if (!ai) {
        return NULL;
    }
    
    /* Use direct[0] as pointer to file data (hack for in-memory FS) */
    struct axiomfs_file_data *fdata = (void *)ai->i_direct[0];
    
    if (!fdata) {
        fdata = kzalloc(sizeof(struct axiomfs_file_data));
        if (!fdata) {
            return NULL;
        }
        ai->i_direct[0] = (uint64_t)fdata;
    }
    
    return fdata;
}

/* ==========================================================================
 * File Read/Write
 * ========================================================================== */

ssize_t axiomfs_read(struct file *file, char *buf, size_t count, loff_t *pos) {
    struct inode *inode = file->f_dentry->d_inode;
    struct axiomfs_file_data *fdata;
    ssize_t ret;
    
    if (!inode) {
        return -EBADF;
    }
    
    /* Check policy */
    if (policy_daemon_available()) {
        int result = policy_check_access(
            current ? current->cred->fsuid : 0,
            current ? current->cred->fsgid : 0,
            inode->i_ino,
            file->f_dentry->d_name,
            POLICY_OP_READ
        );
        
        if (result == POLICY_DENY) {
            return -EACCES;
        }
    }
    
    fdata = get_file_data(inode);
    if (!fdata || !fdata->data) {
        return 0;  /* Empty file */
    }
    
    /* Check bounds */
    if (*pos >= (loff_t)fdata->size) {
        return 0;  /* EOF */
    }
    
    if (*pos + count > fdata->size) {
        count = fdata->size - *pos;
    }
    
    /* Copy data */
    memcpy(buf, (char *)fdata->data + *pos, count);
    *pos += count;
    ret = count;
    
    /* Update access time */
    inode->i_atime = get_ticks();
    
    return ret;
}

ssize_t axiomfs_write(struct file *file, const char *buf, size_t count, loff_t *pos) {
    struct inode *inode = file->f_dentry->d_inode;
    struct axiomfs_file_data *fdata;
    
    if (!inode) {
        return -EBADF;
    }
    
    /* Check policy */
    if (policy_daemon_available()) {
        int result = policy_check_access(
            current ? current->cred->fsuid : 0,
            current ? current->cred->fsgid : 0,
            inode->i_ino,
            file->f_dentry->d_name,
            POLICY_OP_WRITE
        );
        
        if (result == POLICY_DENY) {
            return -EACCES;
        }
    }
    
    fdata = get_file_data(inode);
    if (!fdata) {
        return -ENOMEM;
    }
    
    /* Handle append mode */
    if (file->f_flags & O_APPEND) {
        *pos = fdata->size;
    }
    
    /* Expand if necessary */
    size_t new_size = *pos + count;
    if (new_size > fdata->capacity) {
        size_t new_cap = MAX(new_size, fdata->capacity * 2);
        new_cap = MAX(new_cap, 4096);
        
        void *new_data = kmalloc(new_cap);
        if (!new_data) {
            return -ENOMEM;
        }
        
        if (fdata->data) {
            memcpy(new_data, fdata->data, fdata->size);
            kfree(fdata->data);
        }
        
        fdata->data = new_data;
        fdata->capacity = new_cap;
    }
    
    /* Zero fill gap if writing past end */
    if (*pos > (loff_t)fdata->size) {
        memset((char *)fdata->data + fdata->size, 0, *pos - fdata->size);
    }
    
    /* Write data */
    memcpy((char *)fdata->data + *pos, buf, count);
    *pos += count;
    
    if (*pos > (loff_t)fdata->size) {
        fdata->size = *pos;
        inode->i_size = fdata->size;
    }
    
    /* Update times */
    inode->i_mtime = inode->i_ctime = get_ticks();
    mark_inode_dirty(inode);
    
    return count;
}

/* ==========================================================================
 * Policy Helpers
 * ========================================================================== */

int axiomfs_policy_check_access(struct inode *inode, int mask) {
    if (!policy_daemon_available()) {
        /* Fall back to traditional permission check */
        return generic_permission(inode, mask);
    }
    
    uint32_t op = 0;
    if (mask & MAY_READ) op |= POLICY_OP_READ;
    if (mask & MAY_WRITE) op |= POLICY_OP_WRITE;
    if (mask & MAY_EXEC) op |= POLICY_OP_EXECUTE;
    
    int result = policy_check_access(
        current ? current->cred->fsuid : 0,
        current ? current->cred->fsgid : 0,
        inode->i_ino,
        "",  /* Path not available here */
        op
    );
    
    switch (result) {
        case POLICY_ALLOW:
            return 0;
        case POLICY_DENY:
            return -EACCES;
        case POLICY_DEFAULT:
        default:
            return generic_permission(inode, mask);
    }
}

int axiomfs_policy_inherit_permissions(struct inode *parent, struct inode *child) {
    if (!policy_daemon_available()) {
        return POLICY_DEFAULT;
    }
    
    /* Query inheritance policy from daemon */
    /* For now, just allow */
    (void)parent;
    (void)child;
    
    return POLICY_ALLOW;
}