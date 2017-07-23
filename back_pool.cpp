/* Простой пул, автоматически возвращающий отданные ресурсы */

#include <iostream>
#include <string>
#include <memory>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

using namespace std;

#define UNUSED __attribute((unused))

class Resource {
public:
    Resource(): Resource(data1Counter++, data2Counter--) {}
    Resource(int data1, int data2): _data1{data1}, _data2{data2} {}

    ~Resource() {
        cout << "delete resource " << _data1 << ":" << _data2 << endl;
    }

    int data1() {return _data1;}
    int data2() {return _data2;}
private:
    int _data1;
    int _data2;

    static int data1Counter;
    static int data2Counter;
};

int Resource::data1Counter {0};
int Resource::data2Counter {100};

class ResourceDeleter;

class _ResourcePoolImpl: public enable_shared_from_this<_ResourcePoolImpl>
{
public:
    ~_ResourcePoolImpl() {cout << "~_ResourcePoolImpl" << endl;}

    void addResource(Resource *res);

    shared_ptr<Resource> getResource();
private:
    queue<shared_ptr<Resource>> pool;

    mutex m_mutex;
    condition_variable m_condition;
};

class ResourcePool {
public:
    ResourcePool(int poolSize);

    shared_ptr<Resource> getResource();
    void freeResource(shared_ptr<Resource>& res);
    void freeResource(shared_ptr<Resource>&& res);
private:
    shared_ptr<_ResourcePoolImpl> rPool{make_shared<_ResourcePoolImpl>()};
};

class ResourceDeleter {
public:
    ResourceDeleter(shared_ptr<_ResourcePoolImpl> origin): origin{origin} {}

    void operator()(Resource * r) {
        shared_ptr<_ResourcePoolImpl> rPool = origin.lock();
        if (rPool) {
            string str {"ResourceDeleter return resource " + to_string(r->data1()) + ":" + to_string(r->data2()) + "\n"};
            cout << str << flush;
            rPool->addResource(r);
        } else {
            string str {"ResourceDeleter delete resource " + to_string(r->data1()) + ":" + to_string(r->data2()) + "\n"};
            cout << str;
            delete r;
        }
    }
private:
    weak_ptr<_ResourcePoolImpl> origin;
};


ResourcePool::ResourcePool(int poolSize) {
    for (int i = 0; i < poolSize; ++i) {
        rPool->addResource(new Resource);
    }
}

shared_ptr<Resource> ResourcePool::getResource()
{
    return rPool->getResource();
}

void ResourcePool::freeResource(shared_ptr<Resource>& res)
{
    res.reset();
}

void ResourcePool::freeResource(shared_ptr<Resource>&& res)
{
    res.reset();
}

shared_ptr<Resource> _ResourcePoolImpl::getResource()
{
    std::unique_lock<std::mutex> lock_( m_mutex );

    while(pool.empty()){
        m_condition.wait( lock_ );
    }

    auto res = pool.front();
    pool.pop();

    return res;
}

void _ResourcePoolImpl::addResource(Resource *res)
{
    std::lock_guard<std::mutex> locker_( m_mutex );

    pool.emplace(shared_ptr<Resource>(res, ResourceDeleter(shared_from_this())));
    m_condition.notify_one();
}

void fn(shared_ptr<ResourcePool> rPool) {
    this_thread::sleep_for(chrono::milliseconds(20));
    auto r = rPool->getResource();
    string str {to_string(r->data1()) + ":" + to_string(r->data2()) + "\n"};
    cout << str << flush;
}

int main(int argc UNUSED, char *argv[] UNUSED)
{
    shared_ptr<ResourcePool> rPool{make_shared<ResourcePool>(10)};
    std::vector<std::shared_ptr<std::thread>> vec;


    for ( size_t i = 0; i< 50 ; ++i ){
        vec.push_back(std::make_shared<std::thread>(std::thread(fn, rPool)));
    }

    for(auto &i : vec) {
        i.get()->join();
    }

    cout << "[WHATS ALL!]" << endl;

    return 0;
}
