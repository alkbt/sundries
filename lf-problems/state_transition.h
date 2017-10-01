#pragma once

#include <atomic>

class StateTransion {
public:
    StateTransion(): state{NoData} {}

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
        switch (state.load(std::memory_order_acquire)) {
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
    static const long NoData = 0;
    static const long ProducingData = 1;
    static const long DataReady = 2;
    static const long ConsumingData = 3;

    std::atomic<long> state;

    bool tryAdvanceState(long From, long To) {
        return state.compare_exchange_strong(From, To, std::memory_order_acq_rel);
    }

    void SetState(long to) {
        state.store(std::memory_order_release);
    }
};

