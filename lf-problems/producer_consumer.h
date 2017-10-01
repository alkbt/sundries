#pragma once

#include <atomic>

class ProducerConsumer {
public:
    void wait_for_data_consumed();
    void wait_for_data_ready();
    void set_data_ready();
    void set_data_consumed();

private:
    std::atomic<bool> ready = {false};
};