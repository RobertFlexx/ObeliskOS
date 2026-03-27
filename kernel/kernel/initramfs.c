/*
 * Obelisk OS - Minimal TAR Initramfs Importer
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <obelisk/initramfs.h>
#include <obelisk/zig_initramfs.h>
#include <obelisk/zig_bytes.h>
#include <fs/vfs.h>
#include <fs/inode.h>
#include <fs/file.h>

#ifndef O_WRONLY
#define O_WRONLY 0x0001
#endif

#ifndef O_TRUNC
#define O_TRUNC 0x0200
#endif

typedef off_t loff_t;

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} __packed;

static void normalize_tar_path(char *path) {
    char normalized[256];
    size_t r = 0;
    size_t w = 0;

    if (!path || !path[0]) {
        return;
    }

    while (path[r] == '/') {
        r++;
    }

    normalized[w++] = '/';

    while (path[r] && w < sizeof(normalized) - 1) {
        if (path[r] == '/' && path[r + 1] == '/') {
            r++;
            continue;
        }
        if (path[r] == '.' && (path[r + 1] == '/' || path[r + 1] == '\0')) {
            r++;
            if (path[r] == '/') {
                r++;
            }
            continue;
        }
        if (path[r] == '/' && path[r + 1] == '.' &&
            (path[r + 2] == '/' || path[r + 2] == '\0')) {
            r += 2;
            if (path[r] == '/') {
                r++;
            }
            continue;
        }

        normalized[w++] = path[r++];
    }

    if (w > 1 && normalized[w - 1] == '/') {
        w--;
    }

    normalized[w] = '\0';
    strncpy(path, normalized, 255);
    path[255] = '\0';
}

static bool is_zero_block(const uint8_t *block) {
    return zig_mem_all_zero(block, 512) != 0;
}

static uint64_t parse_octal(const char *s, size_t n) {
    uint64_t v = 0;
    size_t i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\0')) {
        i++;
    }
    while (i < n && s[i] >= '0' && s[i] <= '7') {
        v = (v << 3) + (uint64_t)(s[i] - '0');
        i++;
    }
    return v;
}

static int mkdir_p(const char *path) {
    char tmp[256];
    size_t len = strlen(path);

    if (len == 0 || len >= sizeof(tmp)) {
        return -EINVAL;
    }

    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (tmp[0]) {
                int r = vfs_mkdir(tmp, 0755);
                if (r < 0 && r != -EEXIST) {
                    return r;
                }
            }
            tmp[i] = '/';
        }
    }

    {
        int r = vfs_mkdir(tmp, 0755);
        if (r < 0 && r != -EEXIST) {
            return r;
        }
    }

    return 0;
}

static int ensure_parent_dir(const char *path) {
    char parent[256];
    char *slash;

    if (strlen(path) >= sizeof(parent)) {
        return -ENAMETOOLONG;
    }

    strcpy(parent, path);
    slash = strrchr(parent, '/');
    if (!slash || slash == parent) {
        return 0;
    }
    *slash = '\0';
    return mkdir_p(parent);
}

static void apply_inode_metadata(const char *path, mode_t mode, uid_t uid, gid_t gid) {
    struct dentry *d = vfs_lookup(path);
    if (!d || !d->d_inode) {
        if (d) {
            dput(d);
        }
        return;
    }
    d->d_inode->i_mode = (d->d_inode->i_mode & S_IFMT) | (mode & 07777);
    d->d_inode->i_uid = uid;
    d->d_inode->i_gid = gid;
    mark_inode_dirty(d->d_inode);
    dput(d);
}

static int import_regular_file(const char *path, const void *data, size_t size,
                               mode_t mode, uid_t uid, gid_t gid) {
    struct file *f;
    loff_t pos = 0;
    int r;

    r = ensure_parent_dir(path);
    if (r < 0) {
        return r;
    }

    if ((mode & 07777) == 0) {
        mode = 0644;
    }

    r = vfs_create(path, mode);
    if (r < 0 && r != -EEXIST) {
        return r;
    }

    f = vfs_open(path, O_WRONLY | O_TRUNC, 0);
    if (IS_ERR(f)) {
        return PTR_ERR(f);
    }

    if (size > 0) {
        ssize_t written = vfs_write(f, data, size, &pos);
        if (written < 0 || (size_t)written != size) {
            vfs_close(f);
            return written < 0 ? (int)written : -EIO;
        }
    }

    r = vfs_close(f);
    if (r < 0) {
        return r;
    }

    apply_inode_metadata(path, mode, uid, gid);
    return 0;
}

static void normalize_runtime_ids(const char *path, uid_t *uid, gid_t *gid) {
    if (!path || !uid || !gid) {
        return;
    }
    if (strncmp(path, "/home/obelisk", 13) == 0) {
        *uid = 1000;
        *gid = 1000;
        return;
    }
    *uid = 0;
    *gid = 0;
}

int initramfs_unpack_tar(const void *archive, size_t archive_size) {
    if (zig_initramfs_scan(archive, (uint64_t)archive_size) != 0) {
        printk(KERN_ERR
               "initramfs: archive failed structural validation (truncated or malformed)\n");
        return -EINVAL;
    }

    const uint8_t *p = (const uint8_t *)archive;
    const uint8_t *end = p + archive_size;
    size_t imported = 0;
    size_t failed = 0;

    while (p + 512 <= end) {
        const struct tar_header *h = (const struct tar_header *)p;
        uint64_t fsize;
        mode_t fmode;
        uid_t fuid;
        gid_t fgid;
        size_t payload_bytes;
        size_t advance;
        char path[256];
        int ret;

        if (is_zero_block(p)) {
            break;
        }

        if (h->name[0] == '\0') {
            break;
        }

        if (h->prefix[0]) {
            snprintf(path, sizeof(path), "%s/%s", h->prefix, h->name);
        } else {
            strncpy(path, h->name, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
        }

        if (path[0] != '/') {
            char absolute[256];
            snprintf(absolute, sizeof(absolute), "/%s", path);
            strncpy(path, absolute, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
        }
        normalize_tar_path(path);
        if (zig_path_has_dotdot_component(path, sizeof(path)) != 0) {
            printk(KERN_WARNING "initramfs: skipping unsafe path with '..': %s\n", path);
            failed++;
            p += 512; /* move to next header baseline; final advance below requires parsed size */
            /* Re-derive size and full advance conservatively from header to stay aligned. */
            fsize = parse_octal(h->size, sizeof(h->size));
            payload_bytes = (size_t)fsize;
            advance = 512 + ALIGN_UP(payload_bytes, 512);
            if (p + (advance - 512) > end) {
                return -EINVAL;
            }
            p += (advance - 512);
            continue;
        }

        fsize = parse_octal(h->size, sizeof(h->size));
        fmode = (mode_t)(parse_octal(h->mode, sizeof(h->mode)) & 07777);
        fuid = (uid_t)parse_octal(h->uid, sizeof(h->uid));
        fgid = (gid_t)parse_octal(h->gid, sizeof(h->gid));
        normalize_runtime_ids(path, &fuid, &fgid);
        payload_bytes = (size_t)fsize;
        advance = 512 + ALIGN_UP(payload_bytes, 512);

        if (p + advance > end) {
            printk(KERN_ERR "initramfs: truncated archive entry: %s\n", path);
            return -EINVAL;
        }

        if (h->typeflag == '5') {
            ret = mkdir_p(path);
            if (ret < 0) {
                printk(KERN_WARNING "initramfs: mkdir failed for %s: %d\n", path, ret);
            } else {
                if ((fmode & 07777) == 0) {
                    fmode = 0755;
                }
                apply_inode_metadata(path, fmode, fuid, fgid);
            }
        } else if (h->typeflag == '0' || h->typeflag == '\0') {
            ret = import_regular_file(path, p + 512, payload_bytes, fmode, fuid, fgid);
            if (ret < 0) {
                printk(KERN_WARNING "initramfs: import failed for %s: %d\n", path, ret);
                failed++;
                p += advance;
                continue;
            }
            imported++;
        } else if (h->typeflag == '2') {
            /* Symlink entry: best-effort fallback for bring-up.
             * If target starts with '/', try copying target contents into a
             * regular file at link path so basic userland aliases still work.
             */
            if (h->linkname[0] == '/') {
                struct file *src = vfs_open(h->linkname, O_RDONLY, 0);
                if (!IS_ERR(src)) {
                    char buf[512];
                    loff_t src_pos = 0;
                    int create_ret = import_regular_file(path, NULL, 0, fmode, fuid, fgid);
                    if (create_ret == 0) {
                        struct file *dst = vfs_open(path, O_WRONLY | O_TRUNC, 0);
                        if (!IS_ERR(dst)) {
                            loff_t dst_pos = 0;
                            while (1) {
                                ssize_t rd = vfs_read(src, buf, sizeof(buf), &src_pos);
                                if (rd < 0) {
                                    break;
                                }
                                if (rd == 0) {
                                    imported++;
                                    break;
                                }
                                if (vfs_write(dst, buf, (size_t)rd, &dst_pos) != rd) {
                                    break;
                                }
                            }
                            vfs_close(dst);
                        }
                    }
                    vfs_close(src);
                }
            } else {
                printk(KERN_DEBUG "initramfs: skipping relative symlink %s -> %s\n",
                       path, h->linkname);
            }
        } else {
            printk(KERN_DEBUG "initramfs: skipping unsupported type '%c' for %s\n",
                   h->typeflag, path);
        }

        p += advance;
    }

    printk(KERN_INFO "initramfs: imported %lu file(s), failed %lu\n", imported, failed);
    return 0;
}
