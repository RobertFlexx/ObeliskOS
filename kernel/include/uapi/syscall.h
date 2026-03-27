/*
 * Obelisk OS - System Call Definitions
 * From Axioms, Order.
 *
 * This file defines the userspace ABI for system calls.
 */

#ifndef _OBELISK_UAPI_SYSCALL_H
#define _OBELISK_UAPI_SYSCALL_H

/* System call numbers */
#define SYS_READ            0
#define SYS_WRITE           1
#define SYS_OPEN            2
#define SYS_CLOSE           3
#define SYS_STAT            4
#define SYS_FSTAT           5
#define SYS_LSTAT           6
#define SYS_POLL            7
#define SYS_LSEEK           8
#define SYS_MMAP            9
#define SYS_MPROTECT        10
#define SYS_MUNMAP          11
#define SYS_BRK             12
#define SYS_IOCTL           16
#define SYS_WRITEV          20
#define SYS_ACCESS          21
#define SYS_PIPE            22
#define SYS_SELECT          23
#define SYS_SCHED_YIELD     24
#define SYS_MREMAP          25
#define SYS_MSYNC           26
#define SYS_MINCORE         27
#define SYS_MADVISE         28
#define SYS_DUP             32
#define SYS_DUP2            33
#define SYS_PAUSE           34
#define SYS_NANOSLEEP       35
#define SYS_GETITIMER       36
#define SYS_ALARM           37
#define SYS_SETITIMER       38
#define SYS_GETPID          39
#define SYS_SENDFILE        40
#define SYS_SOCKET          41
#define SYS_CONNECT         42
#define SYS_ACCEPT          43
#define SYS_SENDTO          44
#define SYS_RECVFROM        45
#define SYS_SENDMSG         46
#define SYS_RECVMSG         47
#define SYS_SHUTDOWN        48
#define SYS_BIND            49
#define SYS_LISTEN          50
#define SYS_GETSOCKNAME     51
#define SYS_GETPEERNAME     52
#define SYS_SOCKETPAIR      53
#define SYS_SETSOCKOPT      54
#define SYS_GETSOCKOPT      55
#define SYS_CLONE           56
#define SYS_FORK            57
#define SYS_VFORK           58
#define SYS_EXECVE          59
#define SYS_EXIT            60
#define SYS_WAIT4           61
#define SYS_KILL            62
#define SYS_UNAME           63
#define SYS_FCNTL           72
#define SYS_FLOCK           73
#define SYS_FSYNC           74
#define SYS_FDATASYNC       75
#define SYS_TRUNCATE        76
#define SYS_FTRUNCATE       77
#define SYS_GETDENTS        78
#define SYS_GETCWD          79
#define SYS_CHDIR           80
#define SYS_FCHDIR          81
#define SYS_RENAME          82
#define SYS_MKDIR           83
#define SYS_RMDIR           84
#define SYS_CREAT           85
#define SYS_LINK            86
#define SYS_UNLINK          87
#define SYS_SYMLINK         88
#define SYS_READLINK        89
#define SYS_CHMOD           90
#define SYS_FCHMOD          91
#define SYS_CHOWN           92
#define SYS_FCHOWN          93
#define SYS_LCHOWN          94
#define SYS_UMASK           95
#define SYS_GETTIMEOFDAY    96
#define SYS_GETRLIMIT       97
#define SYS_GETRUSAGE       98
#define SYS_SYSINFO         99
#define SYS_TIMES           100
#define SYS_GETUID          102
#define SYS_SYSLOG          103
#define SYS_GETGID          104
#define SYS_SETUID          105
#define SYS_SETGID          106
#define SYS_GETEUID         107
#define SYS_GETEGID         108
#define SYS_SETPGID         109
#define SYS_GETPPID         110
#define SYS_GETPGRP         111
#define SYS_SETSID          112
#define SYS_SETREUID        113
#define SYS_SETREGID        114
#define SYS_GETGROUPS       115
#define SYS_SETGROUPS       116
#define SYS_SETRESUID       117
#define SYS_GETRESUID       118
#define SYS_SETRESGID       119
#define SYS_GETRESGID       120
#define SYS_GETPGID         121
#define SYS_SETFSUID        122
#define SYS_SETFSGID        123
#define SYS_GETSID          124
#define SYS_CAPGET          125
#define SYS_CAPSET          126
#define SYS_RT_SIGPENDING   127
#define SYS_RT_SIGTIMEDWAIT 128
#define SYS_RT_SIGQUEUEINFO 129
#define SYS_RT_SIGSUSPEND   130
#define SYS_SIGALTSTACK     131
#define SYS_UTIME           132
#define SYS_MKNOD           133
#define SYS_USELIB          134
#define SYS_PERSONALITY     135
#define SYS_USTAT           136
#define SYS_STATFS          137
#define SYS_FSTATFS         138
#define SYS_SYSFS           139
#define SYS_GETPRIORITY     140
#define SYS_SETPRIORITY     141
#define SYS_SCHED_SETPARAM  142
#define SYS_SCHED_GETPARAM  143
#define SYS_SCHED_SETSCHEDULER  144
#define SYS_SCHED_GETSCHEDULER  145
#define SYS_SCHED_GET_PRIORITY_MAX  146
#define SYS_SCHED_GET_PRIORITY_MIN  147
#define SYS_SCHED_RR_GET_INTERVAL   148
#define SYS_MLOCK           149
#define SYS_MUNLOCK         150
#define SYS_MLOCKALL        151
#define SYS_MUNLOCKALL      152
#define SYS_VHANGUP         153
#define SYS_MODIFY_LDT      154
#define SYS_PIVOT_ROOT      155
#define SYS_SYSCTL          156
#define SYS_PRCTL           157
#define SYS_ARCH_PRCTL      158
#define SYS_ADJTIMEX        159
#define SYS_SETRLIMIT       160
#define SYS_CHROOT          161
#define SYS_SYNC            162
#define SYS_ACCT            163
#define SYS_SETTIMEOFDAY    164
#define SYS_MOUNT           165
#define SYS_UMOUNT2         166
#define SYS_SWAPON          167
#define SYS_SWAPOFF         168
#define SYS_REBOOT          169
#define SYS_SETHOSTNAME     170
#define SYS_SETDOMAINNAME   171
#define SYS_IOPL            172
#define SYS_IOPERM          173
#define SYS_QUOTACTL        179
#define SYS_GETTID          186
#define SYS_READAHEAD       187
#define SYS_SETXATTR        188
#define SYS_LSETXATTR       189
#define SYS_FSETXATTR       190
#define SYS_GETXATTR        191
#define SYS_LGETXATTR       192
#define SYS_FGETXATTR       193
#define SYS_LISTXATTR       194
#define SYS_LLISTXATTR      195
#define SYS_FLISTXATTR      196
#define SYS_REMOVEXATTR     197
#define SYS_LREMOVEXATTR    198
#define SYS_FREMOVEXATTR    199
#define SYS_TKILL           200
#define SYS_TIME            201
#define SYS_FUTEX           202
#define SYS_SCHED_SETAFFINITY   203
#define SYS_SCHED_GETAFFINITY   204
#define SYS_GETDENTS64      217
#define SYS_SET_TID_ADDRESS 218
#define SYS_FADVISE64       221
#define SYS_TIMER_CREATE    222
#define SYS_TIMER_SETTIME   223
#define SYS_TIMER_GETTIME   224
#define SYS_TIMER_GETOVERRUN    225
#define SYS_TIMER_DELETE    226
#define SYS_CLOCK_SETTIME   227
#define SYS_CLOCK_GETTIME   228
#define SYS_CLOCK_GETRES    229
#define SYS_CLOCK_NANOSLEEP 230
#define SYS_EXIT_GROUP      231
#define SYS_EPOLL_WAIT      232
#define SYS_EPOLL_CTL       233
#define SYS_TGKILL          234
#define SYS_UTIMES          235
#define SYS_MBIND           237
#define SYS_SET_MEMPOLICY   238
#define SYS_GET_MEMPOLICY   239
#define SYS_MQ_OPEN         240
#define SYS_MQ_UNLINK       241
#define SYS_MQ_TIMEDSEND    242
#define SYS_MQ_TIMEDRECEIVE 243
#define SYS_MQ_NOTIFY       244
#define SYS_MQ_GETSETATTR   245
#define SYS_WAITID          247
#define SYS_IOPRIO_SET      251
#define SYS_IOPRIO_GET      252
#define SYS_INOTIFY_INIT    253
#define SYS_INOTIFY_ADD_WATCH   254
#define SYS_INOTIFY_RM_WATCH    255
#define SYS_MIGRATE_PAGES   256
#define SYS_OPENAT          257
#define SYS_MKDIRAT         258
#define SYS_MKNODAT         259
#define SYS_FCHOWNAT        260
#define SYS_FUTIMESAT       261
#define SYS_NEWFSTATAT      262
#define SYS_UNLINKAT        263
#define SYS_RENAMEAT        264
#define SYS_LINKAT          265
#define SYS_SYMLINKAT       266
#define SYS_READLINKAT      267
#define SYS_FCHMODAT        268
#define SYS_FACCESSAT       269
#define SYS_PSELECT6        270
#define SYS_PPOLL           271
#define SYS_SPLICE          275
#define SYS_TEE             276
#define SYS_SYNC_FILE_RANGE 277
#define SYS_VMSPLICE        278
#define SYS_MOVE_PAGES      279
#define SYS_UTIMENSAT       280
#define SYS_EPOLL_PWAIT     281
#define SYS_SIGNALFD        282
#define SYS_TIMERFD_CREATE  283
#define SYS_EVENTFD         284
#define SYS_FALLOCATE       285
#define SYS_TIMERFD_SETTIME 286
#define SYS_TIMERFD_GETTIME 287
#define SYS_ACCEPT4         288
#define SYS_SIGNALFD4       289
#define SYS_EVENTFD2        290
#define SYS_EPOLL_CREATE1   291
#define SYS_DUP3            292
#define SYS_PIPE2           293
#define SYS_INOTIFY_INIT1   294
#define SYS_PREADV          295
#define SYS_PWRITEV         296

/* Obelisk-specific system calls (start at 400) */
#define SYS_OBELISK_SYSCTL      400     /* sysctl interface */
#define SYS_OBELISK_POLICY      401     /* Policy query (internal) */
#define SYS_OBELISK_AXIOM_QUERY 402     /* Direct axiom query (privileged) */
#define SYS_OBELISK_CACHE_FLUSH 403     /* Flush policy cache */
#define SYS_OBELISK_PROC_LIST   404     /* Process list snapshot (text, see sysctl system.proc.list) */

/* Maximum system call number */
#define NR_SYSCALLS         405

/* System call register convention (x86_64):
 *
 * syscall number: rax
 * arguments:      rdi, rsi, rdx, r10, r8, r9
 * return value:   rax
 *
 * Registers preserved: rbx, rbp, r12-r15
 * Registers clobbered: rcx, r11 (used by syscall instruction)
 */

/* Open flags */
#define O_RDONLY        0x0000
#define O_WRONLY        0x0001
#define O_RDWR          0x0002
#define O_ACCMODE       0x0003
#define O_CREAT         0x0040
#define O_EXCL          0x0080
#define O_NOCTTY        0x0100
#define O_TRUNC         0x0200
#define O_APPEND        0x0400
#define O_NONBLOCK      0x0800
#define O_DSYNC         0x1000
#define O_ASYNC         0x2000
#define O_DIRECT        0x4000
#define O_LARGEFILE     0x8000
#define O_DIRECTORY     0x10000
#define O_NOFOLLOW      0x20000
#define O_NOATIME       0x40000
#define O_CLOEXEC       0x80000
#define O_SYNC          0x101000
#define O_PATH          0x200000
#define O_TMPFILE       0x400000

/* File modes */
#define S_IFMT      0170000     /* File type mask */
#define S_IFSOCK    0140000     /* Socket */
#define S_IFLNK     0120000     /* Symbolic link */
#define S_IFREG     0100000     /* Regular file */
#define S_IFBLK     0060000     /* Block device */
#define S_IFDIR     0040000     /* Directory */
#define S_IFCHR     0020000     /* Character device */
#define S_IFIFO     0010000     /* FIFO */

#define S_ISUID     0004000     /* Set UID bit */
#define S_ISGID     0002000     /* Set GID bit */
#define S_ISVTX     0001000     /* Sticky bit */

#define S_IRWXU     0000700     /* User permissions */
#define S_IRUSR     0000400     /* User read */
#define S_IWUSR     0000200     /* User write */
#define S_IXUSR     0000100     /* User execute */

#define S_IRWXG     0000070     /* Group permissions */
#define S_IRGRP     0000040     /* Group read */
#define S_IWGRP     0000020     /* Group write */
#define S_IXGRP     0000010     /* Group execute */

#define S_IRWXO     0000007     /* Other permissions */
#define S_IROTH     0000004     /* Other read */
#define S_IWOTH     0000002     /* Other write */
#define S_IXOTH     0000001     /* Other execute */

/* Seek flags */
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

/* Access flags */
#define F_OK        0
#define X_OK        1
#define W_OK        2
#define R_OK        4

/* mmap flags */
#define PROT_NONE       0x0
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4

#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20
#define MAP_ANON        MAP_ANONYMOUS
#define MAP_GROWSDOWN   0x0100
#define MAP_DENYWRITE   0x0800
#define MAP_EXECUTABLE  0x1000
#define MAP_LOCKED      0x2000
#define MAP_NORESERVE   0x4000
#define MAP_POPULATE    0x8000
#define MAP_NONBLOCK    0x10000
#define MAP_STACK       0x20000
#define MAP_HUGETLB     0x40000

#define MAP_FAILED      ((void *)-1)

/* Clone flags */
#define CLONE_VM        0x00000100
#define CLONE_FS        0x00000200
#define CLONE_FILES     0x00000400
#define CLONE_SIGHAND   0x00000800
#define CLONE_PTRACE    0x00002000
#define CLONE_VFORK     0x00004000
#define CLONE_PARENT    0x00008000
#define CLONE_THREAD    0x00010000
#define CLONE_NEWNS     0x00020000
#define CLONE_SYSVSEM   0x00040000
#define CLONE_SETTLS    0x00080000
#define CLONE_PARENT_SETTID     0x00100000
#define CLONE_CHILD_CLEARTID    0x00200000
#define CLONE_DETACHED  0x00400000
#define CLONE_UNTRACED  0x00800000
#define CLONE_CHILD_SETTID      0x01000000
#define CLONE_NEWCGROUP 0x02000000
#define CLONE_NEWUTS    0x04000000
#define CLONE_NEWIPC    0x08000000
#define CLONE_NEWUSER   0x10000000
#define CLONE_NEWPID    0x20000000
#define CLONE_NEWNET    0x40000000
#define CLONE_IO        0x80000000

/* Stat structure */
struct stat {
    dev_t       st_dev;
    ino_t       st_ino;
    nlink_t     st_nlink;
    mode_t      st_mode;
    uid_t       st_uid;
    gid_t       st_gid;
    int         __pad0;
    dev_t       st_rdev;
    off_t       st_size;
    blksize_t   st_blksize;
    blkcnt_t    st_blocks;
    time_t      st_atime;
    time_t      st_atime_nsec;
    time_t      st_mtime;
    time_t      st_mtime_nsec;
    time_t      st_ctime;
    time_t      st_ctime_nsec;
    long        unused_pad[3];
};

/* Directory entry */
struct dirent {
    ino_t       d_ino;
    off_t       d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char        d_name[256];
};

/* dirent types */
#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK      10
#define DT_SOCK     12
#define DT_WHT      14

#endif /* _OBELISK_UAPI_SYSCALL_H */
