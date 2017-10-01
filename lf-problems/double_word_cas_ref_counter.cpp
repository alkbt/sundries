#include <thread>
#include <iostream>

#include "random.h"

#if _WIN32

#include <Windows.h>

namespace double_word_cas_ref_counter {

    /*
    * Решение с использованием CMPXCHG8B/CMPXCHG16B
    */

#if _WIN64
    bool DoubleWordCas(volatile uintptr_t * destination,
                       uintptr_t exchangeHigh,
                       uintptr_t exchangeLow,
                       uintptr_t *comparandResult)
    {
        return InterlockedCompareExchange128(reinterpret_cast<volatile LONG64 *>(destination),
                                             exchangeHigh, exchangeLow,
                                             reinterpret_cast<LONG64 *>(comparandResult)) != 0;
    }

#else
    bool DoubleWordCas(volatile uintptr_t * destination,
                       uintptr_t exchangeHigh,
                       uintptr_t exchangeLow,
                       uintptr_t *comparandResult)
    {
        volatile LONG64 *destination64bit = reinterpret_cast<volatile LONG64 *>(destination);

        LONG64 exchange = (static_cast<LONG64>(exchangeHigh) << 32) | exchangeLow;
        LONG64 comparand64bit = *reinterpret_cast<volatile LONG64 *>(comparandResult);
        LONG64 result64bit = InterlockedCompareExchange64(destination64bit, exchange, comparand64bit);
        *reinterpret_cast<LONG64 *>(comparandResult) = result64bit;

        return result64bit == comparand64bit;
    }

#endif

    struct Data {
        int a;
        int b;
        int c;

        volatile LONG refCounter;
    };

    struct Base {
        volatile uintptr_t refCounter;
        volatile uintptr_t ptr;
    };

    volatile Base * g_ptr = nullptr;

    Data * acquire()
    {
        Base oldBase;

        for (;;) {
            if (!g_ptr)
                return nullptr;

            oldBase = {0, 0};
            DoubleWordCas(reinterpret_cast<volatile uintptr_t*>(g_ptr),
                          0, 0, reinterpret_cast<uintptr_t *>(&oldBase));


            if (DoubleWordCas(reinterpret_cast<volatile uintptr_t*>(g_ptr),
                              oldBase.ptr, oldBase.refCounter + 1,
                              reinterpret_cast<uintptr_t *>(&oldBase))) {
                break;
            }

            Sleep(20);
        }

        return reinterpret_cast<Data *>(oldBase.ptr);
    }

    void release(Data * object)
    {
        InterlockedDecrement(&object->refCounter);
    }

    void set(Data * object)
    {
        if (object)
            object->refCounter = 0;

        Base * newBase = new Base{0, reinterpret_cast<uintptr_t>(object)};

        Base * oldBase = reinterpret_cast<Base *>(InterlockedExchangePointer(
            (volatile PVOID *)&g_ptr,
            reinterpret_cast<PVOID>(newBase)));

        if (!oldBase)
            return;

        Data * oldObject = reinterpret_cast<Data *>(oldBase->ptr);
        if (oldObject) {
            while (oldBase->refCounter + oldObject->refCounter)
                Sleep(20);

            delete oldObject;
        }

        delete oldBase;
    }

    void writerThread()
    {
        Sleep(get_random(0, 100));

        static int a = 0;
        static int b = 0;

        if (get_random(0, 10) < 3)
            set(nullptr);
        else
            set(new Data{++a, --b, a + b});
    }

    void readerThread()
    {
        Sleep(get_random(0, 100));

        Data * object = acquire();

        if (!object)
            return;

        if (object->a + object->b != object->c)
            std::cout << "ReferernceCounterDoubleWordCas is broken!\n";

        release(object);
    }


    void test()
    {
        const int threadsCount = 10000;

        std::cout << "ReferernceCounterDoubleWordCas test on " << threadsCount << " threads\n";
        std::vector<std::thread> threads;
        for (int i = 0; i < threadsCount; ++i) {
            if (i % 3)
                threads.emplace_back(std::thread(readerThread));
            else
                threads.emplace_back(std::thread(writerThread));
        }

        for (auto& thread : threads)
            thread.join();

        set(nullptr);
    }
}
#endif
