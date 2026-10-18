// The few durable file-system primitives the save protocol needs. Every one
// reports failure instead of ignoring it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace strata::save::fs {

bool exists(const std::string& path);           // a regular file
bool fsyncFile(const std::string& path);        // flush file data to stable storage
bool fsyncDir(const std::string& dir);          // make renames/links in `dir` durable
bool atomicReplace(const std::string& from, const std::string& to);  // rename(2): `to` is never absent
bool hardLink(const std::string& existing, const std::string& linkPath);
bool remove(const std::string& path);           // true if gone afterwards
bool makeDirs(const std::string& dir);
std::vector<std::string> list(const std::string& dir);  // file names (not paths), sorted
bool readSmall(const std::string& path, std::string& out, size_t maxBytes);  // false if larger
bool writeDurable(const std::string& path, const std::string& bytes);       // write + fsync
std::string join(const std::string& dir, const std::string& name);

// Exclusive advisory lock on <dir>/LOCK for the life of the object, so two
// processes can never write one save slot at once.
class DirLock {
public:
    explicit DirLock(const std::string& dir);
    ~DirLock();
    DirLock(const DirLock&) = delete;
    DirLock& operator=(const DirLock&) = delete;
    bool held() const { return fd_ >= 0; }

private:
    int fd_ = -1;
};

}  // namespace strata::save::fs
