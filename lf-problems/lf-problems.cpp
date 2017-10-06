// lf-problems.cpp : Defines the entry point for the console application.
//
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <random>

#include "random.h"
#include "spin_lock.h"

#include "state_transition.h"
#include "producer_consumer.h"
#include "critical_section.h"
#include "rcu.h"
#include "rw_lock.h"
#include "thread_safe_singleton.h"
#include "deferred_delete_ref_counter.h"
#include "double_word_cas_ref_counter.h"

#if _WIN32
#include <Windows.h>
#endif

using namespace std;

/*
2. State Transition.

Объект имеет четыре состояния: NoData, ProducingData, DataReady и ConsumingData.

X пишущих потоков приходят с данными, но захватить блокировку может только один,
он переводит объект из NoData в ProducingData и начинает запись данных.
Когда данные готовы, объект переводится в DataReady.

Y потоков-читателей конкурируют за получение данных, но выигрывает также
только один из них, он переводит объект из DataReady в ConsumingData и
читает данные. После этого объект опять становится NoData и цикл повторяется.

Реализовать без блокировок и ожиданий.
*/

namespace state_transition {

    StateTransion state;
    std::vector<int> data;
    int checkSum = 0;

    const int testThreadsCount = 50;
    const int iterationsCount = 1000;

    void reader()
    {
        this_thread::sleep_for(chrono::milliseconds(get_random(0, 100)));

        for (auto i = 0; i < iterationsCount; ++i) {
            if (!state.AcquireForRead())
                continue;

            int l_checkSum = 0;
            for (const auto x : data) {
                l_checkSum += x;
            }

            if (l_checkSum != checkSum)
                std::cout << "StateTransion object is broken!\n";

            state.Release();
        }
    }

    void writer()
    {
        this_thread::sleep_for(chrono::milliseconds(get_random(0, 100)));

        if (!state.AcquireForWrite())
            return;

        auto dataSize = get_random(0, 50);
        data.clear();
        checkSum = 0;
        for (auto i = 0; i < dataSize; ++i) {
            int x = get_random(0, 100);
            checkSum += x;
            data.push_back(x);
        }

        state.Release();
    }

    void test()
    {
        std::cout << "StateTransition test on " << testThreadsCount << " threads\n";

        std::vector<std::thread> threads;
        for (auto i = 0; i < testThreadsCount; ++i)
            threads.emplace_back(std::thread(i % 2 ? reader : writer));

        for (auto& thread : threads)
            thread.join();
    }

}

/*
3. Producer - Consumer.

Поток 1 готовит данные и выставляет флаг ready = true.
Поток 2 в цикле ждет флага ready и по приходу забирает данные.
Реализовать без блокировок и ожиданий.
*/
namespace producer_consumer {
    ProducerConsumer producer_consumer;

    int data = 0;
    const int iterationsCount = 100;

    void producer_thread()
    {
        for (auto i = 0; i < iterationsCount; ++i) {
            producer_consumer.wait_for_data_consumed();

            data = i;

            producer_consumer.set_data_ready();
        }
    }

    void consumer_thread()
    {
        for (auto i = 0; i < iterationsCount; ++i) {
            producer_consumer.wait_for_data_ready();

            if (data != i)
                std::cout << "Corrupted Data!\n";

            producer_consumer.set_data_consumed();
        }
    }

    void test()
    {
        std::cout << "ProducerConsumer test on " << iterationsCount << " iterations\n";
        std::thread producer(producer_thread), consumer(consumer_thread);
        producer.join();
        consumer.join();
    }
}

/*
4. Thread-Safe Singleton.

Реализовать потокобезопасный синглтон с double checked locking.

Пример:

Singleton<Data>::getInstance()->someMethod();

Инициализация выполняется при первом вызове getInstance().
Издержки на повторный доступ к объекту должны быть минимальны.
*/

namespace thread_safe_singleton {

    class TestObject {
    public:

        TestObject() {
            creation_counter.fetch_add(1, memory_order_acq_rel);
        }

        long getCreationCounter() {
            return creation_counter.load(memory_order_acquire);
        }
    private:
        static atomic<long> creation_counter;
    };

    atomic<long> TestObject::creation_counter{0};

    void singleton_thread()
    {
        auto o = Singleton<TestObject>::get_instance();
        if (o->getCreationCounter() != 1) {
            std::cout << "Singleton multiple creation error!\n";
        }
    }

    void test()
    {
        const int testThreadsCount = 100;
        std::cout << "ThreadSafeSingleton test on " << testThreadsCount << " threads\n";

        std::vector<std::thread> threads;
        for (auto i = 0; i < testThreadsCount; ++i)
            threads.emplace_back(std::thread(singleton_thread));

        for (auto& thread : threads)
            thread.join();
    }
}

/*
6. Critical Section.

Реализуйте критическую секцию по аналогии с Win32:

- методы lock, unlock и tryLock;

- захват свободной секции не должен приводить к ожиданию (WaitForXxx,
Sleep, etc) или сисемным вызовам (syscall);

- попытка захвата занятой секции - уход в ожидание на объекте ядра;

- должна быть поддержка рекурсивного захвата.
*/

#if _WIN32
namespace critical_section {

    CriticalSection cs;
    DWORD cs_data = 0;

    DWORD recursive_cs_check()
    {
        AutoCriticalSection lock(cs);

        DWORD local_data = cs_data;
        if ((get_random(0, 100) < 80) && (local_data != recursive_cs_check()))
            throw std::exception("CriticalSection's algorithm is broken!");

        return local_data;
    }

    void critical_section_thread()
    {
        Sleep(get_random(0, 100));
        try {
            AutoCriticalSection lock(cs);

            DWORD local_data = get_random(0, 100);
            cs_data = local_data;

            if (local_data != recursive_cs_check())
                throw std::exception("CriticalSection's algorithm is broken!");
        } catch (std::exception e) {
            std::cout << e.what() << std::endl;
        }
    }

    void test()
    {
        const int test_threads_count = 10000;
        std::cout << "CriticalSection test on "<< test_threads_count << " threads\n";

        std::vector<std::thread> threads;
        for (auto i = 0; i < test_threads_count; ++i)
            threads.emplace_back(std::thread(critical_section_thread));

        for (auto& thread : threads)
            thread.join();
    }
}
#endif

/*
7. Read-Copy Update (RCU), упрощенный вариант.

Реализуйте механизм разделяемого владения объектом между писателем (writer) и
читателями (readers):

Читатели работают с первой копией объекта. Писатель изменяет вторую копию
объекта, а затем меняет копии местами.

Работа читателей должна быть полностью неблокирующей (lock-free).

Пример:

struct Data
{
int a;
int b;
int c;
};

Rcu<Data> g_Data;

// Читатель:
value_t val = g_Data->get();

// Писатель:
value_t newVal;
newVal.a = 123;
newVal.b = 456;
newVal.c = 789;
g_Data->setNew(newVal);
*/

namespace read_copy_update {
    struct Data {
        Data(int a = 0, int b = 0): a{a}, b{b}, c{a + b} {}
        int a;
        int b;
        int c;
    };

    Rcu<Data> rcu;
    static atomic<int> a = {0};
    static atomic<int> b = {0};

    void writer()
    {
        this_thread::sleep_for(chrono::milliseconds(get_random(0, 100)));
        for (unsigned long long i = 0; i < 1000; ++i) {
            Data data{--a, ++b};

            rcu.set(data);
        }
    }

    void reader()
    {
        this_thread::sleep_for(chrono::milliseconds(get_random(0, 100)));
        for (unsigned long long i = 0; i < 1000; ++i) {
            Data data = rcu.get();
            if (data.a + data.b != data.c)
                std::cout << "Rcu is broken!\n";
        }
    }

    void test()
    {
        const int testThreadsCount = 100;
        std::cout << "ReadCopyUpdate test on " << testThreadsCount << " threads\n";

        std::vector<std::thread> threads;
        for (auto i = 0; i < testThreadsCount; ++i)
            if (!(i % 3))
                threads.emplace_back(std::thread(writer));
            else
                threads.emplace_back(std::thread(reader));

        for (auto& thread : threads)
            thread.join();
    }
}

/*
8. Readers-Writer Lock.

Реализуйте механизм разделяемого владения объектом между писателями
(writers) и читателями (readers).

Работа читателей должна быть полностью неблокирующей (lock-free).

Пример:

RwMutex rw;

// Читатель
rw.sharedLock();
// Работаем с данными на чтение.
rw.sharedUnlock();

// Писатель
rw.exclusiveLock();
// Изменяем данные.
rw.exclusiveUnlock();
*/

#if _WIN32
namespace readers_writer_lock {

    struct Data {
        int a;
        int b;
        int c;
    };
    Data data{};
    RwLock lock;

    void reader()
    {
        this_thread::sleep_for(chrono::milliseconds(get_random(0, 1500)));

        try {
            lock.sharedLock();

            if (data.a + data.b != data.c)
                throw std::exception("RwLock is broken!\n");

            lock.sharedUnlock();
        } catch (std::exception e) {
            std::cout << e.what() << std::endl;
        }
    }

    void writer()
    {
        this_thread::sleep_for(chrono::milliseconds(get_random(0, 1500)));

        try {
            lock.exclusiveLock();

            static int a = 0;
            static int b = 0;

            data.a = ++a;
            data.b = --b;
            data.c = data.a + data.b;

            lock.exclusiveUnlock();
        } catch (std::exception e) {
            std::cout << e.what() << std::endl;
        }
    }

    void test()
    {
        const int test_threads_count = 10000;
        std::cout << "ReadersWriterLock test on " << test_threads_count << " threads\n";

        std::vector<std::thread> threads;
        for (auto i = 0; i < test_threads_count; ++i)
            if (!(i % 3))
                threads.emplace_back(std::thread(writer));
            else
                threads.emplace_back(std::thread(reader));

        for (auto& thread : threads)
            thread.join();
    }
}
#endif

/*
12. Reference Counter.

Реализуйте разделяемое владение объектом между писателями (writers) и
читателями (readers), построенное на основе подсчета ссылок:

struct DataObject
{
LONG RefCount;

// Данные
};

DataObject * g_DataObject = NULL; // Глобальная переменная с данными.

//Читатель:
DataObject * acquire();
void release(DataObject * p);

// Писатель:
void setNew(DataObject * p);

Читатели не должны блокироваться.
*/

namespace deferred_delete_ref_counter {

    static atomic<long long> live_objects_count = {0};

    struct TestData: public SharedBase {
        TestData() {
            ++live_objects_count;
        }

        ~TestData() override {
            --live_objects_count;
        }

        vector<int> data;
        int sum;
    };

    const int threads_count = 100;
    const int readers_iterations_count = 500;
    const int writer_iterations_count = 150;

    void reader(SharedObject& shared_object)
    {
        for (auto i = 0; i < readers_iterations_count; ++i) {
            AutoSharedObject<TestData> object(shared_object);

            if (!object)
                continue;

            int sum = 0;
            for (auto x : object->data)
                sum += x;

            if (sum != object->sum)
                cout << "error!\n";

            this_thread::sleep_for(chrono::milliseconds(get_random(10, 50)));
        }
    }

    void writer(SharedObject& shared_object)
    {
        static atomic<long> filler;

        for (auto i = 0; i < writer_iterations_count; ++i) {
            this_thread::sleep_for(chrono::milliseconds(get_random(10, 50)));

            TestData * data{new TestData};

            const size_t vector_size = 100;
            data->data.reserve(vector_size);

            data->sum = 0;
            for (auto j = 0; j < vector_size; ++j) {
                long x = filler.fetch_add(1);
                data->sum += x;
                data->data.push_back(x);
            }

            shared_object.set(data);
        }
    }

    void test()
    {
        std::cout << "DeferredDeleteReferenceCounter test on " << threads_count << " threads\n";
        {
            SharedGarbageCollector shared_garbage_collector;
            SharedObject shared_object(shared_garbage_collector);
            vector<thread> threads;

            for (auto i = 0; i < threads_count; ++i)
                threads.emplace_back(i % 3? reader : writer, ref(shared_object));

            for (auto& thread : threads)
                thread.join();
        }

        if (live_objects_count.load())
            cout << "ERROR! live objects: " << live_objects_count.load() << "\n";
    }
}

int main()
{
    state_transition::test();
    producer_consumer::test();
    thread_safe_singleton::test();
    read_copy_update::test();
    deferred_delete_ref_counter::test();

#if _WIN32
    critical_section::test();
    readers_writer_lock::test();
    double_word_cas_ref_counter::test();
#endif

    return 0;
}
