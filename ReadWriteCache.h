#pragma once

#include <errno.h>
#include <sys/stat.h>
#include <cstddef>
#include <fuse.h>
#include <dirent.h>
#include <cstring>
#include <unistd.h>
#include <cstdio>
#include <memory>
#include <vector>
#include <mutex>

#include <boost/filesystem.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/unordered_map.hpp>

class ReadWriteCache
{
public:
    ReadWriteCache(const boost::filesystem::path& src,
          const boost::filesystem::path& cache,
          const boost::filesystem::path& readWrite)
        : src_(src)
        , cache_(cache)
        , readWrite_(readWrite)
    {
    }

    boost::filesystem::path ensureExists(const char* path)
    {
        const auto cached = cache_ / path;

        if (boost::filesystem::exists(cached))
            return cached;

        const auto full = src_ / path;
        boost::filesystem::copy(full, cached);
        return cached;
    }

    boost::filesystem::path ensureParentExists(const char* path)
    {
        const auto cached = cache_ / path;

        if (boost::filesystem::exists(cached.parent_path()))
            return cached;

        const auto full = (src_ / path).parent_path();
        boost::filesystem::copy(full, cached);
        return cached;
    }

    int getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
    {
        const auto full = ensureExists(path);

        (void) fi;
        int res;

        res = lstat(full.c_str(), stbuf);
        if (res == -1)
            return -errno;

        return 0;
    }

    int access(const char *path, int mask)
    {
        const auto full = ensureExists(path);

        int res;

        res = ::access(full.c_str(), mask);
        if (res == -1)
            return -errno;

        return 0;
    }

    int readlink(const char *path, char *buf, size_t size)
    {
        const auto full = ensureExists(path);

        int res;

        res = ::readlink(full.c_str(), buf, size - 1);
        if (res == -1)
            return -errno;

        buf[res] = '\0';
        return 0;
    }


    int list(const char* path,
             void* buf,
             fuse_fill_dir_t filler,
             off_t offset,
             struct fuse_file_info* fi,
             enum fuse_readdir_flags flags)
    {
        const auto full = ensureExists(path);

        DIR *dp;
        struct dirent *de;

        (void) offset;
        (void) fi;
        (void) flags;

        dp = opendir(full.c_str());
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
        const auto full = ensureParentExists(path);

        int res;

        /* On Linux this could just be 'mknod(path, mode, rdev)' but this
           is more portable */
        if (S_ISREG(mode)) {
            res = ::open(full.c_str(), O_CREAT | O_EXCL | O_WRONLY, mode);
            if (res >= 0)
                res = close(res);
        } else if (S_ISFIFO(mode))
            res = ::mkfifo(full.c_str(), mode);
        else
            res = ::mknod(full.c_str(), mode, rdev);
        if (res == -1)
            return -errno;

        return 0;
    }

    int mkdir(const char *path, mode_t mode)
    {
        const auto full = ensureParentExists(path);

        int res;

        res = ::mkdir(full.c_str(), mode);
        if (res == -1)
            return -errno;

        return 0;
    }

    int unlink(const char *path)
    {
        const auto full = ensureExists(path);

        int res;

        res = ::unlink(full.c_str());
        if (res == -1)
            return -errno;

        return 0;
    }

    int rmdir(const char *path)
    {
        const auto full = ensureParentExists(path);

        int res;

        res = ::rmdir(full.c_str());
        if (res == -1)
            return -errno;

        return 0;
    }

    int symlink(const char *from, const char *to)
    {
        const auto full = ensureParentExists(from);

        int res;

        res = ::symlink(full.c_str(), (cache_ / to).c_str());
        if (res == -1)
            return -errno;

        return 0;
    }

    int rename(const char *from, const char *to, unsigned int flags)
    {
        const auto full = ensureParentExists(from);

        int res;

        if (flags)
            return -EINVAL;

        res = ::rename(full.c_str(), (cache_ / to).c_str());
        if (res == -1)
            return -errno;

        return 0;
    }

    int link(const char *from, const char *to)
    {
        int res;

        res = ::link((cache_ / from).c_str(), (cache_ / to).c_str());
        if (res == -1)
            return -errno;

        return 0;
    }

    int chmod(const char *path, mode_t mode,
                         struct fuse_file_info *fi)
    {
        const auto full = ensureExists(path);

        (void) fi;
        int res;

        res = ::chmod(full.c_str(), mode);
        if (res == -1)
            return -errno;

        return 0;
    }

    int chown(const char *path, uid_t uid, gid_t gid,
                         struct fuse_file_info *fi)
    {
        const auto full = ensureExists(path);

        (void) fi;
        int res;

        res = lchown(full.c_str(), uid, gid);
        if (res == -1)
            return -errno;

        return 0;
    }

    int truncate(const char *path, off_t size,
                            struct fuse_file_info *fi)
    {
        const auto full = ensureExists(path);

        int res;

        if (fi != NULL)
            res = ftruncate(fi->fh, size);
        else
            res = ::truncate(full.c_str(), size);
        if (res == -1)
            return -errno;

        return 0;
    }

    int create(const char *path, mode_t mode,
                          struct fuse_file_info *fi)
    {
        const auto full = ensureParentExists(path);

        int res;

        res = ::open(full.c_str(), fi->flags, mode);
        if (res == -1)
            return -errno;

        fi->fh = res;
        return 0;
    }

    int open(const char *path, struct fuse_file_info *fi)
    {
        const auto full = ensureExists(path);

        int res;

        res = ::open(full.c_str(), fi->flags);
        if (res == -1)
            return -errno;

        fi->fh = res;
        return 0;
    }

    int read(const char *path, char *buf, size_t size, off_t offset,
                        struct fuse_file_info *fi)
    {
        const auto full = ensureExists(path);

        int fd;
        int res;

        if(fi == NULL)
            fd = ::open(full.c_str(), O_RDONLY);
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
        const auto full = ensureExists(path);

        int fd;
        int res;

        (void) fi;
        if(fi == NULL)
            fd = ::open(full.c_str(), O_WRONLY);
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
    const boost::filesystem::path readWrite_;
};

