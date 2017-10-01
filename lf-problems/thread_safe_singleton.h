#pragma once

#include <atomic>

template<typename T>
struct Singleton {
    Singleton() = delete;

    static T * get_instance() {
        if (!object.load(std::memory_order_relaxed)) {
            AutoSpinLock lock(creatorsLock);

            if (!object.load(std::memory_order_relaxed)) {
                T * newObject = new T();
                atomic_thread_fence(std::memory_order_release);
                object.store(newObject, std::memory_order_relaxed);
            }
        }

        return object.load(std::memory_order_relaxed);
    }
private:
    static volatile std::atomic<T *> object;
    static SpinLock creatorsLock;
};

template<typename T>
volatile std::atomic<T*> Singleton<T>::object = {nullptr};

template<typename T>
SpinLock Singleton<T>::creatorsLock;