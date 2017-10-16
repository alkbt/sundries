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
            T * deletion_list = deletion_head.exchange(nullptr);

            old_obj_ptr->next = deletion_list;
            deletion_list = old_obj_ptr;

            AdaptiveWait wait(50);
            while (acquires_count)
                wait();

            try_delete_list(deletion_list, true);
        }

        void set(T * obj_ptr) {
            T * old_obj_ptr = object.exchange(obj_ptr);
            T * deletion_list = deletion_head.exchange(nullptr);

            if (!old_obj_ptr && !deletion_list)
                return;

            if (old_obj_ptr) {
                old_obj_ptr->next = deletion_list;
                deletion_list = old_obj_ptr;
            }

            bool delete_later = false;
            AdaptiveWait wait(50, 50);
            while (!delete_later && acquires_count)
                delete_later = wait();

            if (delete_later) {
                return_deletion_list(deletion_list);
                return;
            }

            try_delete_list(deletion_list);
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
        std::atomic<T *> object{nullptr};
        std::atomic<long> acquires_count{0};
        std::atomic<T *> deletion_head{nullptr};

        void insert_to_deletion_list(T * obj_ptr) {
            T * head = deletion_head.load();
            for (;;) {
                obj_ptr->next = head;
                if (deletion_head.compare_exchange_weak(head, obj_ptr))
                    break;
            }
        }

        void return_deletion_list(T * obj_ptr) {
            while (obj_ptr) {
                T * next = static_cast<T *>(obj_ptr->next);
                insert_to_deletion_list(obj_ptr);
                obj_ptr = next;
            }
        }

        void try_delete_list(T * obj_ptr, bool wait_forever = false) {
            while (obj_ptr) {
                T * next = static_cast<T *>(obj_ptr->next);

                bool delete_later = false;
                AdaptiveWait wait(50, wait_forever? 0 : 100);
                while (obj_ptr->ref_count)
                    delete_later = wait();

                if (delete_later)
                    insert_to_deletion_list(obj_ptr);
                else
                    delete obj_ptr;

                obj_ptr = next;
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
