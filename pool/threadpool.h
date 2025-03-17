#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<queue>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<thread>
#include<assert.h>

class ThreadPool{
public:
    ThreadPool() = default;
    ThreadPool(ThreadPool &&) = default;            //移动构造函数
    // 尽量用make_shared代替new，如果通过new再传递给shared_ptr，内存是不连续的，会造成内存碎片化
    explicit ThreadPool(int threadCount = 8):pool_(std::make_shared<Pool>()){
        assert(threadCount > 0);
        for(int i = 0; i < threadCount; i ++){
            std::thread([this](){
                std::unique_lock<std::mutex>locker(pool_->mtx_);
                while(true){
                    if(!pool_->tasks.empty()){
                        auto task = std::move(pool_->tasks.front());//取出队头的任务,使用move将左值转化为右值，实现移动语意
                        pool_->tasks.pop();
                        locker.unlock();
                        task();//开始执行
                        locker.lock();
                    }else if(pool_->isClosed){
                        break;
                    }else{
                        pool_->cond_.wait(locker);//释放锁并使线程进入等待状态，直到有任务被添加到队列或线程池关闭。
                    }
                }
            }).detach();//线程分离
        }
    }

    ~ThreadPool(){
        if(pool_){
            std::unique_lock<std::mutex>locker(pool_->mtx_);
            pool_->isClosed = true;
        }
        pool_->cond_.notify_all();//唤醒所有线程
    }

    template<typename T>     //T&& :通用引用实现完美转发，即将参数以原始的类型（左值或右值）传递给其他函数
    void AddTask(T&& task){
        {
            std::unique_lock<std::mutex> locker(pool_->mtx_);
            pool_->tasks.emplace(std::forward<T>(task));
        }
        pool_->cond_.notify_one();
    }


private:
    struct Pool{
        std::mutex mtx_;                            //互斥锁
        std::condition_variable cond_;              //条件变量，用于进程间通信
        bool isClosed;
        std::queue<std::function<void()>>tasks;     //任务队列，函数类型为void()
    };
    std::shared_ptr<Pool>pool_;
};


#endif