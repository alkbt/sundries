#pragma once

#include <thread>
#include <atomic>

using namespace std;

struct SharedBase {
    SharedBase() = default;

    virtual ~SharedBase() {}

    SharedBase(const SharedBase&) = delete;
    SharedBase(SharedBase&&) = delete;

    SharedBase& operator=(const SharedBase&) = delete;
    SharedBase& operator=(SharedBase&&) = delete;

    void acquire() {
        ref_count.fetch_add(1, memory_order_acq_rel);
    }

    void release() {
        ref_count.fetch_sub(1, memory_order_acq_rel);
    }

private:
    SharedBase * next = {nullptr};

    atomic<long> ref_count = {0};
    atomic<bool> in_garbage = {false};

    friend class SharedGarbageCollector;
    friend class SharedObject;
};

class SharedGarbageCollector {
public:
    SharedGarbageCollector() = default;

    ~SharedGarbageCollector();

    SharedGarbageCollector(const SharedGarbageCollector&) = delete;
    SharedGarbageCollector(SharedGarbageCollector&&) = delete;
    SharedGarbageCollector& operator=(const SharedGarbageCollector&) = delete;
    SharedGarbageCollector& operator=(SharedGarbageCollector&&) = delete;

    void insert(SharedBase * object);

    atomic<long> acquires_count{0};

private:
    atomic<SharedBase *> deferred_delete_list{nullptr};
    atomic<bool> shutdown{false};
    thread deleter_thread{thread(&SharedGarbageCollector::deleter, this)};

    bool delete_list(SharedBase * head);
    void deleter();
};

class SharedObject {
public:
    SharedObject(SharedGarbageCollector& garbage_collector)
                            :garbage_collector{garbage_collector} {
    }

    ~SharedObject();

    SharedObject(const SharedObject&) = delete;
    SharedObject(SharedObject&&) = delete;
    SharedObject& operator=(const SharedObject&) = delete;
    SharedObject& operator=(SharedObject&&) = delete;

    SharedBase * acquire();
    void set(SharedBase * object);

private:
    atomic<SharedBase *> data{nullptr};
    SharedGarbageCollector& garbage_collector;
};

template<class T = SharedBase>
class AutoSharedObject {
public:
    AutoSharedObject(SharedObject& shared_object)
            : object{dynamic_cast<T*>(shared_object.acquire())}  {
    }

    AutoSharedObject(T * object): object{object} {
        if (object)
            object->acquire();
    }

    ~AutoSharedObject() {
        if (object)
            object->release();
    }

    T* operator->() {
        return object;
    }

    operator T*() {
        return object;
    }
private:
    T * object;
};