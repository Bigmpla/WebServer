#include"heaptimer.h"

void HeapTimer::SwapNode_(size_t i, size_t j){
    assert(i >= 0 && i < heap_.size());
    assert(j >= 0 && j < heap_.size());
    swap(heap_[i], heap_[j]);
    ref_[heap_[i].id] = i;
    ref_[heap_[j].id] = j;
}

void HeapTimer::siftup_(size_t i){
    assert(i >= 0 && i < heap_.size());
    size_t pa = (i - 1)/ 2; //从0开始计数
    while(pa >= 0){
        if(heap_[pa] > heap_[i]){
            SwapNode_(i,pa);
            i = pa;
            pa = (i - 1) / 2;
        }else{
            break;
        }
    }
}
//false：没进行下沉， true：进行了下沉
bool HeapTimer::siftdown_(size_t i, size_t n){//n：堆中元素个数
    assert(i >= 0 && i < heap_.size());
    assert(n >= 0 && n <= heap_.size());
    auto index = i;
    auto child = index * 2 + 1;//左子节点
    while(child < n){
        if(child + 1 < n && heap_[child + 1] < heap_[child]){//若有右子节点且右子节点更小
            child ++;
        }
        if(heap_[child] < heap_[index]){
            SwapNode_(index, child);
            index = child;
            child = child * 2 + 1;
        }else{
            break;
        }
    }
    return index > i;
}

void HeapTimer::add(int id, int timeout, const TimeoutCallBack& cb){
    assert(id >= 0);
    size_t i;
    if(!ref_.count(id)){
        i = heap_.size();
        ref_[id] = i;
        heap_.push_back({id,Clock::now() + MS(timeout), cb});
        siftup_(i);
    }else{
        i = ref_[id];
        heap_[i].expires = Clock::now()+ MS(timeout);
        heap_[i].cb = cb;
        if(!siftdown_(i,heap_.size())){
            siftup_(i);
        }
    }
}

void HeapTimer::doWork(int id){
    if(heap_.empty() || !ref_.count(id)){
        return ;
    }
    size_t i = ref_[id];
    TimerNode node = heap_[i];
    node.cb();
    del_(i);
}

void HeapTimer::del_(size_t index){
    assert(!heap_.empty() && index >= 0 && index < heap_.size());
    size_t i = index;
    size_t j = heap_.size() - 1;
    assert(i <= j);
    if(i < j){
        SwapNode_(i,j);
        if(!siftdown_(i, j)){
            siftup_(i);
        }
    }
    ref_.erase(heap_.back().id);
    heap_.pop_back();
}

void HeapTimer::adjust(int id, int timeout){//调整节点的值
    assert(!heap_.empty() && ref_.count(id));
    heap_[ref_[id]].expires = Clock::now() + MS(timeout);
    siftdown_(ref_[id], heap_.size());
}

void HeapTimer::tick(){//清除超时的节点
    if(heap_.empty()){
        return;
    }
    while(!heap_.empty()){
        TimerNode node = heap_.front();
        if(std::chrono::duration_cast<MS>(node.expires - Clock::now()).count() > 0){
            break;
        }
        node.cb();//触发回调函数
        pop();
    }
}

void HeapTimer::pop(){
    assert(!heap_.empty());
    del_(0);
}

void HeapTimer::clear(){
    ref_.clear();
    heap_.clear();
}

int HeapTimer::GetNextTick(){
    tick();
    size_t res = -1;
    if(!heap_.empty()){
        res = std::chrono::duration_cast<MS>(heap_.front().expires - Clock::now()).count();
        if(res < 0)res = 0;
    }
    return res;
}

