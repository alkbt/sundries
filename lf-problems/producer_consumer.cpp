
#include <atomic>

#include "spin_lock.h"
#include "producer_consumer.h"

using namespace std;

void ProducerConsumer::wait_for_data_consumed()
{
    AdaptiveWait adaptiveWait;
    while (ready.load(memory_order_acquire))
        adaptiveWait();
}

void ProducerConsumer::wait_for_data_ready()
{
    AdaptiveWait adaptiveWait;
    while (!ready.load(memory_order_acquire))
        adaptiveWait();
}

void ProducerConsumer::set_data_ready()
{
    ready.store(true, memory_order_release);
}

void ProducerConsumer::set_data_consumed()
{
    ready.store(false, memory_order_release);
}