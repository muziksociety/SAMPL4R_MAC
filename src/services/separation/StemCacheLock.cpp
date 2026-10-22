#include "StemCacheLock.h"

#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace samplar {

std::wstring toLongPathIfNeeded (const std::wstring& p)
{
#ifdef _WIN32
    if (p.size() < MAX_PATH - 12 || p.rfind (L"\\\\?\\", 0) == 0)
        return p;
    if (p.rfind (L"\\\\", 0) == 0)                       // UNC: \\server\share -> \\?\UNC\server\share
        return L"\\\\?\\UNC\\" + p.substr (2);
    return L"\\\\?\\" + p;
#else
    return p;
#endif
}

#ifdef _WIN32

CrossProcessFileLock::CrossProcessFileLock (std::wstring lockFilePath) : path_ (std::move (lockFilePath)) {}

CrossProcessFileLock::~CrossProcessFileLock()
{
    unlock();
    if (handle_ != nullptr)
        CloseHandle ((HANDLE) handle_);
}

CrossProcessFileLock::Try CrossProcessFileLock::tryLock()
{
    if (locked_)
        return Try::Acquired;
    if (handle_ == nullptr)
    {
        HANDLE h = CreateFileW (toLongPathIfNeeded (path_).c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_HIDDEN, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            lastError_ = GetLastError();
            return Try::Error;
        }
        handle_ = h;
    }
    OVERLAPPED ov {};
    if (LockFileEx ((HANDLE) handle_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov))
    {
        locked_ = true;
        return Try::Acquired;
    }
    lastError_ = GetLastError();
    return lastError_ == ERROR_LOCK_VIOLATION || lastError_ == ERROR_IO_PENDING ? Try::Busy : Try::Error;
}

void CrossProcessFileLock::unlock()
{
    if (! locked_ || handle_ == nullptr)
        return;
    OVERLAPPED ov {};
    UnlockFileEx ((HANDLE) handle_, 0, 1, 0, &ov);
    locked_ = false;
}

bool LeaseFile::acquire (const std::wstring& path)
{
    release();
    HANDLE h = CreateFileW (toLongPathIfNeeded (path).c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
                            FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    handle_ = h;
    return true;
}

void LeaseFile::release()
{
    if (handle_ != nullptr)
    {
        CloseHandle ((HANDLE) handle_);   // FILE_FLAG_DELETE_ON_CLOSE removes the file
        handle_ = nullptr;
    }
}

bool LeaseFile::isLeaseLive (const std::wstring& path)
{
    const std::wstring p = toLongPathIfNeeded (path);
    if (GetFileAttributesW (p.c_str()) == INVALID_FILE_ATTRIBUTES)
        return false;                                      // no lease file
    if (DeleteFileW (p.c_str()))
        return false;                                      // nobody held it: stale leftover removed
    const DWORD e = GetLastError();
    return e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED;
}

#else

CrossProcessFileLock::CrossProcessFileLock (std::wstring lockFilePath) : path_ (std::move (lockFilePath)) {}
CrossProcessFileLock::~CrossProcessFileLock() = default;
CrossProcessFileLock::Try CrossProcessFileLock::tryLock() { locked_ = true; return Try::Acquired; }
void CrossProcessFileLock::unlock() { locked_ = false; }
bool LeaseFile::acquire (const std::wstring&) { return true; }
void LeaseFile::release() {}
bool LeaseFile::isLeaseLive (const std::wstring&) { return false; }

#endif

} // namespace samplar
