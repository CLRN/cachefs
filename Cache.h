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

#include "ReadWriteCache.h"
#include "ReadOnlyCache.h"

class Cache
{
public:
    Cache(const boost::filesystem::path& src,
          const boost::filesystem::path& cache,
          const boost::filesystem::path& readWrite)
        : src_(boost::filesystem::system_complete(src))
        , cache_(boost::filesystem::system_complete(cache))
        , readWrite_(boost::filesystem::system_complete(readWrite))
        , readOnlyCache_(src, cache, readWrite)
        , readWriteCache_(src, cache, readWrite)
    {
    }

    bool isReadOnly(const char* path)
    {
        const auto full = src_ / path;
        return readWrite_.string().find(full.string()) == std::string::npos;
    }

    int getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
    {
        return isReadOnly(path) ? readOnlyCache_.getattr(path, stbuf, fi) : readWriteCache_.getattr(path, stbuf, fi);
    }

    int access(const char *path, int mask)
    {
        return isReadOnly(path) ? readOnlyCache_.access(path, mask) : readWriteCache_.access(path, mask);
    }

    int readlink(const char *path, char *buf, size_t size)
    {
        return isReadOnly(path) ? readOnlyCache_.readlink(path, buf, size) : readWriteCache_.readlink(path, buf, size);
    }

    int list(const char* path,
             void* buf,
             fuse_fill_dir_t filler,
             off_t offset,
             struct fuse_file_info* fi,
             enum fuse_readdir_flags flags)
    {
        return isReadOnly(path) ? readOnlyCache_.list(path, buf, filler, offset, fi, flags) : readWriteCache_.list(path, buf, filler, offset, fi, flags);
    }

    int mknod(const char *path, mode_t mode, dev_t rdev)
    {
        return isReadOnly(path) ? readOnlyCache_.mknod(path, mode, rdev) : readWriteCache_.mknod(path, mode, rdev);
    }

    int mkdir(const char *path, mode_t mode)
    {
        return isReadOnly(path) ? readOnlyCache_.mkdir(path, mode) : readWriteCache_.mkdir(path, mode);
    }

    int unlink(const char *path)
    {
        return isReadOnly(path) ? readOnlyCache_.unlink(path) : readWriteCache_.unlink(path);
    }

    int rmdir(const char *path)
    {
        return isReadOnly(path) ? readOnlyCache_.rmdir(path) : readWriteCache_.rmdir(path);
    }

    int symlink(const char *from, const char *to)
    {
        return isReadOnly(from) ? readOnlyCache_.symlink(from, to) : readWriteCache_.symlink(from, to);
    }

    int rename(const char *from, const char *to, unsigned int flags)
    {
        return isReadOnly(from) ? readOnlyCache_.rename(from, to, flags) : readWriteCache_.rename(from, to, flags);
    }

    int link(const char *from, const char *to)
    {
        return isReadOnly(from) ? readOnlyCache_.link(from, to) : readWriteCache_.link(from, to);
    }

    int chmod(const char *path, mode_t mode,
                         struct fuse_file_info *fi)
    {
        return isReadOnly(path) ? readOnlyCache_.chmod(path, mode, fi) : readWriteCache_.chmod(path, mode, fi);
    }

    int chown(const char *path, uid_t uid, gid_t gid,
                         struct fuse_file_info *fi)
    {
        return isReadOnly(path) ? readOnlyCache_.chown(path, uid, gid, fi) : readWriteCache_.chown(path, uid, gid, fi);
    }

    int truncate(const char *path, off_t size,
                            struct fuse_file_info *fi)
    {
        return isReadOnly(path) ? readOnlyCache_.truncate(path, size, fi) : readWriteCache_.truncate(path, size, fi);
    }

    int create(const char *path, mode_t mode,
                          struct fuse_file_info *fi)
    {
        return isReadOnly(path) ? readOnlyCache_.create(path, mode, fi) : readWriteCache_.create(path, mode, fi);
    }

    int open(const char *path, struct fuse_file_info *fi)
    {
        return isReadOnly(path) ? readOnlyCache_.open(path, fi) : readWriteCache_.open(path, fi);
    }

    int read(const char *path, char *buf, size_t size, off_t offset,
                        struct fuse_file_info *fi)
    {
        return isReadOnly(path) ? readOnlyCache_.read(path, buf, size, offset, fi) : readWriteCache_.read(path, buf, size, offset, fi);
    }

    int write(const char *path, const char *buf, size_t size,
                         off_t offset, struct fuse_file_info *fi)
    {
        return isReadOnly(path) ? readOnlyCache_.write(path, buf, size, offset, fi) : readWriteCache_.write(path, buf, size, offset, fi);
    }

    int release(const char *path, struct fuse_file_info *fi)
    {
        return isReadOnly(path) ? readOnlyCache_.release(path, fi) : readWriteCache_.release(path, fi);
    }


private:
    const boost::filesystem::path src_;
    const boost::filesystem::path cache_;
    const boost::filesystem::path readWrite_;

    ReadOnlyCache readOnlyCache_;
    ReadWriteCache readWriteCache_;
};

