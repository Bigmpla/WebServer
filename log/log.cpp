#include "log.h"

//init 和 write函数是重点
Log* Log::Instance(){
    static Log log;
    return &log;
}

void Log::FlushLogThread(){
    Log::Instance()->AsyncWrite_();
}

void Log::AsyncWrite_(){
    string str = "";
    while(deque_->pop(str)){
        lock_guard<mutex>locker(mtx_);
        fputs(str.c_str(), fp_);
    }
}

void Log::flush(){
    if(isAsync_){
        deque_->flush();//唤醒消费者，处理掉剩下的任务
    }
    fflush(fp_);//清空输入缓冲区
}

Log::Log(){
    isAsync_ = false;
    lineCount_ = 0;
    toDay_ = 0;
    fp_ = nullptr;
    deque_ = nullptr;
    writeThread_ = nullptr;
}

Log::~Log(){
    while(!deque_->empty()){
        deque_->flush();
    }
    deque_->close();
    writeThread_->join(); //等待线程，同步
    if(fp_){
        lock_guard<mutex>locker(mtx_);
        flush();
        fclose(fp_);
    }
}

void Log::AppendLogLevelTitle_(int level) {
    switch(level) {
    case 0:
        buff_.Append("[debug]: ", 9);
        break;
    case 1:
        buff_.Append("[info] : ", 9);
        break;
    case 2:
        buff_.Append("[warn] : ", 9);
        break;
    case 3:
        buff_.Append("[error]: ", 9);
        break;
    default:
        buff_.Append("[info] : ", 9);
        break;
    }
}

int Log::GetLevel() {
    lock_guard<mutex> locker(mtx_);
    return level_;
}

void Log::SetLevel(int level) {
    lock_guard<mutex> locker(mtx_);
    level_ = level;
}

void Log::init(int level, const char* path, const char* suffix, int maxQueCapacity){
    level_ = level;
    path_ = path;
    suffix_ = suffix_;
    isOpen_ = true;
    if(maxQueCapacity){//不为0就是异步日志
        isAsync_ = true;
        if(!deque_){
            unique_ptr<BlockQueue<std::string>>newdq(new BlockQueue<std::string>);
            deque_ = move(newdq);
            unique_ptr<thread>newThread;
            writeThread_ = move(newThread);

        }
    }else{
        isAsync_ = false;
    }

    lineCount_ = 0;

    time_t timer = time(nullptr);//获取从1970至今过了多少秒
    struct tm* systime = localtime(&timer);//用localtime将秒数转化为struct tm结构体
    char fileName[LOG_NAME_LEN] = {0};

    toDay_ = systime->tm_mday;
    //将格式化的字符串输入到filname中
    snprintf(fileName, LOG_NAME_LEN - 1, 
    "%s/%04d_%02d_%02d%s", path_, systime->tm_year + 1900, systime->tm_mon + 1,systime->tm_mday, suffix_);

    {
        lock_guard<mutex>locker(mtx_);
        buff_.RetrieveAll();
        if(fp_){//如果之前打开过，清空并重新打开
            flush();
            fclose(fp_);
        }
        fp_ = fopen(fileName, "a");//追加模式
        if(!fp_){
            mkdir(path_ ,0777);
            fp_ = fopen(fileName, "a");
        }
        assert(fp_ != nullptr);
    }

}

void Log::write(int level, const char *format, ...){
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);            //获取时间
    time_t tSec = now.tv_sec;
    struct tm *sysTime = localtime(&tSec);  //转化为本地时间
    struct tm t = *sysTime;                 //复制其值
    va_list vaList;                         //处理可变参数列表
    
    //日期不是今天，或者超过最大行数, 就新建一个日志进行写
    if(toDay_ != t.tm_mday || (lineCount_ && (lineCount_ % MAX_LINES == 0))){
        unique_lock<mutex>locker(mtx_);
        locker.unlock();
        char newFile[LOG_NAME_LEN];
        char tail[36] = {0};
        snprintf(tail, 36, "%04d_%02d_%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

        if(toDay_ != t.tm_mday){
            snprintf(newFile,LOG_NAME_LEN - 72, "%s/%s%s", path_, tail, suffix_);
            toDay_ = t.tm_mday;
            lineCount_ = 0;
        }else{
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s-%d%s", path_, tail, (lineCount_  / MAX_LINES), suffix_);
        }

        locker.lock();
        flush();
        fclose(fp_);
        fp_ = fopen(newFile,"a");
        assert(fp_ != nullptr);
    }
    
    //生成一条日志信息，用buff存放
    {
        unique_lock<mutex>locker(mtx_);
        lineCount_++;
        int n = snprintf(buff_.BeginWrite(), 128, "%d-%02d-%02d %02d:%02d:%02d.%06ld",
        t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, now.tv_usec);
        buff_.HasWritten(n);

        AppendLogLevelTitle_(level);
        //按照格式写入
        va_start(vaList, format);
        int m = vsnprintf(buff_.BeginWrite(), buff_.WritableBytes(), format, vaList);
        va_end(vaList);

        buff_.HasWritten(m);
        buff_.Append("\n\0",2);

        if(isAsync_ && deque_ && !deque_->full()){//异步日志，先存在缓冲区里
            deque_->push_back(buff_.RetrieveAllToStr());
        }else{//同步日志，直接写入文件里
            fputs(buff_.Peek(), fp_);
        }
        buff_.RetrieveAll();
    }
}