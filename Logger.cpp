#include <iosfwd>
#include "Logger.h"

#include <fstream>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/time_formatters.hpp>

class FileHolder
{
public:
    FileHolder()
    {
        ofs_.open("/tmp/cachefs.log", std::ios::binary);
        if (!ofs_.is_open())
            throw std::runtime_error("failed to open log");
    }

    std::ostream& get()
    {
        return ofs_;
    }

private:
    std::ofstream ofs_;
};


std::ostream& Logger::instance()
{
    static FileHolder file;
    return file.get()
        << "["
        << boost::posix_time::to_iso_extended_string(boost::posix_time::microsec_clock::local_time())
        << "]: ";
}
