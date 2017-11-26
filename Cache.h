#pragma once

#include <errno.h>
#include <sys/stat.h>
#include <cstddef>
#include <fuse.h>
#include <dirent.h>
#include <cstring>
#include <unistd.h>
#include <cstdio>

#include <boost/filesystem.hpp>

class Cache
{
public:
    Cache(const boost::filesystem::path& src, const boost::filesystem::path& cache)
        : src_(src)
        , cache_(cache)
    {
    }

    int getattr(const char *path, struct stat *stbuf,
                           struct fuse_file_info *fi)
    {
        (void) fi;
        int res;

        res = lstat(path, stbuf);
        if (res == -1)
            return -errno;

        return 0;
    }

    int access(const char *path, int mask)
    {
        int res;

        res = ::access(path, mask);
        if (res == -1)
            return -errno;

        return 0;
    }

    int readlink(const char *path, char *buf, size_t size)
    {
        int res;

        res = ::readlink(path, buf, size - 1);
        if (res == -1)
            return -errno;

        buf[res] = '\0';
        return 0;
    }


    int list(const char *path, void *buf, fuse_fill_dir_t filler,
                           off_t offset, struct fuse_file_info *fi,
                           enum fuse_readdir_flags flags)
    {
        DIR *dp;
        struct dirent *de;

        (void) offset;
        (void) fi;
        (void) flags;

        dp = opendir(path);
        if (dp == NULL)
            return -errno;

        while ((de = readdir(dp)) != NULL) {
            struct stat st;
            memset(&st, 0, sizeof(st));
            st.st_ino = de->d_ino;
            st.st_mode = de->d_type << 12;
            if (filler(buf, de->d_name, &st, 0, static_cast<fuse_fill_dir_flags>(0)))
                break;
        }

        closedir(dp);
        return 0;
    }

    int mknod(const char *path, mode_t mode, dev_t rdev)
    {
        int res;

        /* On Linux this could just be 'mknod(path, mode, rdev)' but this
           is more portable */
        if (S_ISREG(mode)) {
            res = ::open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
            if (res >= 0)
                res = close(res);
        } else if (S_ISFIFO(mode))
            res = mkfifo(path, mode);
        else
            res = mknod(path, mode, rdev);
        if (res == -1)
            return -errno;

        return 0;
    }

    int mkdir(const char *path, mode_t mode)
    {
        int res;

        res = mkdir(path, mode);
        if (res == -1)
            return -errno;

        return 0;
    }

    int unlink(const char *path)
    {
        int res;

        res = unlink(path);
        if (res == -1)
            return -errno;

        return 0;
    }

    int rmdir(const char *path)
    {
        int res;

        res = rmdir(path);
        if (res == -1)
            return -errno;

        return 0;
    }

    int symlink(const char *from, const char *to)
    {
        int res;

        res = symlink(from, to);
        if (res == -1)
            return -errno;

        return 0;
    }

    int rename(const char *from, const char *to, unsigned int flags)
    {
        int res;

        if (flags)
            return -EINVAL;

        res = ::rename(from, to);
        if (res == -1)
            return -errno;

        return 0;
    }

    int link(const char *from, const char *to)
    {
        int res;

        res = link(from, to);
        if (res == -1)
            return -errno;

        return 0;
    }

    int chmod(const char *path, mode_t mode,
                         struct fuse_file_info *fi)
    {
        (void) fi;
        int res;

        res = ::chmod(path, mode);
        if (res == -1)
            return -errno;

        return 0;
    }

    int chown(const char *path, uid_t uid, gid_t gid,
                         struct fuse_file_info *fi)
    {
        (void) fi;
        int res;

        res = lchown(path, uid, gid);
        if (res == -1)
            return -errno;

        return 0;
    }

    int truncate(const char *path, off_t size,
                            struct fuse_file_info *fi)
    {
        int res;

        if (fi != NULL)
            res = ftruncate(fi->fh, size);
        else
            res = ::truncate(path, size);
        if (res == -1)
            return -errno;

        return 0;
    }

    int create(const char *path, mode_t mode,
                          struct fuse_file_info *fi)
    {
        int res;

        res = ::open(path, fi->flags, mode);
        if (res == -1)
            return -errno;

        fi->fh = res;
        return 0;
    }

    int open(const char *path, struct fuse_file_info *fi)
    {
        int res;

        res = ::open(path, fi->flags);
        if (res == -1)
            return -errno;

        fi->fh = res;
        return 0;
    }

    int read(const char *path, char *buf, size_t size, off_t offset,
                        struct fuse_file_info *fi)
    {
        int fd;
        int res;

        if(fi == NULL)
            fd = open(path, O_RDONLY);
        else
            fd = fi->fh;

        if (fd == -1)
            return -errno;

        res = pread(fd, buf, size, offset);
        if (res == -1)
            res = -errno;

        if(fi == NULL)
            close(fd);
        return res;
    }

    int write(const char *path, const char *buf, size_t size,
                         off_t offset, struct fuse_file_info *fi)
    {
        int fd;
        int res;

        (void) fi;
        if(fi == NULL)
            fd = ::open(path, O_WRONLY);
        else
            fd = fi->fh;

        if (fd == -1)
            return -errno;

        res = pwrite(fd, buf, size, offset);
        if (res == -1)
            res = -errno;

        if(fi == NULL)
            close(fd);
        return res;
    }

    int release(const char *path, struct fuse_file_info *fi)
    {
        (void) path;
        close(fi->fh);
        return 0;
    }


private:
    const boost::filesystem::path src_;
    const boost::filesystem::path cache_;
};

