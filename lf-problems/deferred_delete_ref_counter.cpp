#include "deferred_delete_ref_counter.h"
#include "spin_lock.h"

using namespace std;

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

SharedGarbageCollector::~SharedGarbageCollector()
{
    shutdown.store(true, memory_order_release);
    deleter_thread.join();
}

void SharedGarbageCollector::insert(SharedBase * node)
{
    node->next = deferred_delete_list.load(memory_order_acquire);
    while (!deferred_delete_list.compare_exchange_weak(node->next, node,
                                                    memory_order_acq_rel));
}

bool SharedGarbageCollector::delete_list(SharedBase * head)
{
    bool live_objects = false;

    SharedBase * item = head;
    while (item) {
        SharedBase * next = item->next;

        if (item->ref_count.load(memory_order_acquire)) {
            live_objects = true;
            insert(item);
        } else {
            delete item;
        }

        item = next;
    }

    return live_objects;
}

void SharedGarbageCollector::deleter()
{
    while (!shutdown) {
        SharedBase * local_list = deferred_delete_list.exchange(nullptr,
                                                    memory_order_acq_rel);
        if (!local_list) {
            this_thread::sleep_for(chrono::milliseconds(10));
            continue;
        }

        AdaptiveWait wait(100);
        while (acquires_count)
            wait();

        delete_list(local_list);
    }

    while (deferred_delete_list.load(memory_order_acquire)) {
        if (delete_list(deferred_delete_list.exchange(nullptr)))
            this_thread::sleep_for(chrono::milliseconds(10));
    }
}

SharedGarbageCollector garbage_collector;

SharedObject::~SharedObject()
{
    set(nullptr);
}

SharedBase * SharedObject::acquire()
{
    garbage_collector.acquires_count.fetch_add(1, memory_order_acq_rel);

    SharedBase * object = data.load(memory_order_consume);
    if (object)
        object->ref_count.fetch_add(1, memory_order_acq_rel);

    garbage_collector.acquires_count.fetch_sub(1, memory_order_acq_rel);

    return object;
}

void SharedObject::set(SharedBase * object)
{
    if (object) {
        object->ref_count.fetch_add(1, memory_order_acq_rel);
    }

    SharedBase * old_object = data.exchange(object, memory_order_acq_rel);

    if (old_object) {
        bool expected = false;
        if (old_object->in_garbage.compare_exchange_strong(expected, true))
            garbage_collector.insert(old_object);

        old_object->ref_count.fetch_sub(1, memory_order_acq_rel);
    }
}
