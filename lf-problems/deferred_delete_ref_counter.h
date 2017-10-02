#pragma once

#include <thread>
#include <atomic>

using namespace std;

struct SharedBase {
    SharedBase() = default;

    virtual ~SharedBase() {}

    SharedBase * next = {nullptr};

    atomic<long> ref_count = {0};
};

class DeferredDeleteList {
public:
    DeferredDeleteList() = default;

    ~DeferredDeleteList();

    DeferredDeleteList(const DeferredDeleteList&) = delete;
    DeferredDeleteList(DeferredDeleteList&&) = delete;
    DeferredDeleteList& operator=(const DeferredDeleteList&) = delete;
    DeferredDeleteList& operator=(DeferredDeleteList&&) = delete;

    void insert(SharedBase * object);

    atomic<long> acquires_count{0};

private:
    atomic<SharedBase *> deferred_delete_list{nullptr};
    atomic<bool> shutdown{false};
    thread deleter_thread{thread(&DeferredDeleteList::deleter, this)};

    bool delete_list(SharedBase * head);
    void deleter();
};

class SharedObject {
public:
    SharedObject() = default;

    SharedObject(const SharedObject&) = delete;
    SharedObject(SharedObject&&) = delete;
    SharedObject& operator=(const SharedObject&) = delete;
    SharedObject& operator=(SharedObject&&) = delete;

    SharedBase * acquire();
    void release(SharedBase * object);
    void set(SharedBase * object);

private:
    atomic<SharedBase *> data{nullptr};
    DeferredDeleteList deferred_delete_list;
};
