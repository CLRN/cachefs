#pragma once

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
#include <boost/make_shared.hpp>
#include <boost/optional.hpp>
#include <boost/algorithm/string/erase.hpp>

class ReadOnlyCache
{
    struct DirEntry
    {
        DirEntry(struct stat& in, const char* name) : stat_(in), name_(name) {}

        struct stat stat_;
        std::string name_;
    };

    struct CacheEntry
    {
        typedef boost::shared_ptr<CacheEntry> Ptr;

        std::mutex lock_;

        boost::optional<int> checkResult_;
        struct stat stat_;

        std::vector<char> link_;
        boost::optional<int> linkResult_;

        std::map<int, int> accessMap_;

        std::vector<DirEntry> list_;
        boost::optional<int> listResult_;
    };

    typedef boost::unordered_map<boost::filesystem::path, CacheEntry::Ptr> CacheMap;


public:
    ReadOnlyCache(const boost::filesystem::path& src,
          const boost::filesystem::path& cache,
          const boost::filesystem::path& readWrite)
        : src_(src)
        , cache_(cache)
        , readWrite_(readWrite)
    {
        //readCache();
    }

    void readCache()
    {
        Logger::instance() << "reading cache from: " << cache_.string() << std::endl;

        boost::filesystem::recursive_directory_iterator it(cache_);
        boost::filesystem::recursive_directory_iterator end;

        char buffer[4096] = {};

        const auto filler = [](void* buf,
                               const char* name,
                               const struct stat* stbuf,
                               off_t off,
                               enum fuse_fill_dir_flags flags)
        {
            return 1;
        };

        for (int count; it != end; ++it, ++count)
        {
            std::string path = it->path().string();
            boost::algorithm::erase_all(path, cache_.string());

            struct stat s = {};
            fuse_file_info info = {};
            getattr(path.c_str(), &s, &info);
            access(path.c_str(), 4);

            if (boost::filesystem::is_directory(it->path()))
                list(path.c_str(), buffer, filler, 0, &info, fuse_readdir_flags());
        }

        Logger::instance() << "read " << cacheMap_.size() << " items" << std::endl;
    }

    CacheEntry::Ptr get(const boost::filesystem::path& path)
    {
        std::unique_lock<std::mutex> lock(cacheLock_);
        auto it = cacheMap_.find(path);
        if (it == cacheMap_.end())
        {
            it = cacheMap_.emplace(path, boost::make_shared<CacheEntry>()).first;
            Logger::instance() << "MISS: " << path.string() << std::endl;
        }
        return it->second;
    }

    int ret(int res)
    {
        return res == -1 ? -errno : 0;
    }

    int getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
    {
        const auto full = src_ / path;
        const auto entry = get(path);

        std::unique_lock<std::mutex> lock(entry->lock_);
        if (!entry->checkResult_)
            entry->checkResult_ = lstat(full.c_str(), &entry->stat_);

        memcpy(stbuf, &entry->stat_, sizeof(*stbuf));
        return ret(*entry->checkResult_);
    }

    int access(const char *path, int mask)
    {
        const auto full = src_ / path;
        const auto entry = get(path);

        std::unique_lock<std::mutex> lock(entry->lock_);
        auto it = entry->accessMap_.find(mask);
        if (it == entry->accessMap_.end())
            it = entry->accessMap_.emplace(mask, ::access(full.c_str(), mask)).first;

        return ret(it->second);
    }

    int readlink(const char *path, char *buf, size_t size)
    {
        const auto full = src_ / path;
        const auto entry = get(path);

        std::unique_lock<std::mutex> lock(entry->lock_);
        if (!entry->linkResult_)
        {
            entry->link_.resize(size);
            entry->linkResult_ = ::readlink(full.c_str(), entry->link_.data(), size - 1);
            if (entry->linkResult_ != -1)
                entry->link_.resize(*entry->linkResult_);
        }

        int res = *entry->linkResult_;
        if (res == -1)
            return -errno;

        memcpy(buf, entry->link_.data(), res);

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
        const auto full = src_ / path;
        const auto entry = get(path);

        if (!entry->listResult_)
        {
            DIR *dp;
            struct dirent *de;

            Logger::instance() << "LISTING " << full.string() << std::endl;

            dp = opendir(full.c_str());
            if (dp == NULL)
            {
                entry->listResult_ = -errno;
            }
            else
            {
                while ((de = readdir(dp)) != NULL) {
                    struct stat st;
                    memset(&st, 0, sizeof(st));
                    st.st_ino = de->d_ino;
                    st.st_mode = de->d_type << 12;

                    entry->list_.emplace_back(DirEntry(st, de->d_name));
                }

                closedir(dp);
            }

            entry->listResult_ = 0;
        }

        for (const auto& item : entry->list_)
        {
            if (filler(buf, item.name_.c_str(), &item.stat_, 0, static_cast<fuse_fill_dir_flags>(0)))
                break;
        }

        return ret(*entry->listResult_);
    }

    int mknod(const char *path, mode_t mode, dev_t rdev)
    {
        return -errno;
    }

    int mkdir(const char *path, mode_t mode)
    {
        return -errno;
    }

    int unlink(const char *path)
    {
        return -errno;
    }

    int rmdir(const char *path)
    {
        return -errno;
    }

    int symlink(const char *from, const char *to)
    {
        return -errno;
    }

    int rename(const char *from, const char *to, unsigned int flags)
    {
        return -errno;
    }

    int link(const char *from, const char *to)
    {
        return -errno;
    }

    int chmod(const char *path, mode_t mode,
                         struct fuse_file_info *fi)
    {
        return -errno;
    }

    int chown(const char *path, uid_t uid, gid_t gid,
                         struct fuse_file_info *fi)
    {
        return -errno;
    }

    int truncate(const char *path, off_t size,
                            struct fuse_file_info *fi)
    {
        return -errno;
    }

    int create(const char *path, mode_t mode,
                          struct fuse_file_info *fi)
    {
        return -errno;
    }

    int open(const char *path, struct fuse_file_info *fi)
    {
        const auto cached = cache_ / path;
        const auto full = src_ / path;

        if (!boost::filesystem::exists(cached))
        {
            Logger::instance() << "read-only copy '" << full.string() << "' -> '" << cached.string() << "'" << std::endl;
            boost::filesystem::create_directories(cached.parent_path());
            boost::filesystem::copy_file(full, cached);
        }

        int res;

        res = ::open(cached.c_str(), fi->flags);
        if (res == -1)
            return -errno;

        fi->fh = res;
        return 0;
    }

    int read(const char *path, char *buf, size_t size, off_t offset,
                        struct fuse_file_info *fi)
    {
        const auto cached = cache_ / path;
        const auto full = src_ / path;

        if (!boost::filesystem::exists(cached))
        {
            Logger::instance() << "read-only copy '" << full.string() << "' -> '" << cached.string() << "'" << std::endl;
            boost::filesystem::create_directories(cached.parent_path());
            boost::filesystem::copy_file(full, cached);
        }

        int fd;
        int res;

        if(fi == NULL)
            fd = ::open(cached.c_str(), O_RDONLY);
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
        return -errno;
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

    CacheMap cacheMap_;
    std::mutex cacheLock_;
};

