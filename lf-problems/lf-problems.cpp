// lf-problems.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <exception>
#include <iostream>
#include <vector>
#include <thread>
#include <string>
#include <sstream>
#include <random>
#include <cstdlib>
#include <Windows.h>

int getRandom(int from, int to)
{
    static std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> uni(from, to);

    return uni(rng);
}

/*
Простые задачки по lock-free и параллельным алгоритмам.
*/

/*
1. Recursive Fast Mutex.

Есть следующие функции ядра:

VOID ExAcquireFastMutex(
_Inout_ PFAST_MUTEX FastMutex
);

VOID ExReleaseFastMutex(
_Inout_ PFAST_MUTEX FastMutex
);

В документации сказано: "Fast mutexes cannot be acquired recursively. If a thread that
is already holding a fast mutex tries to acquire it, that thread will deadlock".

Задание:

Реализуйте обертку над fast mutex, которая позволит захватывать его рекурсивно.

*/

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
namespace StateTransion {

    class StateTransion {
    public:
        StateTransion():State{ NoData }, OwnerThreadId{ 0 } {}

        StateTransion(const StateTransion&) = delete;
        StateTransion(StateTransion&&) = delete;
        StateTransion& operator=(const StateTransion&) = delete;
        StateTransion& operator=(StateTransion&&) = delete;

        void AcquireForWrite() {
            AdvanceState(NoData, ProducingData);
        }

        void AcquireForRead() {
            AdvanceState(DataReady, ConsumingData);
        }

        void Release() {
            if (GetCurrentThreadId() != OwnerThreadId)
                return;

            switch (State) {
            case ProducingData:
                SetState(DataReady);
                break;
            case ConsumingData:
                SetState(NoData);
                break;
            default:
                throw std::exception("StateTransion object is broken!");
            }

        }
    private:
        static const LONG NoData = 0;
        static const LONG ProducingData = 1;
        static const LONG DataReady = 2;
        static const LONG ConsumingData = 3;

        volatile LONG State;

        /*
         * Идентификатор потока используется для
         * предотвращения злоупотреблением Release
         */
        volatile DWORD OwnerThreadId;

        void AdvanceState(LONG From, LONG To) {
            for (;;) {
                if (InterlockedCompareExchange(&State, To, From) == From)
                    break;

                Sleep(20);
            }

            OwnerThreadId = GetCurrentThreadId();
            MemoryBarrier();
        }

        void SetState(LONG to) {
            InterlockedExchange(&OwnerThreadId, 0);
            InterlockedExchange(&State, to);
        }
    };

    StateTransion state;
    std::vector<int> data;
    int checkSum = 0;

    void StateTransionThread(bool reader)
    {
        Sleep(getRandom(0, 100));

        if (reader) {
            /* Reader */
            state.AcquireForRead();

            int checkSum = 0;
            for (const auto x : data) {
                checkSum += x;
            }

            if (checkSum != checkSum)
                std::cout << "StateTransion object is broken!\n";

            state.Release();
        } else {
            /* Writer */
            state.AcquireForWrite();

            auto dataSize = getRandom(0, 50);
            data.clear();
            checkSum = 0;
            for (auto i = 0; i < dataSize; ++i) {
                int x = getRandom(0, 100);
                checkSum += x;
                data.push_back(x);
            }

            state.Release();
        }
    }


    void Test()
    {
        const int testThreadsCount = 10000;
        std::cout << "StateTransition test on " << testThreadsCount << " threads\n";

        std::vector<std::thread> threads;
        for (auto i = 0; i < testThreadsCount; ++i)
            threads.emplace_back(std::thread(StateTransionThread, !(i % 2)));

        for (auto& thread : threads)
            thread.join();
    }
}

/*
3. Producer-Consumer.

Поток 1 готовит данные и выставляет флаг ready=true.
Поток 2 в цикле ждет флага ready и по приходу забирает данные.
Реализовать без блокировок и ожиданий.
*/

namespace ProducerConsumer {
    volatile bool ready = false;
    int data = 0;

    void producerWait()
    {
        for (;;) {
            if (!ready)
                break;

            Sleep(20);
        }

        MemoryBarrier();
    }

    void consumerWait()
    {
        for (;;) {
            if (ready)
                break;

            Sleep(20);
        }

        MemoryBarrier();
    }

    void setDataProduced()
    {
        MemoryBarrier();
        ready = true;
    }

    void setDataConsumed()
    {
        MemoryBarrier();
        ready = false;
    }

    const int iterationsCount = 1000;
    void ProducerThread()
    {
        for (auto i = 0; i < iterationsCount; ++i) {
            producerWait();

            data = i;

            setDataProduced();
        }
    }

    void ConsumerThread()
    {
        for (auto i = 0; i < iterationsCount; ++i) {
            consumerWait();

            if (data != i)
                std::cout << "Corrupted Data!\n";

            setDataConsumed();
        }
    }

    void Test()
    {
        std::cout << "ProducerConsumer test on " << iterationsCount << " iterations\n";
        std::thread producer(ProducerThread), consumer(ConsumerThread);
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

namespace ThreadSafeSingleton {
    class TestObject {
    public:

        TestObject() {
            InterlockedIncrement(&creationCounter);
        }

        LONG getCreationCounter() {
            return creationCounter;
        }
    private:
        static volatile LONG creationCounter;
    };
    volatile LONG TestObject::creationCounter = 0;

    template <typename T>
    struct Singleton {
        Singleton() = delete;

        static T * getInstance() {
            static volatile T * object = nullptr;
            static volatile LONG createLock = 0;

            if (!object) {
                for (;;) {
                    if (!InterlockedCompareExchange(&createLock, 1, 0))
                        break;

                    Sleep(20);
                }

                if (!object)
                    object = new T();

                InterlockedExchange(&createLock, 0);
            }

            return const_cast<T *>(object);
        }
    };

    void SingletonThread()
    {
        Sleep(getRandom(0, 100));
        if (Singleton<TestObject>::getInstance()->getCreationCounter() != 1) {
            std::cout << "Singleton multiple creation error!\n";
        }
    }


    void Test()
    {
        const int testThreadsCount = 100;
        std::cout << "ThreadSafeSingleton test on " << testThreadsCount << " threads\n";

        std::vector<std::thread> threads;
        for (auto i = 0; i < testThreadsCount; ++i)
            threads.emplace_back(std::thread(SingletonThread));

        for (auto& thread : threads)
            thread.join();
    }
}

/*
5. One-time Initialization.

Реализуйте механизм потокобезопасной инициализации данных: первый поток,
обращающийся к данным, должен выполнять их инициализацию, все остальные
конкурирующие потоки должны ждать. После того, как данные инициализированы,
дальнейшие издержки на получение данных должны быть минимальны.

Пример:

class Data // Данные.
{
// ...
};

template <class T>
class OneTimeInit // Обертка для one-time init.
{
// ...
};



OneTimeInit<Data> g_Data;

void func()
{
g_Data->doWork(arg1, arg2); // Инициализация Data должна быть здесь.
int value = g_Data->getValue();
// И т.д.
}
*/

/*
6. Critical Section.

Реализуйте критическую секцию по аналогии с Win32:

- методы lock, unlock и tryLock;

- захват свободной секции не должен приводить к ожиданию (WaitForXxx,
Sleep, etc) или сисемным вызовам (syscall);

- попытка захвата занятой секции - уход в ожидание на объекте ядра;

- должна быть поддержка рекурсивного захвата.
*/

namespace CriticalSection {
    class CriticalSection {
    public:
        CriticalSection():refCount{ 0 }, ownerThreadId{ 0 } {
            event = CreateEvent(nullptr, false, false, nullptr);
            if (!event)
                throw std::exception("CreateEvent Error!");
        }

        ~CriticalSection() {
            if (event)
                CloseHandle(event);
        }

        CriticalSection(const CriticalSection&) = delete;
        CriticalSection(CriticalSection&&) = delete;
        CriticalSection& operator=(const CriticalSection&) = delete;
        CriticalSection& operator=(CriticalSection&&) = delete;

        bool tryLock() {
            DWORD threadId = GetCurrentThreadId();

            if (tryLockRecursive(threadId))
                return true;

            return tryLockFreeSection(threadId);
        }

        void lock() {
            DWORD threadId = GetCurrentThreadId();

            if (tryLockRecursive(threadId))
                return;

            for (;;) {
                if (tryLockFreeSection(threadId))
                    return;

                DWORD waitResult = WaitForSingleObject(event, INFINITE);
                if (waitResult != WAIT_OBJECT_0) {
                    throw std::exception("CriticalSection can't wait");
                }
            }
        }

        void unlock() {
            DWORD threadId = GetCurrentThreadId();
            if (threadId != ownerThreadId)
                return;

            if (!InterlockedDecrement(&refCount)) {
                InterlockedExchange(&ownerThreadId, 0);
                SetEvent(event);
            }
        }

    private:
        bool tryLockRecursive(DWORD threadId) {
            if (threadId == ownerThreadId) {
                InterlockedIncrement(&refCount);
                return true;
            }

            return false;
        }

        bool tryLockFreeSection(DWORD threadId) {
            if (!InterlockedCompareExchange(&ownerThreadId, threadId, 0)) {
                InterlockedExchange(&refCount, 1);
                return true;
            }

            return false;
        }

        volatile LONG refCount;
        volatile DWORD ownerThreadId;
        HANDLE event;
    };

    class AutoCriticalSection {
    public:
        AutoCriticalSection(CriticalSection& cs):cs{ cs } {
            cs.lock();
        }

        ~AutoCriticalSection() {
            cs.unlock();
        }

    private:
        CriticalSection& cs;
    };

    CriticalSection cs;
    DWORD csData = 0;

    DWORD recursiveCsCheck()
    {
        AutoCriticalSection lock(cs);

        DWORD localData = csData;
        if ((getRandom(0, 100) < 80) && (localData != recursiveCsCheck()))
            throw std::exception("CriticalSection's algorithm is broken!");

        return localData;
    }

    void CriticalSectionThread()
    {
        Sleep(getRandom(0, 100));
        try {
            AutoCriticalSection lock(cs);

            DWORD localData = getRandom(0, 100);
            csData = localData;

            if (localData != recursiveCsCheck())
                throw std::exception("CriticalSection's algorithm is broken!");
        } catch (std::exception e) {
            std::cout << e.what() << std::endl;
        }
    }


    void Test()
    {
        const int testThreadsCount = 10000;
        std::cout << "CriticalSection test on "<< testThreadsCount << " threads\n";

        std::vector<std::thread> threads;
        for (auto i = 0; i < testThreadsCount; ++i)
            threads.emplace_back(std::thread(CriticalSectionThread));

        for (auto& thread : threads)
            thread.join();
    }
}
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
namespace ReadCopyUpdate {
    struct Data {
        Data(int a = 0, int b = 0, int c = 0):a{ a }, b{ b}, c{ c } {}
        int a;
        int b;
        int c;
    };

    template <class T>
    class Rcu {
    public:
        Rcu():data{ new InternalData, new InternalData() }, writersLock{ 0 } {}

        ~Rcu() {
            delete data[0];
            delete data[1];
        }

        Rcu(const Rcu&) = delete;
        Rcu(Rcu&&) = delete;
        Rcu& operator=(const Rcu&) = delete;
        Rcu& operator=(Rcu&&) = delete;

        T get() {
            for (;;) {
                InternalData * currentData = getCurrentData();

                InterlockedIncrement(&currentData->readersCount);
                if (InterlockedCompareExchange(&currentData->updatingInProgress, 0, 0) == 1) {
                    /*
                     * Поток был вытеснен между получением указателя и
                     * инкрементом readersCount, а проснулся когда писатель
                     * начал обновлять данные
                     */
                    InterlockedDecrement(&currentData->readersCount);
                    continue;
                }

                T value = currentData->data;
                InterlockedDecrement(&currentData->readersCount);

                return value;
            }
        }

        void set(const T& value)
        {
            for (;;) {
                if (!InterlockedCompareExchange(&writersLock, 1, 0))
                    break;

                Sleep(20);
            }

            InternalData * newData = getCopy();
            InterlockedExchange(&newData->updatingInProgress, 1);
            for (;;) {
                if (!InterlockedCompareExchange(&newData->readersCount, 0, 0))
                    break;

                Sleep(20);
            }

            newData->data = value;
            InterlockedExchange(&newData->updatingInProgress, 0);

            setCopy(data[0]);
            setCurrentData(newData);

            InterlockedExchange(&writersLock, 0);
        }
    private:
        struct InternalData {
            InternalData(): readersCount{ 0 }, updatingInProgress{0} {}

            T data;

            volatile LONG readersCount;
            volatile LONG updatingInProgress;
        };

       volatile InternalData * data[2];

       InternalData * getCurrentData() {
           return (InternalData *)InterlockedCompareExchangePointer(
                        (volatile PVOID *)&data[0], nullptr, nullptr);
       }

       void setCurrentData(InternalData * newData) {
           InterlockedExchangePointer((volatile PVOID *)&data[0], newData);
       }

       InternalData * getCopy() {
           return (InternalData *)InterlockedCompareExchangePointer(
                        (volatile PVOID *)&data[1], nullptr, nullptr);
       }

       void setCopy(volatile InternalData * newData) {
           InterlockedExchangePointer((volatile PVOID *)&data[1], (PVOID)newData);
       }

       volatile LONG writersLock;
    };

    Rcu<Data> rcu;
    void RcuWriterThread()
    {
        static int a = 0;
        static int b = 0;

        Sleep(getRandom(0, 1000));
        for (unsigned long long i = 0; i < 1000; ++i) {
            Data data{ --a, ++b, 0 };
            data.c = data.a + data.b;

        }
    }

    void RcuReaderThread()
    {
        Sleep(getRandom(0, 1000));
        for (unsigned long long i = 0; i < 1000; ++i) {
            Data data = rcu.get();
            if (data.a + data.b != data.c)
                std::cout << "Rcu is broken!\n";
        }
    }

    void Test()
    {
        const int testThreadsCount = 1000;
        std::cout << "ReadCopyUpdate test on " << testThreadsCount << " threads\n";

        std::vector<std::thread> threads;
        for (auto i = 0; i < testThreadsCount; ++i)
            if (!(i % 3))
                threads.emplace_back(std::thread(RcuWriterThread));
            else
                threads.emplace_back(std::thread(RcuReaderThread));

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
namespace ReadersWriterLock {
    class RwLock {
    public:
        RwLock() {
            lockState.raw = 0;
        }

        RwLock(const RwLock&) = delete;
        RwLock(RwLock&&) = delete;
        RwLock& operator=(const RwLock&) = delete;
        RwLock& operator=(RwLock&&) = delete;

        void sharedLock() {
            for (;;) {
                LockState oldLockState;
                oldLockState.raw = InterlockedCompareExchange(&lockState.raw, 0, 0);

                if (oldLockState.writerLock) {
                    Sleep(20);
                    continue;
                }

                LockState newLockState = oldLockState;
                ++newLockState.readersCount;

                if (InterlockedCompareExchange(&lockState.raw,
                                               newLockState.raw, oldLockState.raw)
                                               == oldLockState.raw)
                    return;
            }

        }

        void sharedUnlock() {
            for (;;) {
                LockState oldLockState;
                oldLockState.raw = InterlockedCompareExchange(&lockState.raw, 0, 0);

                LockState newLockState = oldLockState;
                --newLockState.readersCount;

                if (InterlockedCompareExchange(&lockState.raw,
                                               newLockState.raw, oldLockState.raw)
                                               == oldLockState.raw)
                    return;

                Sleep(20);
            }
        }

        void exclusiveLock() {
            LockState oldLockState;
            oldLockState.raw = 0;

            LockState newLockState;
            newLockState.writerLock = 1;
            newLockState.readersCount = 0;

            for (;;) {
                if (InterlockedCompareExchange(&lockState.raw,
                                               newLockState.raw, oldLockState.raw)
                                               == oldLockState.raw)
                    break;

                Sleep(20);
            }
        }

        void exclusiveUnlock() {
            LockState oldLockState;
            oldLockState.raw = InterlockedCompareExchange(&lockState.raw, 0, 0);

            if (oldLockState.readersCount || (!oldLockState.writerLock))
                throw std::exception("RwMutex in broken!\n");

            LockState newLockState;
            newLockState.raw = 0;

            InterlockedExchange(&lockState.raw, newLockState.raw);
        }
    private:
        union LockState {
            volatile LONG raw;
            struct {
                volatile ULONG writerLock    : 1;
                volatile ULONG readersCount  : 31;
            };
        };

        LockState lockState;
    };

    struct Data {
        int a;
        int b;
        int c;
    };
    Data data{};
    RwLock lock;

    void ReadersLockThread()
    {
        Sleep(getRandom(0, 1500));

        lock.sharedLock();

        if (data.a + data.b != data.c)
            std::cout << "RwLock is broken!\n";

        lock.sharedUnlock();
    }

    void WriterLockThread()
    {
        Sleep(getRandom(0, 500));

        lock.exclusiveLock();

        static int a = 0;
        static int b = 0;

        data.a = ++a;
        data.b = --b;
        data.c = data.a + data.b;

        lock.exclusiveUnlock();
    }

    void Test()
    {
        const int testThreadsCount = 10000;
        std::cout << "ReadersWriterLock test on " << testThreadsCount << " threads\n";

        std::vector<std::thread> threads;
        for (auto i = 0; i < testThreadsCount; ++i)
            if (!(i % 3))
                threads.emplace_back(std::thread(WriterLockThread));
            else
                threads.emplace_back(std::thread(ReadersLockThread));

        for (auto& thread : threads)
            thread.join();
    }
}

/*
9. Rundown Protection.

Реализуйте блокировку объекта, защищающую его от преждевременного удаления
(аналог I/O Remove Locks):

acquireRundown - захватывает блокировку, защищая объект от удаления.
Возвращает ошибку, если объект уже находится в состоянии удаления.

releaseRundown - освобождает блокировку.

releaseRundownAndWait - освобождает блокировку и запускает процесс
удаления объекта. С этого момента все acquireRundown должны
завершаться ошибкой. Возвращает управление только тогда, когда
все блокировки освобождены.

acquireRundown и releaseRundown должны быть неблокирующими.



10. Spinlock.

Реализуйте спинлок в kernel-mode.
Необходимо учесть следующие аспекты:

- код под спинлоком не должен вытесняться системным планировщиком;

- спинлок должен корректно работать как на многопроцессорных, так и на однопроцессорных машинах.



11. In-Stack Queued Spinlock.

Реализуйте спинлок с очередью в kernel-mode:

- захват спинлока выдается потокам в порядке очереди, т.е. первым пришел -
первым вышел (FIFO);

- цикл активного ожидания (busy wait) в каждом потоке должен выполняться
не на разделяемой глобальной переменной, а на стековой переменной (снижая
негативный эффект от конкуренции за данные между процессорами).
*/

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

namespace ReferenceCounter {
    struct DataObject {
        volatile int data;

        volatile LONG refCount;
    };

    DataObject * g_DataObject = nullptr;
    volatile LONG acquiresCount = 0;

    DataObject * acquire()
    {
        InterlockedIncrement(&acquiresCount);

        DataObject * object = (DataObject *)InterlockedCompareExchangePointer(
            (volatile PVOID *)&g_DataObject, nullptr, nullptr);
        if (object)
            InterlockedIncrement(&object->refCount);

        InterlockedDecrement(&acquiresCount);
        return object;
    }

    void release(DataObject * object)
    {
        if (!object)
            return;

        if (!InterlockedDecrement(&object->refCount))
            delete object;
    }

    void setNew(DataObject * newObject)
    {
        newObject->refCount = 1;
        DataObject * oldObject = (DataObject *)InterlockedExchangePointer(
            (volatile PVOID *)&g_DataObject, newObject);
        if (!oldObject)
            return;

        for (;;) {
            if (!InterlockedCompareExchange(&acquiresCount, 0, 0))
                break;

            Sleep(10);
        }

        release(oldObject);
    }

    void ReferenceCounterThread()
    {
        Sleep(getRandom(0, 100));

        if (getRandom(0, 10) < 6) {
            /* Writer*/
            DataObject * object = new DataObject();
            object->data = getRandom(0, 500);
            setNew(object);
        } else {
            /* Reader */
            DataObject * object = acquire();
            if (!object)
                return;

            int data = object->data;
            int iterationsCount = getRandom(0, 1000);
            for (int i = 0; i < iterationsCount; ++i) {
                if (object->data != data) {
                    std::cout << "ReferenceCounter is broken!\n";
                    return;
                }

                Sleep(10);
            }
        }
    }

    void Test()
    {
        const int testThreadsCount = 10000;
        std::cout << "ReferenceCounter test on " << testThreadsCount << " threads\n";

        std::vector<std::thread> threads;
        for (auto i = 0; i < testThreadsCount; ++i)
            threads.emplace_back(std::thread(ReferenceCounterThread));

        for (auto& thread : threads)
            thread.join();
    }
}
/*
----------------------------------------------------------------------

Литература:

The Art of Multiprocessor Programming
https://www.amazon.com/Art-Multiprocessor-Programming-Revised-Reprint/dp/0123973376

C++ Concurrency in Action: Practical Multithreading
https://www.amazon.com/C-Concurrency-Action-Practical-Multithreading/dp/1933988770

1024cores
http://www.1024cores.net/

*/

class MassiveThreads {
public:
};

int main()
{
    StateTransion::Test();
    ProducerConsumer::Test();
    ThreadSafeSingleton::Test();
    CriticalSection::Test();
    ReadCopyUpdate::Test();
    ReadersWriterLock::Test();
    ReferenceCounter::Test();
    return 0;
}
