#if _WIN32

#include <exception>

#include "critical_section.h"

CriticalSection::CriticalSection()
{
    event = CreateEvent(nullptr, false, false, nullptr);
    if (!event)
        throw std::exception("CreateEvent Error!");
}

CriticalSection::~CriticalSection()
{
    if (event)
        CloseHandle(event);
}

bool CriticalSection::tryLock()
{
    DWORD threadId = GetCurrentThreadId();

    if (tryLockRecursive(threadId))
        return true;

    return tryLockFreeSection(threadId);
}

void CriticalSection::lock()
{
    DWORD threadId = GetCurrentThreadId();

    if (tryLockRecursive(threadId))
        return;

    for (;;) {
        if (tryLockFreeSection(threadId))
            return;

        DWORD waitResult = WaitForSingleObject(event, INFINITE);
        if (waitResult != WAIT_OBJECT_0) {
            throw std::exception("CriticalSection can't wait");
        }
    }
}

void CriticalSection::unlock()
{
    DWORD threadId = GetCurrentThreadId();
    if (threadId != ownerThreadId)
        return;

    if (!InterlockedDecrement(&refCount)) {
        ownerThreadId = 0;
        MemoryBarrier();

        SetEvent(event);
    }
}

bool CriticalSection::tryLockRecursive(DWORD threadId)
{
    if (threadId == ownerThreadId) {
        InterlockedIncrement(&refCount);
        return true;
    }

    return false;
}

bool CriticalSection::tryLockFreeSection(DWORD threadId)
{
    if (!InterlockedCompareExchange(&ownerThreadId, threadId, 0)) {
        refCount = 1;
        MemoryBarrier();

        return true;
    }

    return false;
}

#endif