#include "webserver.h"

using namespace std;

WebServer::WebServer(int port,int trigMode, int timeoutMs, bool OptLinger,
    int sqlPort, const char* sqlUser, const char* sqlPwd,
    const char* dbName, int connPoolNumm ,int threadNum,
    bool openLog, int logLevel, int logQueSize):
    port_(port), openLinger_(OptLinger), timeoutMs_(timeoutMs), isClose_(false),
            timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller()){

    srcDir_ = getcwd(nullptr, 256);
    assert(srcDir_);
    strncat(srcDir_, "/resources/", 16);
    HttpConn::userCount = 0;
    HttpConn::srcDir = srcDir_;

    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNumm);
    InitEventMode_(trigMode);
    if(!InitSocket_()) isClose_ = true;

    if(openLog){
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        if(isClose_){
            LOG_ERROR("======== Server init error! ========");
        }
        else{
            LOG_INFO("======== Server init ========");
            LOG_INFO("Port: %d, OpenLinger: %s", port_, openLinger_ ? "true" : "false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s", 
            (listenEvent_ & EPOLLET ? "ET" : "LT"),
            (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNumm, threadNum);
        }
    }
}

WebServer::~WebServer(){
    close(listenFd_);
    isClose_ = true;
    free(srcDir_);
    SqlConnPool::Instance()->ClosePool();
}

void WebServer::InitEventMode_(int trigMode){
    listenEvent_ = EPOLLRDHUP;              //监听事件，EPOLLRDHUP，表示监听对端关闭连接的事件
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP; //连接事件，EPOLLONESHOT 确保事件只会触发一次，除非重新注册。
    switch (trigMode){
        case 0:
            break;
        case 1:
            connEvent_ |= EPOLLET;  //使用边缘触发
            break;
        case 2:
            listenEvent_ |= EPOLLET;
            break;
        case 3:
            listenEvent_ |= EPOLLET;
            connEvent_ |= EPOLLET;
            break;
        default:
            listenEvent_ |= EPOLLET;
            connEvent_ |= EPOLLET;
            break;
    }
    HttpConn::isET = (connEvent_ & EPOLLET);
}

void WebServer::Start(){
    int timeMS = -1;
    
        LOG_INFO("======== Server start ========");
    
    while(!isClose_){
        if(timeoutMs_ > 0){
            timeMS = timer_->GetNextTick();
        }
        int eventCnt = epoller_->Wait(timeMS);
        for(int i = 0; i < eventCnt; i ++){
            //处理事件
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);
            if(fd == listenFd_){
                DealListen_();
            }else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                //对端关闭连接（半关闭）\ 对端完全关闭连接 \ 发生错误
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]);
            }else if(events & EPOLLIN){
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]);
            }else if(events & EPOLLOUT){
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]);
            }else{
                LOG_ERROR("Unexpected event");
            }
        }
    }

}

void WebServer::SendError_(int fd, const char* info){
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if(ret < 0){
        LOG_WARN("send error to client[%d] error!", fd);
    }
    close(fd);
}

void WebServer::CloseConn_(HttpConn* client){
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());
    epoller_->DelFd(client->GetFd());
    client->Close();
}

void WebServer::AddClient_(int fd, sockaddr_in addr){
    assert(fd > 0);
    users_[fd].init(fd, addr);
    if(timeoutMs_ > 0){
        //超时后回调CloseConn_()
        //成员函数需要通过对象指针调用，因此绑定成员函数时需要绑定对象指针，&users_[fd]是传入的参数
        timer_->add(fd, timeoutMs_, std::bind(&WebServer::CloseConn_, this, &users_[fd]));
    }
    epoller_->AddFd(fd, EPOLLIN | connEvent_);
    //将fd 设置为非阻塞模式，非阻塞模式下，read 和 write 函数会立即返回，而不是阻塞等待数据可用
    SetFdNonblock(fd);
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

//当客户端通过 connect 连接到服务器时，连接请求会被放入监听套接字的连接队列中。
//服务器调用 accept 从队列中取出一个连接。
void WebServer::DealListen_(){
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do{
        //addr用于存储客户端的地址信息
        int fd = accept(listenFd_, (struct sockaddr *)&addr, &len);
        if(fd <= 0)return;
        else if(HttpConn::userCount >= MAX_FD){//超出最大数量
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
    }while(listenEvent_ & EPOLLET);//如果是边缘触发就循环处理，水平触发则只调用一次
}

//处理读事件，将onread加入线程池的任务队列中
void WebServer::DealRead_(HttpConn* client){
    assert(client);
    ExtentTime_(client);
    threadpool_->AddTask(std::bind(&WebServer::OnRead_,this,client));
}

void WebServer::DealWrite_(HttpConn* client){
    assert(client);
    ExtentTime_(client);
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_,this,client));
}

void WebServer::ExtentTime_(HttpConn* client){
    assert(client);
    if(timeoutMs_ > 0)timer_->adjust(client->GetFd(), timeoutMs_);
}

void WebServer::OnRead_(HttpConn* client){
    assert(client);
    int ret = -1;
    int readErrno = 0;
    ret = client->read(&readErrno);
    if(ret <= 0 && readErrno != EAGAIN){//无数据可读
        CloseConn_(client);
        return ;
    }
    OnProcess_(client);
}

void WebServer::OnProcess_(HttpConn* client){
    if(client->process()){
        //返回 true，表示处理成功，并且需要向客户端发送响应数据。
        //将事件设置为 connEvent_ | EPOLLOUT，表示监听写事件（准备向客户端发送数据）
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    }else{
        //否则继续监听读事件，等待接收客户端发送数据
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}

void WebServer::OnWrite_(HttpConn* client){
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno);
    if(client->ToWriteBytes() == 0){//所有数据都写入了
        if(client->IsKeepAlive()){//连接需要保持，切换监听事件，监听读，等客户端发送新数据
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
            return ;
        }
    }else if(ret < 0){
        if(writeErrno == EAGAIN){//缓冲区写满了，监听写，等缓冲区可写时继续写入数据
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
        }
    }
    CloseConn_(client);
}

bool WebServer::InitSocket_(){
    int ret;
    struct sockaddr_in addr;
    if(port_ > 65535 || port_ < 1024){
        LOG_ERROR("Port:%d error!", port_);
        return false;
    }
    addr.sin_family = AF_INET;//使用 IPv4 地址
    addr.sin_addr.s_addr = htonl(INADDR_ANY);//即监听所有网络接口的连接请求
    addr.sin_port = htons(port_);//将主机字节序（host byte order）转换为网络字节序（network byte order）

    //配置优雅关闭
    
    struct linger optLinger = {0};
    if(openLinger_){
        optLinger.l_onoff = 1;//表示启用SO_LINGER
        optLinger.l_linger = 1;//延迟等待1s，等待数据发送完毕或连接关闭
    }

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);//使用IPV4,使用TCP协议
    if(listenFd_ < 0){
        LOG_ERROR("Create socket error!", port_);
        return false;
    }
    //设置套接字的选项
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if(ret < 0){
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }
    

    int optval = 1;
    //SO_REUSEADDR:允许多个套接字绑定到相同的地址和端口(即使仍处于 TIME_WAIT 状态),服务器重启时可直接复用端口
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if(ret < 0){
        LOG_ERROR("set socket setsockopt error!");
        close(listenFd_);
        return false;
    }

    //绑定套接字到指定的地址和端口
    ret = bind(listenFd_, (struct sockaddr*) &addr, sizeof(addr));
    if(ret < 0){
        LOG_ERROR("Bind Port:%d error: %s", port_, strerror(errno));
        printf("Bind Port:%d error: %s\n", port_, strerror(errno));
        // fflush(stdout);
        close(listenFd_);
        return false;
    }
    //监听套接字，监听队列最大为6
    ret = listen(listenFd_, 6);
    if(ret < 0){
        LOG_ERROR("Listen Port:%d error!",port_);
        close(listenFd_);
        return false;
    }
    //添加到epoll监听器
    ret = epoller_->AddFd(listenFd_, listenEvent_ | EPOLLIN);
    if(ret == 0){
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }
    SetFdNonblock(listenFd_);
    LOG_INFO("Server port:%d", port_);
    return true;
}

int WebServer::SetFdNonblock(int fd){
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}
