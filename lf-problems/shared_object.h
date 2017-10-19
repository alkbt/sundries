#pragma once

#include <atomic>
#include "spin_lock.h"

namespace shared_object {

    class SharedObjectBase {
    public:
        virtual ~SharedObjectBase() {}

        void acquire() {
            ++ref_count;
        }

        void release() {
            --ref_count;
        }

    private:
        std::atomic<long> ref_count{0};

        SharedObjectBase * next{nullptr};

        template <class T> friend class SharedObject;
    };

    template <class T>
    class SharedObject {
    public:
        ~SharedObject() {
            T * old_obj_ptr = object.exchange(nullptr);
            try_to_delete_objects(old_obj_ptr, true);
        }

        void set(T * obj_ptr) {
            T * old_obj_ptr = object.exchange(obj_ptr);
            try_to_delete_objects(old_obj_ptr);
        }

        T * acquire() {
            ++acquires_count;

            T * obj_ptr = object.load();

            if (obj_ptr)
                obj_ptr->acquire();

            --acquires_count;

            return obj_ptr;
        }
    private:
        using ListElement = decltype(T::next);

        std::atomic<T *> object{nullptr};
        std::atomic<long> acquires_count{0};
        std::atomic<ListElement> deletion_head{nullptr};

        void insert_to_deletion_list(ListElement obj_ptr) {
            obj_ptr->next = deletion_head;
            while (!deletion_head.compare_exchange_weak(obj_ptr->next, obj_ptr))
                _mm_pause();
        }

        void return_deletion_list(ListElement obj_ptr) {
            while (obj_ptr) {
                ListElement next = obj_ptr->next;
                insert_to_deletion_list(obj_ptr);
                obj_ptr = next;
            }
        }

        void try_to_delete_objects(T * obj_ptr, bool wait_forever = false) {
            ListElement deletion_list = deletion_head.exchange(nullptr);

            if (!obj_ptr && !deletion_list)
                return;

            if (obj_ptr) {
                obj_ptr->next = deletion_list;
                deletion_list = obj_ptr;
            }

            bool delete_later = false;
            AdaptiveWait wait(15, wait_forever? 0 : 20);
            while (!delete_later && acquires_count)
                delete_later = wait();

            if (delete_later) {
                return_deletion_list(deletion_list);
                return;
            }

            while (deletion_list) {
                ListElement next = deletion_list->next;

                delete_later = false;
                wait.reset(15, wait_forever? 0 : 20);
                while (!delete_later && deletion_list->ref_count)
                    delete_later = wait();

                if (delete_later)
                    insert_to_deletion_list(deletion_list);
                else
                    delete deletion_list;

                deletion_list = next;
            }
        }
    };


    template<class T>
    class AutoSharedObject {
    public:
        AutoSharedObject(SharedObject<T>& shared_object)
            : object{shared_object.acquire()} {
        }

        AutoSharedObject(T * object): object{object} {
            if (object)
                object->acquire();
        }

        AutoSharedObject(const AutoSharedObject&) = delete;
        AutoSharedObject(AutoSharedObject&&) = delete;

        AutoSharedObject& operator=(const AutoSharedObject&) = delete;
        AutoSharedObject& operator=(AutoSharedObject&&) = delete;

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
}
