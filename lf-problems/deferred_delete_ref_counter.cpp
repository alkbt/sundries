#include <chrono>
#include <iostream>
#include <vector>
#include <string>

#include "spin_lock.h"
#include "random.h"

#include "deferred_delete_ref_counter.h"

using namespace std;

DeferredDeleteList::~DeferredDeleteList()
{
    shutdown.store(true, memory_order_release);
    deleter_thread.join();
}

void DeferredDeleteList::insert(SharedBase * node)
{
    node->next = deferred_delete_list.load(memory_order_acquire);
    while (!deferred_delete_list.compare_exchange_weak(node->next, node,
                                                        memory_order_acq_rel));
}

void DeferredDeleteList::delete_list(SharedBase * head)
{
    SharedBase * item = head;
    while (item) {
        SharedBase * next = item->next;

        if (item->ref_count.load(memory_order_acquire)) {
            insert(item);
        } else {
            delete item;
        }

        item = next;
    }
}

void DeferredDeleteList::deleter()
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
        delete_list(deferred_delete_list.exchange(nullptr));
    }

}

SharedBase * SharedObject::acquire()
{
    deferred_delete_list.acquires_count.fetch_add(1, memory_order_acq_rel);

    SharedBase * object = data.load(memory_order_consume);
    if (object)
        object->ref_count.fetch_add(1, memory_order_acq_rel);

    deferred_delete_list.acquires_count.fetch_sub(1, memory_order_acq_rel);

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
    if (object) {
        object->ref_count.store(0, memory_order_relaxed);
    }

    SharedBase * old_object = data.exchange(object, memory_order_acq_rel);

    if (old_object)
        deferred_delete_list.insert(old_object);
}

