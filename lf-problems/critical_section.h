#pragma once

#if _WIN32

#include <Windows.h>

class CriticalSection {
public:
    CriticalSection();
    ~CriticalSection();

    CriticalSection(const CriticalSection&) = delete;
    CriticalSection(CriticalSection&&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
    CriticalSection& operator=(CriticalSection&&) = delete;

    bool tryLock();
    void lock();
    void unlock();

private:
    volatile LONG refCount = 0;
    volatile DWORD ownerThreadId = 0;
    HANDLE event = nullptr;

    bool tryLockRecursive(DWORD threadId);
    bool tryLockFreeSection(DWORD threadId);
};

class AutoCriticalSection {
public:
    AutoCriticalSection(CriticalSection& cs):cs{cs} {
        cs.lock();
    }

    ~AutoCriticalSection() {
        cs.unlock();
    }

private:
    CriticalSection& cs;
};

#endif