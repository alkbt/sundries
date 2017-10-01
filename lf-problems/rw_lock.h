#pragma once

#if _WIN32

#include <exception>
#include <Windows.h>

class RwLock {
public:
    RwLock() {
        lockState.raw = 0;
    }

    RwLock(const RwLock&) = delete;
    RwLock(RwLock&&) = delete;
    RwLock& operator=(const RwLock&) = delete;
    RwLock& operator=(RwLock&&) = delete;

    void sharedLock() {
        for (;;) {
            LockState oldLockState = lockState;
            if (oldLockState.writerLock) {
                Sleep(20);
                continue;
            }

            MemoryBarrier();

            LockState newLockState = oldLockState;
            ++newLockState.readersCount;

            if (InterlockedCompareExchange(&lockState.raw,
                                           newLockState.raw, oldLockState.raw)
                == oldLockState.raw)
                return;
        }

    }

    void sharedUnlock() {
        for (;;) {
            LockState oldLockState = lockState;

            if (oldLockState.writerLock)
                throw std::exception("RwLock is broken!\n");

            LockState newLockState = oldLockState;
            --newLockState.readersCount;

            if (InterlockedCompareExchange(&lockState.raw,
                                           newLockState.raw, oldLockState.raw)
                == oldLockState.raw)
                return;

            Sleep(20);
        }
    }

    void exclusiveLock() {
        LockState oldLockState;
        oldLockState.raw = 0;

        LockState newLockState;
        newLockState.writerLock = 1;
        newLockState.readersCount = 0;

        while (InterlockedCompareExchange(&lockState.raw, newLockState.raw,
                                          oldLockState.raw) != oldLockState.raw)
            Sleep(20);
    }

    void exclusiveUnlock() {
        if (lockState.readersCount || !lockState.writerLock)
            throw std::exception("RwMutex in broken!\n");

        lockState.raw = 0;
        MemoryBarrier();
    }
private:
    union LockState {
        volatile LONG raw;
        struct {
            volatile ULONG writerLock    : 1;
            volatile ULONG readersCount  : 31;
        };
    };

    LockState lockState;
};

#endif