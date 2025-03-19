#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H

#include<queue>
#include<unordered_map>
#include<time.h>
#include<algorithm>
#include<arpa/inet.h>
#include<functional>
#include<assert.h>
#include<chrono>
#include"../log/log.h"

typedef std::function<void()>TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

struct TimerNode{
    int id;
    TimeStamp expires;
    TimeoutCallBack cb;
    bool operator < (const TimerNode& t){
        return expires < t.expires; 
    }
    bool operator > (const TimerNode& t){
        return expires > t.expires;
    }
};

class HeapTimer{
public:
    HeapTimer(){ heap_.reserve(64);}
    ~HeapTimer(){ clear();}
    void adjust(int id, int NewExpires);
    void add(int id, int timeOut, const TimeoutCallBack& cb);//添加一个定时器
    void doWork(int id);
    void clear();
    void tick();
    void pop();
    int GetNextTick();

private:
    void del_(size_t i);//删除指定定时器
    void siftup_(size_t i);//对堆向上调整(堆排序相关内容)
    bool siftdown_(size_t i, size_t n);//对堆向上调整
    void SwapNode_(size_t i, size_t j);//交换两个结点位置
    std::vector<TimerNode>heap_;
    std::unordered_map<int, size_t>ref_;// key:id value:vector的下标
};




#endif