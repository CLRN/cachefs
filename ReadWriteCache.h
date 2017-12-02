#pragma once

#include "Background.h"
#include "Logger.h"

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
#include <boost/algorithm/string/replace.hpp>

class ReadWriteCache
{
public:
    ReadWriteCache(const boost::filesystem::path& src,
          const boost::filesystem::path& cache,
          const boost::filesystem::path& readWrite)
        : src_(src)
        , cache_(cache)
        , readWrite_(readWrite)
        , sync_(src, cache)
    {
    }

    void copyDirectoryRecursively(const boost::filesystem::path& sourceDir, const boost::filesystem::path& destinationDir)
    {
        namespace fs = boost::filesystem;

        Logger::instance() << "read-write copy '" << sourceDir.string() << "' -> '" << destinationDir.string() << "'" << std::endl;

        fs::create_directories(destinationDir);

        boost::system::error_code ignore;

        for (const auto& dirEnt : fs::recursive_directory_iterator{sourceDir})
        {
            const auto& path = dirEnt.path();
            auto relativePathStr = path.string();
            boost::replace_first(relativePathStr, sourceDir.string(), "");

            const auto dst = destinationDir / relativePathStr;

            const auto time = boost::filesystem::last_write_time(path);

            fs::copy(path, dst);

            boost::filesystem::last_write_time(dst, time, ignore);
        }

        Logger::instance() << "completed copy '" << sourceDir.string() << "' -> '" << destinationDir.string() << "'" << std::endl;
    }

    boost::filesystem::path ensureCacheExists(const char* path)
    {
        static std::mutex lock_;
        std::unique_lock<std::mutex> lock(lock_);

        const auto cached = cache_ / path;

        auto dest = readWrite_.string();
        boost::algorithm::replace_first(dest, src_.string(), cache_.string());

        if (!boost::filesystem::exists(dest))
            copyDirectoryRecursively(readWrite_, dest);

        return cached;
    }

    int getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
    {
        const auto full = ensureCacheExists(path);

        (void) fi;
        int res;

        res = lstat(full.c_str(), stbuf);
        if (res == -1)
            return -errno;

        return 0;
    }

    int access(const char *path, int mask)
    {
        const auto full = ensureCacheExists(path);

        int res;

        res = ::access(full.c_str(), mask);
        if (res == -1)
            return -errno;

        return 0;
    }

    int readlink(const char *path, char *buf, size_t size)
    {
        const auto full = ensureCacheExists(path);

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
        const auto full = ensureCacheExists(path);

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
        const auto full = ensureCacheExists(path);

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
        const auto full = ensureCacheExists(path);
        const auto remote = src_ / path;

        int res;

        sync_.flush();

        res = ::mkdir(full.c_str(), mode);
        if (res == -1)
            return -errno;

        res = ::mkdir(remote.c_str(), mode);
        if (res == -1)
            return -errno;

        return 0;
    }

    int unlink(const char *path)
    {
        const auto full = ensureCacheExists(path);
        const auto remote = src_ / path;

        int res;

        sync_.flush();

        res = ::unlink(full.c_str());
        if (res == -1)
            return -errno;

        res = ::unlink(remote.c_str());
        if (res == -1)
            return -errno;

        return 0;
    }

    int rmdir(const char *path)
    {
        const auto full = ensureCacheExists(path);
        const auto remote = src_ / path;

        int res;

        sync_.flush();

        res = ::rmdir(full.c_str());
        if (res == -1)
            return -errno;

        res = ::rmdir(remote.c_str());
        if (res == -1)
            return -errno;

        return 0;
    }

    int symlink(const char *from, const char *to)
    {
        const auto full = ensureCacheExists(from);

        int res;

        sync_.flush();

        res = ::symlink(full.c_str(), (cache_ / to).c_str());
        if (res == -1)
            return -errno;

        res = ::symlink((src_ / from).c_str(), (src_ / to).c_str());
        if (res == -1)
            return -errno;

        return 0;
    }

    int rename(const char *from, const char *to, unsigned int flags)
    {
        const auto full = ensureCacheExists(from);

        int res;

        if (flags)
            return -EINVAL;

        sync_.flush();

        res = ::rename(full.c_str(), (cache_ / to).c_str());
        if (res == -1)
            return -errno;

        res = ::rename((src_ / from).c_str(), (src_ / to).c_str());
        if (res == -1)
            return -errno;

        return 0;
    }

    int link(const char *from, const char *to)
    {
        int res;

        sync_.flush();

        res = ::link((cache_ / from).c_str(), (cache_ / to).c_str());
        if (res == -1)
            return -errno;

        res = ::link((src_ / from).c_str(), (src_ / to).c_str());
        if (res == -1)
            return -errno;

        return 0;
    }

    int chmod(const char *path, mode_t mode,
                         struct fuse_file_info *fi)
    {
        const auto full = ensureCacheExists(path);

        (void) fi;
        int res;

        sync_.flush();

        res = ::chmod(full.c_str(), mode);
        if (res == -1)
            return -errno;

        res = ::chmod((src_ / path).c_str(), mode);
        if (res == -1)
            return -errno;

        return 0;
    }

    int chown(const char *path, uid_t uid, gid_t gid,
                         struct fuse_file_info *fi)
    {
        const auto full = ensureCacheExists(path);

        (void) fi;
        int res;

        sync_.flush();

        res = lchown(full.c_str(), uid, gid);
        if (res == -1)
            return -errno;

        res = lchown((src_ / path).c_str(), uid, gid);
        if (res == -1)
            return -errno;

        return 0;
    }

    int truncate(const char *path, off_t size,
                            struct fuse_file_info *fi)
    {
        const auto full = ensureCacheExists(path);

        int res;

        if (fi != NULL)
            res = ftruncate(fi->fh, size);
        else
            res = ::truncate(full.c_str(), size);
        if (res == -1)
            return -errno;

        sync_.sync(path);

        return 0;
    }

    int create(const char *path, mode_t mode,
                          struct fuse_file_info *fi)
    {
        const auto full = ensureCacheExists(path);

        int res;

        sync_.flush();

        res = ::open(full.c_str(), fi->flags, mode);
        if (res == -1)
            return -errno;

        fi->fh = res;

        res = ::open((src_ / path).c_str(), fi->flags, mode);
        if (res == -1)
        {
            close(fi->fh);
            fi->fh = 0;
            return -errno;
        }

        close(res);

        return 0;
    }

    int open(const char *path, struct fuse_file_info *fi)
    {
        const auto full = ensureCacheExists(path);

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
        const auto full = ensureCacheExists(path);

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
        const auto full = ensureCacheExists(path);

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

        std::unique_lock<std::mutex> lock(mutex_);
        writtenFiles_.emplace(fd);

        return res;
    }

    int release(const char *path, struct fuse_file_info *fi)
    {
        (void) path;
        close(fi->fh);

        std::unique_lock<std::mutex> lock(mutex_);
        const auto it = writtenFiles_.find(fi->fh);
        if (it != writtenFiles_.end())
        {
            writtenFiles_.erase(it);
            lock.unlock();

            sync_.sync(path);
        }

        return 0;
    }


private:
    const boost::filesystem::path src_;
    const boost::filesystem::path cache_;
    const boost::filesystem::path readWrite_;

    BackgroundSync sync_;

    std::mutex mutex_;
    std::set<uint64_t> writtenFiles_;
};

