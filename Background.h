#pragma once

#include <thread>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <deque>

#include <boost/filesystem.hpp>
#include <boost/shared_ptr.hpp>

class BackgroundSync
{
public:
    BackgroundSync(const boost::filesystem::path& remote, const boost::filesystem::path& local)
        : running_(true)
        , started_(false)
        , remote_(remote)
        , local_(local)
    {
    }

    ~BackgroundSync()
    {
        running_ = false;
        cond_.notify_all();
        worker_.join();
    }

    void start()
    {
        if (started_)
            return;

        started_ = true;
        worker_ = std::thread(std::bind(&BackgroundSync::worker, this));
    }

    void flush()
    {
        start();

        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(lock_);
                if (queue_.empty() || !running_)
                    break;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    }

    void sync(const char* path)
    {
        start();

        std::unique_lock<std::mutex> lock(lock_);
        queue_.emplace_back(path);
        cond_.notify_all();
    }

private:
    void worker()
    {
        while (running_)
        {
            try
            {
                std::string path;

                {
                    std::unique_lock<std::mutex> lock(lock_);
                    while (queue_.empty() && running_)
                        cond_.wait(lock);

                    if (!running_)
                        break;

                    path = std::move(queue_.front());
                    queue_.pop_front();
                }

                const auto remote = remote_ / path;
                const auto local = local_ / path;

                const auto parent = remote.parent_path();
                if (!boost::filesystem::exists(parent))
                    boost::filesystem::create_directories(parent);

                boost::filesystem::copy_file(local, remote, boost::filesystem::copy_option::overwrite_if_exists);
            }
            catch (const std::exception& e)
            {
                std::cerr << "background worker failed: " << e.what() << std::endl;
            }
        }
    }

private:

    const boost::filesystem::path remote_;
    const boost::filesystem::path local_;

    bool running_;
    bool started_;

    std::mutex lock_;
    std::condition_variable cond_;
    std::deque<std::string> queue_;

    std::thread worker_;
};

