#pragma once

#include <thread>
#include <atomic>
#include <chrono>

#include <emmintrin.h>

class AdaptiveWait {
public:
    AdaptiveWait(int theresold = 50): waiting_state{0}, theresold{theresold} {}

    AdaptiveWait(const AdaptiveWait&) = delete;
    AdaptiveWait(AdaptiveWait&&) = delete;
    AdaptiveWait& operator=(const AdaptiveWait&) = delete;
    AdaptiveWait& operator=(AdaptiveWait&&) = delete;

    void operator()() {
        if (waiting_state < theresold)
            _mm_pause();
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        ++waiting_state;
    }
private:
    int theresold;
    int waiting_state;
};

class SpinLock {
public:
    SpinLock():spinLock{false} {}

    SpinLock(const SpinLock&) = delete;
    SpinLock(SpinLock&&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;
    SpinLock& operator=(SpinLock&&) = delete;

    void lock() {
        AdaptiveWait adaptiveWait;
        bool expected;
        for (;;) {
            expected = false;
            if (spinLock.compare_exchange_weak(expected, true,
                                    std::memory_order_acq_rel))
                break;

            adaptiveWait();
        }
    }

    void unlock() {
        spinLock.store(false, std::memory_order_release);
    }
private:
    std::atomic<bool> spinLock;
};

class AutoSpinLock {
public:
    AutoSpinLock(SpinLock& spinLock):spinLock{spinLock} {
        spinLock.lock();
    }
    ~AutoSpinLock() {
        spinLock.unlock();
    }

    AutoSpinLock(const AutoSpinLock&) = delete;
    AutoSpinLock(AutoSpinLock&&) = delete;
    AutoSpinLock& operator=(const AutoSpinLock&) = delete;
    AutoSpinLock& operator=(const AutoSpinLock&&) = delete;
private:
    SpinLock& spinLock;
};