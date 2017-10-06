#include "deferred_delete_ref_counter.h"

#include "spin_lock.h"

using namespace std;

DeferredDeleter::~DeferredDeleter()
{
    shutdown.store(true, memory_order_release);
    deleter_thread.join();
}

void DeferredDeleter::insert(SharedBase * node)
{
    node->next = deferred_delete_list.load(memory_order_acquire);
    while (!deferred_delete_list.compare_exchange_weak(node->next, node,
                                                    memory_order_acq_rel));
}

bool DeferredDeleter::delete_list(SharedBase * head)
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

void DeferredDeleter::deleter()
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

SharedObject::~SharedObject()
{
    set(nullptr);
}

SharedBase * SharedObject::acquire()
{
    deferred_deleter.acquires_count.fetch_add(1, memory_order_acq_rel);

    SharedBase * object = data.load(memory_order_consume);
    if (object)
        object->ref_count.fetch_add(1, memory_order_acq_rel);

    deferred_deleter.acquires_count.fetch_sub(1, memory_order_acq_rel);

    return object;
}

void SharedObject::release(SharedBase * object)
{
    if (!object)
        return;

    object->ref_count.fetch_sub(1, memory_order_acq_rel);
}

void SharedObject::set(SharedBase * object)
{
    if (object)
        ++object->shared_objects_count;

    SharedBase * old_object = data.exchange(object, memory_order_acq_rel);

    if (old_object && !--old_object->shared_objects_count)
        deferred_deleter.insert(old_object);
}
