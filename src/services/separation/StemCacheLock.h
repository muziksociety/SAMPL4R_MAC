#pragma once

#include <string>

namespace samplar {

/**
 * Cross-process primitives for the persistent stem cache (audit S-H1). Win32 only (no-ops
 * that always succeed elsewhere); kept out of JUCE translation units.
 *
 * CrossProcessFileLock - exclusive LockFileEx on a lock file. Works between threads of one
 * process too (each instance owns its own handle). Released by unlock(), the destructor, or
 * by Windows when the owning process dies (no stale lock after a crash).
 *
 * LeaseFile - "this process is using X". The holder keeps the file open with
 * FILE_FLAG_DELETE_ON_CLOSE and without FILE_SHARE_DELETE, so the file disappears when the
 * holder releases it or dies, and isLeaseLive() (a delete attempt) fails while it is held.
 */
class CrossProcessFileLock
{
public:
    explicit CrossProcessFileLock (std::wstring lockFilePath);
    ~CrossProcessFileLock();
    CrossProcessFileLock (const CrossProcessFileLock&) = delete;
    CrossProcessFileLock& operator= (const CrossProcessFileLock&) = delete;

    enum class Try { Acquired, Busy, Error };

    /** Non-blocking attempt. Error = the lock file cannot be opened (caller decides). */
    Try tryLock();
    void unlock();
    bool isLocked() const noexcept { return locked_; }
    unsigned long lastError() const noexcept { return lastError_; }

private:
    std::wstring path_;
    void* handle_ = nullptr;
    bool locked_ = false;
    unsigned long lastError_ = 0;
};

class LeaseFile
{
public:
    LeaseFile() = default;
    ~LeaseFile() { release(); }
    LeaseFile (const LeaseFile&) = delete;
    LeaseFile& operator= (const LeaseFile&) = delete;

    bool acquire (const std::wstring& path);
    void release();
    bool isHeld() const noexcept { return handle_ != nullptr; }

    /** True while another live holder keeps `path` open. A leftover file nobody holds
        (power loss) is deleted and reported as not live. */
    static bool isLeaseLive (const std::wstring& path);

private:
    void* handle_ = nullptr;
};

/** "\\?\"-prefixed absolute path for Win32 calls when the path is >= MAX_PATH (no-op otherwise). */
std::wstring toLongPathIfNeeded (const std::wstring& absolutePath);

} // namespace samplar
