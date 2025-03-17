# ifndef BLOCKQUEUE_H
# define BLOCKQUEUE_H

#include <deque>
#include <condition_variable>
#include <mutex>
#include <sys/time.h>
using namespace std;

//重点在于对锁的使用
template<typename T>
class BlockQueue{
public:
    //explicit显式声明，防止构造函数隐式转换
    explicit BlockQueue(size_t maxsize = 1000);
    ~BlockQueue();
    bool empty();
    bool full();
    void push_back(const T& item);
    void push_front(const T& item);
    bool pop(T& item);
    bool pop(T& item, int timeout);
    void clear();
    T front();
    T back();
    size_t capacity();
    size_t size();

    void flush();
    void close();
    
private:
    deque<T>deq_;
    mutex mtx_;
    bool isClose_;
    size_t capacity_;
    condition_variable condConsumer_;
    condition_variable condProducer_;  

};

template<typename T>
BlockQueue<T>::BlockQueue(size_t maxsize):capacity_(maxsize){
    assert(maxsize > 0);
    isClose_ = false;
}

template<typename T>
BlockQueue<T>::~BlockQueue(){
    close();
}

template<typename T>
void BlockQueue<T>::close(){
    clear();
    isClose_ = true;
    condConsumer_.notify_all();
    condProducer_.notify_all();
}

//唤醒消费者
template<typename T>
void BlockQueue<T>::flush(){
    condConsumer_.notify_all();
}


template<typename T>
void BlockQueue<T>::clear(){
    //使用lock_guard方式上锁并管理锁，控制互斥锁的访问，在生命周期结束时，自动释放锁
    lock_guard<mutex>locker(mtx_);
    deq_.clear();
}

template<typename T>
bool BlockQueue<T>::empty(){
    lock_guard<mutex>locker(mtx_);
    return deq_.empty();
}


template<typename T>
bool BlockQueue<T>::full(){
    lock_guard<mutex>locker(mtx_);
    return deq_.size() >= capacity_;
}

template<typename T>
T BlockQueue<T>::front(){
    lock_guard<mutex>locker(mtx_);
    return deq_.front();
}

template<typename T>
T BlockQueue<T>::back(){
    lock_guard<mutex>locker(mtx_);
    return deq_.back();
}

template<typename T>
size_t BlockQueue<T>::capacity(){
    lock_guard<mutex>locker(mtx_);
    return capacity_;
}

template<typename T>
size_t BlockQueue<T>::size(){
    lock_guard<mutex>locker(mtx_);
    return deq_.size();
}


template<typename T>
void BlockQueue<T>::push_back(const T&item){
    //当需要更灵活的锁定行为，如在代码块中暂时解锁互斥量、将锁的所有权转移或与条件变量配合时，使用 unique_lock
    //可与 std::condition_variable 配合使用，用于等待条件满足
    unique_lock<mutex>locker(mtx_);
    while(deq_.size() >= capacity_){
        condProducer_.wait(locker);
    }
    deq_.push_back(item);
    condConsumer_.notify_one();
}

template<typename T>
void BlockQueue<T>::push_front(const T&item){
    unique_lock<mutex>locker(mtx_);
    while(deq_.size() >= capacity_){
        condProducer_.wait(locker);
    }
    deq_.push_front(item);
    condConsumer_.notify_one();
}

template<typename T>
bool BlockQueue<T>::pop(T&item){
    unique_lock<mutex>locker(mtx_);
    while(deq_.empty()){
        condConsumer_.wait(locker);
    }
    item = deq_.front();
    deq_.pop_front();
    condProducer_.notify_one();
    return true;
}

template<typename T>
bool BlockQueue<T>::pop(T&item, int timeout){
    unique_lock<mutex>locker(mtx_);
    while(deq_.empty()){
        //设置超时等待时间为timeout，如果等待超过timeout还没被唤醒，就返回错误
        if(condConsumer_.wait_for(locker, std::chrono::seconds(timeout)) == 
        std::cv_status::timeout){
            return false;
        }
        if(isClose_){
            return false;
        }
    }
    item = deq_.front();
    deq_.pop_front();
    condProducer_.notify_one();
    return true;
}


# endif