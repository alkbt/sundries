// lf-problems.cpp : Defines the entry point for the console application.
//
#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <random>

#include <emmintrin.h>

using namespace std;

int getRandom(int from, int to)
{
    static std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<int> uni(from, to);

    return uni(rng);
}

class AdaptiveWait
{
public:
    AdaptiveWait(int theresold = 50): waiting_state{0}, theresold{theresold} {}

    AdaptiveWait(const AdaptiveWait&) = delete;
    AdaptiveWait(AdaptiveWait&&) = delete;
    AdaptiveWait& operator=(const AdaptiveWait&) = delete;
    AdaptiveWait& operator=(AdaptiveWait&&) = delete;

    void operator()() {
        if (waiting_state < theresold)
            _mm_pause();
        else
            this_thread::sleep_for(chrono::milliseconds(10));

        ++waiting_state;
    }
private:
    int theresold;
    int waiting_state;
};

class SpinLock
{
public:
    SpinLock():spinLock{false} {}

    SpinLock(const SpinLock&) = delete;
    SpinLock(SpinLock&&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;
    SpinLock& operator=(SpinLock&&) = delete;

    void lock() {
        AdaptiveWait adaptiveWait;
        bool expected;
        for (;;) {
            expected = false;
            if (spinLock.compare_exchange_weak(expected, true, memory_order_acq_rel))
                break;

            adaptiveWait();
        }
    }

    void unlock() {
        spinLock.store(false, memory_order_release);
    }
private:
    atomic<bool> spinLock;
};

class AutoSpinLock
{
public:
    AutoSpinLock(SpinLock& spinLock):spinLock{spinLock} {
        spinLock.lock();
    }
    ~AutoSpinLock() {
        spinLock.unlock();
    }

    AutoSpinLock(const AutoSpinLock&) = delete;
    AutoSpinLock(AutoSpinLock&&) = delete;
    AutoSpinLock& operator=(const AutoSpinLock&) = delete;
    AutoSpinLock& operator=(const AutoSpinLock&&) = delete;
private:
    SpinLock& spinLock;
};

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
        StateTransion(): state{ NoData } {}

        StateTransion(const StateTransion&) = delete;
        StateTransion(StateTransion&&) = delete;
        StateTransion& operator=(const StateTransion&) = delete;
        StateTransion& operator=(StateTransion&&) = delete;

        bool AcquireForWrite() {
            return tryAdvanceState(NoData, ProducingData);
        }

        bool AcquireForRead() {
            return tryAdvanceState(DataReady, ConsumingData);
        }

        void Release() {
            switch (state.load(memory_order_acquire)) {
            case ProducingData:
                SetState(DataReady);
                break;
            case ConsumingData:
                SetState(NoData);
                break;
            default:
                break;
            }

        }
    private:
        static const long NoData        = 0;
        static const long ProducingData = 1;
        static const long DataReady     = 2;
        static const long ConsumingData = 3;

        atomic<long> state;

        bool tryAdvanceState(long From, long To) {
            return state.compare_exchange_strong(From, To, memory_order_acq_rel);
        }

        void SetState(long to) {
            state.store(memory_order_release);
        }
    };

    StateTransion state;
    std::vector<int> data;
    int checkSum = 0;

    const int testThreadsCount = 50;
    const int iterationsCount = 1000;

    void reader()
    {
        this_thread::sleep_for(chrono::milliseconds(getRandom(0, 100)));

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
        this_thread::sleep_for(chrono::milliseconds(getRandom(0, 100)));

        if (!state.AcquireForWrite())
            return;

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

    void Test()
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
3. Producer-Consumer.

Поток 1 готовит данные и выставляет флаг ready=true.
Поток 2 в цикле ждет флага ready и по приходу забирает данные.
Реализовать без блокировок и ожиданий.
*/

namespace ProducerConsumer {
    atomic<bool> ready{false};
    int data = 0;

    void producerWait()
    {
        AdaptiveWait adaptiveWait;
        while (ready.load(memory_order_acquire))
                adaptiveWait();
    }

    void consumerWait()
    {
        AdaptiveWait adaptiveWait;
        while (!ready.load(memory_order_acquire))
                adaptiveWait();
    }

    void setDataProduced()
    {
        ready.store(true, memory_order_release);
    }

    void setDataConsumed()
    {
        ready.store(false, memory_order_release);
    }

    const int iterationsCount = 100;
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
            creationCounter.fetch_add(1, memory_order_acq_rel);
        }

        long getCreationCounter() {
            return creationCounter.load(memory_order_acquire);
        }
    private:
        static atomic<long> creationCounter;
    };

    atomic<long> TestObject::creationCounter{0};

    template<typename T>
    struct Singleton {
        Singleton() = delete;

        static T * getInstance() {

            if (!object.load(memory_order_relaxed)) {
                AutoSpinLock lock(creatorsLock);

                if (!object.load(memory_order_relaxed)) {
                    T * newObject = new T();
                    atomic_thread_fence(memory_order_seq_cst);
                    object.store(newObject, memory_order_relaxed);
                }
            }

            return object.load(memory_order_relaxed);
        }
    private:
        static volatile atomic<T *> object;
        static SpinLock creatorsLock;
    };

    template<typename T>
    volatile atomic<T*> Singleton<T>::object = {nullptr};;

    template<typename T>
    SpinLock Singleton<T>::creatorsLock;

    void SingletonThread()
    {
        auto o = Singleton<TestObject>::getInstance();
        if (o->getCreationCounter() != 1) {
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

/*
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
                ownerThreadId = 0;
                MemoryBarrier();

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
                refCount = 1;
                MemoryBarrier();

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
*/

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
        Rcu():data{ new InternalData, new InternalData() }{}

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

                ++currentData->readersCount;

                T value = currentData->data;

                --currentData->readersCount;

                return value;
            }
        }

        void set(const T& value)
        {
            AutoSpinLock lock(writersLock);

            InternalData * newData = getCopy();
            newData->updatingInProgress.store(1);

            AdaptiveWait wait;
            while (newData->readersCount)
                wait();

            newData->data = value;

            newData->updatingInProgress.store(0);

            setCopy(getCurrentData());
            setCurrentData(newData);
        }
    private:
        struct InternalData {
            InternalData(): readersCount{ 0 }, updatingInProgress{0} {}

            T data;

            atomic<long> readersCount;
            atomic<bool> updatingInProgress;
        };

        InternalData * volatile data[2];

        InternalData * getCurrentData() {
            return data[0];
        }

       void setCurrentData(InternalData * newData) {
           data[0] = newData;
       }

       InternalData * getCopy() {
           return data[1];
       }

       void setCopy(volatile InternalData * newData) {
           data[1] = newData;
       }

       SpinLock writersLock;
    };

    Rcu<Data> rcu;
    void RcuWriterThread()
    {
        static int a = 0;
        static int b = 0;

        this_thread::sleep_for(chrono::milliseconds(getRandom(0, 1000)));
        for (unsigned long long i = 0; i < 1000; ++i) {
            Data data{ --a, ++b, 0 };
            data.c = data.a + data.b;

        }
    }

    void RcuReaderThread()
    {
        this_thread::sleep_for(chrono::milliseconds(getRandom(0, 1000)));
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
#if 0
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
                LockState oldLockState = lockState;
                if (oldLockState.writerLock) {
                    Sleep(20);
                    continue;
                }

                MemoryBarrier();

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
                LockState oldLockState = lockState;

                if (oldLockState.writerLock)
                    throw std::exception("RwLock is broken!\n");

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

            while (InterlockedCompareExchange(&lockState.raw, newLockState.raw,
                                              oldLockState.raw) != oldLockState.raw)
                Sleep(20);
        }

        void exclusiveUnlock() {
            if (lockState.readersCount || !lockState.writerLock)
                throw std::exception("RwMutex in broken!\n");

            lockState.raw = 0;
            MemoryBarrier();
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

        try {
            lock.sharedLock();

            if (data.a + data.b != data.c)
                throw std::exception("RwLock is broken!\n");

            lock.sharedUnlock();
        } catch (std::exception e) {
            std::cout << e.what() << std::endl;
        }
    }

    void WriterLockThread()
    {
        Sleep(getRandom(0, 500));

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
#endif
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
    template<class T>
    class DataObject {
    public:
        DataObject() {
            object.store(nullptr, memory_order_relaxed);
            acquires_count.store(0, memory_order_release);
        }

        T * acquire() {
            acquires_count.fetch_add(1, memory_order_acq_rel);

            T * obj = object.load(memory_order_consume);
            if (obj)
                obj->ref_count.fetch_add(1, memory_order_relaxed);

            acquires_count.fetch_sub(1, memory_order_acq_rel);

            return obj;
        }

        void release(T * obj) {
            if (!obj)
                return;

            if (!obj->ref_count.fetch_sub(1, memory_order_relaxed))
                delete obj;
        }

        void set(T * obj) {
            obj->ref_count.store(1, memory_order_release);

            T * old_obj = object.exchange(obj, memory_order_acq_rel);
            if (!old_obj)
                return;

            AdaptiveWait wait;
            while (acquires_count.load(memory_order_acquire)) {
                wait();
            }

            release(old_obj);
        }
    private:
        atomic<T *> object;
        atomic<long> acquires_count;
    };

    struct TestData {
        vector<int> data;
        int sum;

        atomic<long> ref_count;
    };

    DataObject<TestData> data_object;

    const int readers_iterations_count = 500;
    const int writer_iterations_count = 150;

    void reader()
    {
        for (auto i = 0; i < readers_iterations_count; ++i) {
            auto object = data_object.acquire();

            if (!object)
                continue;

            int sum = 0;
            for (auto x : object->data)
                sum += x;

            if (sum != object->sum)
                cout << "error!\n";

            data_object.release(object);
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }

    void writer()
    {
        static atomic<long> filler;

        for (auto i = 0; i < writer_iterations_count; ++i) {
            TestData * data{new TestData};

            data->sum = 0;
            for (auto j = 0; j < 100; ++j) {
                long x = filler.fetch_add(1);
                data->sum += x;
                data->data.push_back(x);
            }

            data_object.set(data);
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }

    void Test() {
        std::cout << "ReferernceCounter test on " << 50 << " threads\n";

        vector<thread> threads;

        for (auto i = 0; i < 50; ++i)
            threads.emplace_back(i % 3? reader : writer);

        for (auto& thread : threads)
            thread.join();
    }
}

namespace DeferredDeleteReferenceCounter {
    template <typename T>
    class SharedObject {
    public:
        SharedObject(): acquires_count{0},
            data{nullptr},
            deffered_destroy_list{nullptr},
            shutdown{false},
            deleter_thread{thread(&SharedObject::deleter, this)} {
        }

        ~SharedObject() {
            shutdown = true;
            deleter_thread.join();
        }

        T * acquire() {
            ++acquires_count;

            T * object = data.load();
            if (object)
                object->ref_count.fetch_add(1);

            --acquires_count;

            return object;
        }

        void release(T * object) {
            if (!object)
                return;

            if (object->marked_for_delete.load()) {
                object->ref_count.fetch_sub(1);
            } else if (object->ref_count.fetch_sub(1) == 1) {
                object->marked_for_delete.store(true);
                add_to_deferred_destroy_list(object);
            }
        }

        void set(T * object) {
            if (object) {
                object->ref_count.store(1);
                object->marked_for_delete.store(false);
            }

            T * old_object = data.exchange(object);

            release(old_object);
        }
    private:
        atomic<long> acquires_count;
        atomic<T *> data;

        struct list_node {
            list_node(T *object): object{object}, next{nullptr} {}

            list_node * next;

            T * object;
        };

        atomic<list_node *> deffered_destroy_list;

        void add_to_deferred_destroy_list(T * object) {
            list_node * node = new list_node(object);
            node->next = deffered_destroy_list.load();
            while (!deffered_destroy_list.compare_exchange_weak(node->next, node));
        }

        void destroy(list_node * head) {
            list_node * item = head;
            while (item) {
                list_node * next = item->next;

                if (item->object->ref_count) {
                    add_to_deferred_destroy_list(item->object);
                } else {
                    delete item->object;
                }

                delete item;

                item = next;
            }
        }

        atomic<bool> shutdown;
        void deleter() {
            while (!shutdown) {
                list_node * local_list = deffered_destroy_list.exchange(nullptr);
                if (!local_list) {
                    this_thread::sleep_for(chrono::milliseconds(10));
                    continue;
                }

                AdaptiveWait wait(100);
                while (acquires_count)
                    wait();

                destroy(local_list);
            }

            destroy(deffered_destroy_list.load());
        }
        thread deleter_thread;
    };

    static atomic<long long> live_objects_count = {0};

    struct TestData {
        TestData() {
            ++live_objects_count;
        }

        ~TestData() {
            --live_objects_count;
        }

        atomic<long> ref_count;
        atomic<bool> marked_for_delete;

        vector<int> data;
        int sum;


    };

    const int readers_iterations_count = 500;
    const int writer_iterations_count = 150;

    void reader(SharedObject<TestData>& shared_object)
    {
        for (auto i = 0; i < readers_iterations_count; ++i) {
            auto object = shared_object.acquire();

            if (!object)
                continue;

            int sum = 0;
            for (auto x : object->data)
                sum += x;

            if (sum != object->sum)
                cout << "error!\n";

            shared_object.release(object);
            this_thread::sleep_for(chrono::milliseconds(getRandom(10, 50)));
        }
    }

    void writer(SharedObject<TestData>& shared_object)
    {
        static atomic<long> filler;

        for (auto i = 0; i < writer_iterations_count; ++i) {
            TestData * data{new TestData};

            data->sum = 0;
            for (auto j = 0; j < 100; ++j) {
                long x = filler.fetch_add(1);
                data->sum += x;
                data->data.push_back(x);
            }

            shared_object.set(data);
            this_thread::sleep_for(chrono::milliseconds(getRandom(10, 50)));
        }
    }


    int main()
    {
        std::cout << "ReferernceCounter test on " << 50 << " threads\n";
        {
            SharedObject<TestData> shared_object;
            vector<thread> threads;

            for (auto i = 0; i < 500; ++i)
                threads.emplace_back(i % 3? reader : writer, ref(shared_object));

            for (auto& thread : threads)
                thread.join();

            shared_object.set(nullptr);
        }

        cout << "live objects: " << live_objects_count.load() << "\n";
    }
}

#if 0
namespace ReferernceCounterDoubleWordCas {

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

            oldBase = { 0, 0 };
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

        Base * newBase = new Base{ 0, reinterpret_cast<uintptr_t>(object) };

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
        Sleep(getRandom(0, 100));

        static int a = 0;
        static int b = 0;

        if (getRandom(0, 10) < 3)
            set(nullptr);
        else
            set(new Data{ ++a, --b, a + b });
    }

    void readerThread()
    {
        Sleep(getRandom(0, 100));

        Data * object = acquire();

        if (!object)
            return;

        if (object->a + object->b != object->c)
            std::cout << "ReferernceCounterDoubleWordCas is broken!\n";

        release(object);
    }


    void Test()
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

int main()
{
    StateTransion::Test();
    ProducerConsumer::Test();
    ThreadSafeSingleton::Test();
    /*CriticalSection::Test();*/
    ReadCopyUpdate::Test();
    /*ReadersWriterLock::Test();*/
    ReferenceCounter::Test();
    /*ReferernceCounterDoubleWordCas::Test();*/

    return 0;
}
