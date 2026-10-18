// POSIX implementation (Linux, macOS). A Windows implementation
// (MoveFileExW with MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH,
// FlushFileBuffers, LockFileEx) is still to be written.
#include "strata/save/fs.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace strata::save::fs {

bool exists(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool fsyncFile(const std::string& p) {
    const int fd = ::open(p.c_str(), O_RDONLY);
    if (fd < 0) return false;
    const bool ok = ::fsync(fd) == 0;
    ::close(fd);
    return ok;
}

bool fsyncDir(const std::string& d) {
    const int fd = ::open(d.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) return false;
    const bool ok = ::fsync(fd) == 0;
    ::close(fd);
    return ok;
}

bool atomicReplace(const std::string& from, const std::string& to) { return ::rename(from.c_str(), to.c_str()) == 0; }

bool hardLink(const std::string& existing, const std::string& linkPath) {
    return ::link(existing.c_str(), linkPath.c_str()) == 0;
}

bool remove(const std::string& p) { return ::unlink(p.c_str()) == 0 || errno == ENOENT; }

bool makeDirs(const std::string& d) {
    std::string cur;
    for (size_t i = 0; i <= d.size(); ++i) {
        if (i == d.size() || d[i] == '/') {
            if (!cur.empty() && ::mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
        if (i < d.size()) cur += d[i];
    }
    struct stat st;
    return ::stat(d.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::vector<std::string> list(const std::string& d) {
    std::vector<std::string> out;
    DIR* dir = ::opendir(d.c_str());
    if (!dir) return out;
    while (dirent* e = ::readdir(dir)) {
        const std::string n = e->d_name;
        if (n != "." && n != "..") out.push_back(n);
    }
    ::closedir(dir);
    std::sort(out.begin(), out.end());
    return out;
}

bool readSmall(const std::string& p, std::string& out, size_t maxBytes) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    out.assign(maxBytes + 1, '\0');
    const size_t n = std::fread(&out[0], 1, maxBytes + 1, f);
    const bool err = std::ferror(f) != 0;
    std::fclose(f);
    if (err || n > maxBytes) return false;
    out.resize(n);
    return true;
}

bool writeDurable(const std::string& p, const std::string& bytes) {
    const int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    size_t off = 0;
    while (off < bytes.size()) {
        const ssize_t n = ::write(fd, bytes.data() + off, bytes.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            return false;
        }
        off += static_cast<size_t>(n);
    }
    const bool ok = ::fsync(fd) == 0;
    return ::close(fd) == 0 && ok;
}

std::string join(const std::string& d, const std::string& n) {
    if (d.empty()) return n;
    return d.back() == '/' ? d + n : d + "/" + n;
}

DirLock::DirLock(const std::string& dir) {
    fd_ = ::open(join(dir, "LOCK").c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ >= 0 && ::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

DirLock::~DirLock() {
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
    }
}

}  // namespace strata::save::fs
