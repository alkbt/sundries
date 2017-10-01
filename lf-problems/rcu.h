#pragma once

#include <atomic>

template <class T>
class Rcu {
public:
    Rcu():data{new InternalData, new InternalData()} {}

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
        InternalData(): readersCount{0}, updatingInProgress{0} {}

        T data;

        std::atomic<long> readersCount;
        std::atomic<bool> updatingInProgress;
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

    void setCopy(InternalData * volatile newData) {
        data[1] = newData;
    }

    SpinLock writersLock;
};
