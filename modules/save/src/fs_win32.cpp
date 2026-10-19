// Windows implementation. Paths are UTF-8 and converted to UTF-16 for every
// call; '/' separators are accepted by the Win32 file APIs.
//
// Durability: MoveFileExW with MOVEFILE_WRITE_THROUGH does not return until
// the rename is flushed, so fsyncDir has nothing left to flush and only checks
// the directory exists. Rename and delete fail on a file that is still open or
// memory-mapped (usdc crate files are mapped), so every stage over a temp file
// must be released before it is renamed or pruned.
#include "strata/save/fs.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace strata::save::fs {

namespace {

std::wstring wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n =
        ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

// Share everything so other handles (hashing, composing, the async writer) can
// still read; FILE_SHARE_DELETE lets a later rename or delete proceed.
constexpr DWORD kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

}  // namespace

bool exists(const std::string& p) {
    const DWORD a = ::GetFileAttributesW(wide(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool fsyncFile(const std::string& p) {
    // FlushFileBuffers needs GENERIC_WRITE.
    HANDLE h = ::CreateFileW(wide(p).c_str(), GENERIC_WRITE, kShareAll, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const bool ok = ::FlushFileBuffers(h) != 0;
    ::CloseHandle(h);
    return ok;
}

bool fsyncDir(const std::string& d) {
    const DWORD a = ::GetFileAttributesW(wide(d).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool atomicReplace(const std::string& from, const std::string& to) {
    return ::MoveFileExW(wide(from).c_str(), wide(to).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) !=
           0;
}

bool hardLink(const std::string& existing, const std::string& linkPath) {
    return ::CreateHardLinkW(wide(linkPath).c_str(), wide(existing).c_str(), nullptr) != 0;
}

bool remove(const std::string& p) {
    if (::DeleteFileW(wide(p).c_str())) return true;
    const DWORD e = ::GetLastError();
    return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND;
}

bool makeDirs(const std::string& d) {
    std::error_code ec;
    const std::filesystem::path path(wide(d));
    std::filesystem::create_directories(path, ec);
    return std::filesystem::is_directory(path, ec);
}

std::vector<std::string> list(const std::string& d) {
    std::vector<std::string> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(std::filesystem::path(wide(d)), ec), end; !ec && it != end;
         it.increment(ec))
        out.push_back(narrow(it->path().filename().wstring()));
    std::sort(out.begin(), out.end());
    return out;
}

bool readSmall(const std::string& p, std::string& out, size_t maxBytes) {
    HANDLE h = ::CreateFileW(wide(p).c_str(), GENERIC_READ, kShareAll, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    out.assign(maxBytes + 1, '\0');
    size_t n = 0;
    bool err = false;
    while (n < out.size()) {
        const DWORD want = static_cast<DWORD>(std::min<size_t>(out.size() - n, 1u << 30));
        DWORD got = 0;
        if (!::ReadFile(h, &out[n], want, &got, nullptr)) {
            err = true;
            break;
        }
        if (got == 0) break;
        n += got;
    }
    ::CloseHandle(h);
    if (err || n > maxBytes) return false;
    out.resize(n);
    return true;
}

bool writeDurable(const std::string& p, const std::string& bytes) {
    HANDLE h = ::CreateFileW(wide(p).c_str(), GENERIC_WRITE, kShareAll, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    size_t off = 0;
    while (off < bytes.size()) {
        const DWORD want = static_cast<DWORD>(std::min<size_t>(bytes.size() - off, 1u << 30));
        DWORD wrote = 0;
        if (!::WriteFile(h, bytes.data() + off, want, &wrote, nullptr)) {
            ::CloseHandle(h);
            return false;
        }
        off += wrote;
    }
    const bool ok = ::FlushFileBuffers(h) != 0;
    return ::CloseHandle(h) != 0 && ok;
}

std::string join(const std::string& d, const std::string& n) {
    if (d.empty()) return n;
    return d.back() == '/' || d.back() == '\\' ? d + n : d + "/" + n;
}

DirLock::DirLock(const std::string& dir) {
    HANDLE h = ::CreateFileW(wide(join(dir, "LOCK")).c_str(), GENERIC_READ | GENERIC_WRITE, kShareAll, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    OVERLAPPED ov{};
    if (!::LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
        ::CloseHandle(h);
        return;
    }
    handle_ = reinterpret_cast<std::intptr_t>(h);
}

DirLock::~DirLock() {
    if (handle_ < 0) return;
    HANDLE h = reinterpret_cast<HANDLE>(handle_);
    OVERLAPPED ov{};
    ::UnlockFileEx(h, 0, 1, 0, &ov);
    ::CloseHandle(h);
}

}  // namespace strata::save::fs
