#include "epoller.h"

Epoller::Epoller(int maxEvent):epollFd_(epoll_create(512)), events_(maxEvent){
    assert(epollFd_ >= 0 && events_.size() > 0);
}

Epoller::~Epoller(){
    close(epollFd_);
}

//events:需要监听的事件类型（如可读、可写、异常等），是一个位掩码。
bool Epoller::AddFd(int fd, uint32_t events){
    if(fd < 0)return false;
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    //调用 epoll_ctl，将文件描述符 fd 添加到 epoll 实例中，并指定监听的事件类型 events
    return epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) == 0;//调用成功返回0
}

bool Epoller::ModFd(int fd, uint32_t events){
    if(fd < 0)return false;
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    return epoll_ctl(epollFd_ , EPOLL_CTL_MOD, fd, &ev) == 0;
}

bool Epoller::DelFd(int fd){
    if(fd < 0)return false;
    epoll_event ev = {0};
    return epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, &ev) == 0;
}

int Epoller::Wait(int timeoutMs){
    //用于等待 epoll 实例中的文件描述符上发生事件,返回事件的数量
    return epoll_wait(epollFd_, &events_[0], static_cast<int>(events_.size()), timeoutMs);
}

int Epoller::GetEventFd(size_t i)const{
    assert(i < events_.size() && i >= 0);
    return events_[i].data.fd;
}

uint32_t Epoller::GetEvents(size_t i)const{
    assert(i < events_.size() && i >= 0);
    return events_[i].events;
}