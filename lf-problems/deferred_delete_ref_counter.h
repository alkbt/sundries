#pragma once

#include <thread>
#include <atomic>

using namespace std;

struct SharedBase {
    SharedBase() = default;

    virtual ~SharedBase() {}

    SharedBase * next = {nullptr};

    atomic<long> ref_count = {0};
    atomic<long> shared_objects_count = {0};
};

class DeferredDeleter {
public:
    DeferredDeleter() = default;

    ~DeferredDeleter();

    DeferredDeleter(const DeferredDeleter&) = delete;
    DeferredDeleter(DeferredDeleter&&) = delete;
    DeferredDeleter& operator=(const DeferredDeleter&) = delete;
    DeferredDeleter& operator=(DeferredDeleter&&) = delete;

    void insert(SharedBase * object);

    atomic<long> acquires_count{0};

private:
    atomic<SharedBase *> deferred_delete_list{nullptr};
    atomic<bool> shutdown{false};
    thread deleter_thread{thread(&DeferredDeleter::deleter, this)};

    bool delete_list(SharedBase * head);
    void deleter();
};

class SharedObject {
public:
    SharedObject(DeferredDeleter& deferred_deleter)
                            :deferred_deleter{deferred_deleter} {
    }

    ~SharedObject();

    SharedObject(const SharedObject&) = delete;
    SharedObject(SharedObject&&) = delete;
    SharedObject& operator=(const SharedObject&) = delete;
    SharedObject& operator=(SharedObject&&) = delete;

    SharedBase * acquire();
    void release(SharedBase * object);
    void set(SharedBase * object);

private:
    atomic<SharedBase *> data{nullptr};
    DeferredDeleter& deferred_deleter;
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