#pragma once

#include <atomic>

template <class T>
class Rcu {
public:
    Rcu() = default;

    ~Rcu() {
        delete current_data.load();
        delete copy_data.load();
    }

    Rcu(const Rcu&) = delete;
    Rcu(Rcu&&) = delete;
    Rcu& operator=(const Rcu&) = delete;
    Rcu& operator=(Rcu&&) = delete;

    T get() {
        for (;;) {
            InternalData * data = current_data.load();

            ++data->readersCount;
            if (data->updatingInProgress)
                continue;

            T value = data->data;

            --data->readersCount;

            return value;
        }
    }

    void set(const T& value)
    {
        AutoSpinLock lock(writersLock);

        InternalData * new_data = copy_data.load();
        new_data->updatingInProgress.store(true);

        AdaptiveWait wait;
        while (new_data->readersCount)
            wait();

        new_data->data = value;

        new_data->updatingInProgress.store(false);

        copy_data.store(current_data.load());
        current_data.store(new_data);
    }
private:
    struct InternalData {
        InternalData(): readersCount{0}, updatingInProgress{0} {}

        T data;

        std::atomic<long> readersCount;
        std::atomic<bool> updatingInProgress;
    };

    std::atomic<InternalData *> current_data = {new InternalData};
    std::atomic<InternalData *> copy_data = {new InternalData};

    SpinLock writersLock;
};
