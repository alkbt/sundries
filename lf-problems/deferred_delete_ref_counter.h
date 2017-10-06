#pragma once

#include <thread>
#include <atomic>

using namespace std;

struct SharedBase {
    SharedBase() = default;

    virtual ~SharedBase() {}

    SharedBase * next = {nullptr};

    atomic<long> ref_count = {0};
    atomic<bool> in_garbage = {false};
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
    void release(SharedBase * object);
    void set(SharedBase * object, bool acquire = false);

private:
    atomic<SharedBase *> data{nullptr};
    SharedGarbageCollector& garbage_collector;
};

class AutoSharedObject {
public:
    AutoSharedObject(SharedObject& shared_object)
            : shared_object{shared_object},
              object{shared_object.acquire()}  {
    }

    ~AutoSharedObject() {
        if (object)
            shared_object.release(object);
    }

    SharedBase * get_object() {
        return object;
    }
private:
    SharedObject& shared_object;
    SharedBase * object;
};