#include <cstdio>
#include <random>
#include <algorithm>
#include <iostream>
#include <cstring>

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

#include <thread>
#include <atomic>
#include <ctime>

class ExpFile
{
private:
    void *map_;
    int fd_;
    off_t size_;

public:
    ExpFile(const std::string& path, off_t size):
        map_(nullptr), fd_(-1)
    {
        size_ = size & ~(sysconf(_SC_PAGE_SIZE) - 1);

        fd_ = open(path.c_str(), O_CREAT | O_RDWR | O_DIRECT, S_IRUSR | S_IWUSR);
        if(fd_ == -1) {
            std::cerr << "Couldn't open " << path << ": " << std::strerror(errno) << std::endl;
            return;
        }
        if(unlink(path.c_str()) == -1) {
            std::cerr << "Couldn't unlink: " << std::strerror(errno) << std::endl;
        }
        if(ftruncate(fd_, size_) == -1) {
            std::cerr << "ftruncate failed: " << std::strerror(errno) << std::endl;
            return;
        }
        void *map = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if(map == MAP_FAILED) {
            std::cerr << "mmap failed: " << std::strerror(errno) << std::endl;
            return;
        }
        map_ = map;
    }
    
    bool valid() const
    {
        return fd_ != -1 && map_;
    }

    bool msync()
    {
        if(::msync(map_, size_, MS_ASYNC) == -1) {
            std::cerr << "msync failed: " << std::strerror(errno) << std::endl;
            return false;
        }
        return true;
    }

    ~ExpFile()
    {
        if(map_) munmap(map_, size_);
        if(fd_ != -1) close(fd_);
    }

    void *ptr()
    {
        return map_;
    }

    off_t size() const
    {
        return size_;
    }
};

int main(int argc, char *argv[])
{
    int count = 1;
    long long rbytes = 0;
    long long wbytes = 0;
    if(argc <= 2) {
        std::cerr << "Usage: " << argv[0] << " [path] [size(GB)]" << std::endl;
        return 1;
    }
    while(1) {
        ExpFile tmp(argv[1], std::atoll(argv[2]) * 1024 * 1024 * 1024);
        if(! tmp.valid()) return 1;
        const off_t size = tmp.size();
        std::mt19937_64 mt(count);
        uint64_t *p = reinterpret_cast<uint64_t*>(tmp.ptr());
        // write
        for(off_t i = 0; i < size; i += sizeof(uint64_t)) {
           wbytes += sizeof(uint64_t);
           *p++ = mt(); 
        }
        time_t t;
        std::time(&t);
        tmp.msync();
        std::cout << "[" << count << "] " << std::ctime(&t) << wbytes << " bytes written." << std::endl;
        // read
        mt.seed(count);
        p = reinterpret_cast<uint64_t*>(tmp.ptr());
        for(off_t i = 0; i < size; i += sizeof(uint64_t)) {
            rbytes += sizeof(uint64_t);
            if(mt() != *p++) {
                std::time(&t);
                std::cout << "[" << count << "] " << std::ctime(&t) << "Mismatch happened when " << rbytes << " read." << std::endl;
            }
        }
        std::time(&t);
        std::cout << "[" << count << "] " << std::ctime(&t) << rbytes << " bytes read." << std::endl;
        ++count;
    }
    return 0;
}
