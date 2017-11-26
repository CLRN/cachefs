/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

/** @file
 *
 * This file system mirrors the existing file system hierarchy of the
 * system, starting at the root file system. This is implemented by
 * just "passing through" all requests to the corresponding user-space
 * libc functions. Its performance is terrible.
 *
 * Compile with
 *
 *     gcc -Wall passthrough.c `pkg-config fuse3 --cflags --libs` -o passthrough
 *
 * ## Source code ##
 * \include passthrough.c
 */


#define FUSE_USE_VERSION 31

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include "Cache.h"

#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

Cache cache_("~/cachefs/src", "~/cachefs/cache");

static void *xmp_init(fuse_conn_info *conn,
                      fuse_config *cfg)
{
    (void) conn;
    cfg->use_ino = 1;

    /* Pick up changes from lower filesystem right away. This is
       also necessary for better hardlink support. When the kernel
       calls the unlink() handler, it does not know the inode of
       the to-be-removed entry and can therefore not invalidate
       the cache of the associated inode - resulting in an
       incorrect st_nlink value being reported for any remaining
       hardlinks to this inode. */
    cfg->entry_timeout = 0;
    cfg->attr_timeout = 0;
    cfg->negative_timeout = 0;

    return NULL;
}

static int xmp_getattr(const char *path, struct stat *stbuf,
                       struct fuse_file_info *fi)
{
    return cache_.getattr(path, stbuf, fi);
}

static int xmp_access(const char *path, int mask)
{
    return cache_.access(path, mask);
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
    return cache_.readlink(path, buf, size);
}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi,
                       enum fuse_readdir_flags flags)
{
    return cache_.list(path, buf, filler, offset, fi, flags);
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
    return cache_.mknod(path, mode, rdev);
}

static int xmp_mkdir(const char *path, mode_t mode)
{
    return cache_.mkdir(path, mode);
}

static int xmp_unlink(const char *path)
{
    return cache_.unlink(path);
}

static int xmp_rmdir(const char *path)
{
    return cache_.rmdir(path);
}

static int xmp_symlink(const char *from, const char *to)
{
    return cache_.symlink(from, to);
}

static int xmp_rename(const char *from, const char *to, unsigned int flags)
{
    return cache_.rename(from, to, flags);
}

static int xmp_link(const char *from, const char *to)
{
    return cache_.link(from, to);
}

static int xmp_chmod(const char *path, mode_t mode,
                     struct fuse_file_info *fi)
{
    return cache_.chmod(path, mode, fi);
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid,
                     struct fuse_file_info *fi)
{
    return cache_.chown(path, uid, gid, fi);
}

static int xmp_truncate(const char *path, off_t size,
                        struct fuse_file_info *fi)
{
    return cache_.truncate(path, size, fi);
}

#ifdef HAVE_UTIMENSAT
static int xmp_utimens(const char *path, const struct timespec ts[2],
		       struct fuse_file_info *fi)
{
	(void) fi;
	int res;

	/* don't use utime/utimes since they follow symlinks */
	res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return -errno;

	return 0;
}
#endif

static int xmp_create(const char *path, mode_t mode,
                      struct fuse_file_info *fi)
{
    return cache_.create(path, mode, fi);
}

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
    return cache_.open(path, fi);
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    return cache_.read(path, buf, size, offset, fi);
}

static int xmp_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{
    return cache_.write(path, buf, size, offset, fi);
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
    int res;

    res = statvfs(path, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
    return cache_.release(path, fi);
}

static int xmp_fsync(const char *path, int isdatasync,
                     struct fuse_file_info *fi)
{
    /* Just a stub.	 This method is optional and can safely be left
       unimplemented */

    (void) path;
    (void) isdatasync;
    (void) fi;
    return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int xmp_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
	int fd;
	int res;

	(void) fi;

	if (mode)
		return -EOPNOTSUPP;

	if(fi == NULL)
		fd = open(path, O_WRONLY);
	else
		fd = fi->fh;

	if (fd == -1)
		return -errno;

	res = -posix_fallocate(fd, offset, length);

	if(fi == NULL)
		close(fd);
	return res;
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	int res = lsetxattr(path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	int res = lgetxattr(path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
	int res = llistxattr(path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
	int res = lremovexattr(path, name);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

int main(int argc, char *argv[])
{
    fuse_operations xmp_oper = {};

    xmp_oper.init       = xmp_init,
    xmp_oper.getattr	= xmp_getattr,
    xmp_oper.access		= xmp_access,
    xmp_oper.readlink	= xmp_readlink,
    xmp_oper.readdir	= xmp_readdir,
    xmp_oper.mknod		= xmp_mknod,
    xmp_oper.mkdir		= xmp_mkdir,
    xmp_oper.symlink	= xmp_symlink,
    xmp_oper.unlink		= xmp_unlink,
    xmp_oper.rmdir		= xmp_rmdir,
    xmp_oper.rename		= xmp_rename,
    xmp_oper.link		= xmp_link,
    xmp_oper.chmod		= xmp_chmod,
    xmp_oper.chown		= xmp_chown,
    xmp_oper.truncate	= xmp_truncate,
    #ifdef HAVE_UTIMENSAT
        xmp_oper.utimens	= xmp_utimens,
#endif
            xmp_oper.open		= xmp_open,
    xmp_oper.create 	= xmp_create,
    xmp_oper.read		= xmp_read,
    xmp_oper.write		= xmp_write,
    xmp_oper.statfs		= xmp_statfs,
    xmp_oper.release	= xmp_release,
    xmp_oper.fsync		= xmp_fsync,
#ifdef HAVE_POSIX_FALLOCATE
        xmp_oper.fallocate	= xmp_fallocate,
#endif
#ifdef HAVE_SETXATTR
    xmp_oper.setxattr	= xmp_setxattr,
	xmp_oper.getxattr	= xmp_getxattr,
	xmp_oper.listxattr	= xmp_listxattr,
	xmp_oper.removexattr	= xmp_removexattr,
#endif

    umask(0);
    return fuse_main(argc, argv, &xmp_oper, NULL);
}