/*
 * Obelisk OS - AxiomFS Directory Operations
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <proc/process.h>
#include <fs/vfs.h>
#include <fs/inode.h>
#include <axiomfs/axiomfs.h>
#include <axiomfs/policy.h>

/* ==========================================================================
 * Directory Iteration
 * ========================================================================== */

int axiomfs_readdir(struct file *file, struct dir_context *ctx) {
    struct dentry *dentry = file->f_dentry;
    struct inode *inode = dentry->d_inode;
    struct dentry *child;
    int pos = 0;
    
    if (!inode || !S_ISDIR(inode->i_mode)) {
        return -ENOTDIR;
    }
    
    /* Check policy */
    if (policy_daemon_available()) {
        int result = policy_check_access(
            current ? current->cred->fsuid : 0,
            current ? current->cred->fsgid : 0,
            inode->i_ino,
            dentry->d_name,
            POLICY_OP_READ
        );
        
        if (result == POLICY_DENY) {
            return -EACCES;
        }
    }
    
    /* Emit . and .. */
    if (ctx->pos == 0) {
        if (ctx->actor(ctx, ".", 1, ctx->pos, inode->i_ino, DT_DIR)) {
            return 0;
        }
        ctx->pos++;
    }
    
    if (ctx->pos == 1) {
        ino_t parent_ino = inode->i_ino;
        if (dentry->d_parent && dentry->d_parent->d_inode) {
            parent_ino = dentry->d_parent->d_inode->i_ino;
        }
        if (ctx->actor(ctx, "..", 2, ctx->pos, parent_ino, DT_DIR)) {
            return 0;
        }
        ctx->pos++;
    }
    
    /* Emit directory entries */
    pos = 2;
    list_for_each_entry(child, &dentry->d_subdirs, d_child) {
        /* Skip negative/unlinked dentries. We don't use negative dentries for
         * readdir output, and unlink/rmdir removes entries from the parent list. */
        if (!child->d_inode) {
            pos++;
            continue;
        }
        if (pos >= ctx->pos) {
            unsigned char type = DT_UNKNOWN;
            
            if (S_ISDIR(child->d_inode->i_mode)) {
                type = DT_DIR;
            } else if (S_ISREG(child->d_inode->i_mode)) {
                type = DT_REG;
            } else if (S_ISLNK(child->d_inode->i_mode)) {
                type = DT_LNK;
            }
            
            if (ctx->actor(ctx, child->d_name, strlen(child->d_name),
                          pos, child->d_inode->i_ino, type)) {
                return 0;
            }
            ctx->pos = pos + 1;
        }
        pos++;
    }
    
    return 0;
}