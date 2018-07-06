
#pragma once

/*
 * exOS's virtual file system
 *
 * As this project's goals are by far different from the Linux ones, this
 * layer won't provide anything close to the Linux's VFS. Its purpose is to
 * provide the MINIMUM NECESSARY to allow basic operations like open, read,
 * write, close to work both on FAT32 and on character devices like /dev/tty.
 * In particular:
 *
 *    - No real disk I/O will be supported
 *    - No disk cache
 *    - No access control: single user (root) system
 *    - No SMP
 *    - Only the simplest Linux syscalls will be supported
 *
 */

#include <exos/common/basic_defs.h>
#include <exos/kernel/sys_types.h>

/*
 * Opaque type for file handles.
 *
 * The only requirement for such handles is that they must have at their
 * beginning all the members of fs_handle_base. Therefore, a fs_handle MUST
 * always be castable to fs_handle_base *.
 */
typedef void *fs_handle;

typedef struct filesystem filesystem;

/* fs ops */
typedef void (*func_close) (fs_handle);
typedef int (*func_open) (filesystem *, const char *, fs_handle *);
typedef int (*func_dup) (fs_handle, fs_handle *);
typedef int (*func_getdents64) (fs_handle, struct linux_dirent64 *, u32);

typedef void (*func_fs_ex_lock)(filesystem *);
typedef void (*func_fs_ex_unlock)(filesystem *);
typedef void (*func_fs_sh_lock)(filesystem *);
typedef void (*func_fs_sh_unlock)(filesystem *);

/* file ops */
typedef ssize_t (*func_read) (fs_handle, char *, size_t);
typedef ssize_t (*func_write) (fs_handle, char *, size_t);
typedef off_t (*func_seek) (fs_handle, off_t, int);
typedef int (*func_ioctl) (fs_handle, uptr, void *);
typedef int (*func_stat) (fs_handle, struct stat *);

/* file ops (optional) */
typedef void (*func_ex_lock)(fs_handle);
typedef void (*func_ex_unlock)(fs_handle);
typedef void (*func_sh_lock)(fs_handle);
typedef void (*func_sh_unlock)(fs_handle);




#define EXVFS_FS_RO        (0)
#define EXVFS_FS_RW        (1 << 0)

struct filesystem {

   const char *fs_type_name; /* statically allocated: do NOT free() */

   u32 device_id;
   u32 flags;
   void *device_data;

   func_open open;
   func_close close;
   func_dup dup;
   func_getdents64 getdents64;

   /* Whole-filesystem locks */
   func_fs_ex_lock fs_exlock;
   func_fs_ex_unlock fs_exunlock;
   func_fs_sh_lock fs_shlock;
   func_fs_sh_unlock fs_shunlock;
};

typedef struct {

   func_read read;
   func_write write;
   func_seek seek;
   func_ioctl ioctl;
   func_stat stat;

   /* optional, per-file locks */
   func_ex_lock exlock;
   func_ex_unlock exunlock;
   func_sh_lock shlock;
   func_sh_unlock shunlock;

} file_ops;

typedef struct {

   filesystem *fs;
   u32 path_len;
   char path[0];

} mountpoint;

/*
 * Each fs_handle struct should contain at its beginning the fields of the
 * following base struct [a rough attempt to emulate inheritance in C].
 *
 * TODO: introduce a ref-count in the fs_base_handle struct when implementing
 * thread support.
 */
typedef struct {

   filesystem *fs;
   file_ops fops;

} fs_handle_base;

int mountpoint_add(filesystem *fs, const char *path);
void mountpoint_remove(filesystem *fs);

int exvfs_open(const char *path, fs_handle *out);
int exvfs_ioctl(fs_handle h, uptr request, void *argp);
int exvfs_stat(fs_handle h, struct stat *statbuf);
int exvfs_dup(fs_handle h, fs_handle *dup_h);
int exvfs_getdents64(fs_handle h, struct linux_dirent64 *dirp, u32 bs);
void exvfs_close(fs_handle h);

ssize_t exvfs_read(fs_handle h, void *buf, size_t buf_size);
ssize_t exvfs_write(fs_handle h, void *buf, size_t buf_size);

// TODO: consider using 's64' instead of off_t.
off_t exvfs_seek(fs_handle h, off_t off, int whence);

static ALWAYS_INLINE filesystem *get_fs(fs_handle h)
{
   ASSERT(h != NULL);
   return ((fs_handle_base *)h)->fs;
}

/* Per-file locks */
void exvfs_exlock(fs_handle h);
void exvfs_exunlock(fs_handle h);
void exvfs_shlock(fs_handle h);
void exvfs_shunlock(fs_handle h);

/* Whole-filesystem locks */
void exvfs_fs_exlock(filesystem *fs);
void exvfs_fs_exunlock(filesystem *fs);
void exvfs_fs_shlock(filesystem *fs);
void exvfs_fs_shunlock(filesystem *fs);
/* --- */

int
compute_abs_path(const char *path, const char *cwd, char *dest, u32 dest_size);

u32 exvfs_get_new_device_id(void);