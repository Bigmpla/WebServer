#include "sqlconnpool.h"

SqlConnPool* SqlConnPool::Instance(){
    static SqlConnPool pool;
    return &pool;
}

void SqlConnPool::Init(const char* host, int port, const char* user, 
        const char* pwd, const char* dbName, int connSize = 10){
    assert(connSize > 0);
    for(int i = 0; i < connSize; i++){
        //连接数据库的标准操作
        MYSQL* conn = nullptr;
        conn = mysql_init(conn);
        if(!conn){
            LOG_ERROR("Mysql init error!");
            assert(conn);
        }
        conn = mysql_real_connect(conn, host, user, pwd, dbName, port, nullptr, 0);
        if(!conn){
            LOG_ERROR("Mysql Connect error");
        }
        connQue_.push(conn);
    }
    MAX_CONN_ = connSize;
    //pshared = 0 表示该信号量不可共享
    sem_init(&semID_, 0, MAX_CONN_);

}

MYSQL* SqlConnPool::GetConn(){
    MYSQL* conn = nullptr;
    if(connQue_.empty()){
        LOG_WARN("SqlConnPool busy!");
        return nullptr;
    }
    sem_wait(&semID_);
    {
        lock_guard<mutex>locker(mtx_);
        conn = connQue_.front();
        connQue_.pop();
    }
    return conn;
}

//存入连接池
void SqlConnPool::FreeConn(MYSQL* sql){
    assert(sql);
    lock_guard<mutex>locker(mtx_);
    connQue_.push(sql);
    sem_post(&semID_);
}

void SqlConnPool::ClosePool(){
    lock_guard<mutex>locker(mtx_);
    while(!connQue_.empty()){
        auto conn = connQue_.front();
        connQue_.pop();
        mysql_close(conn);
    }
    mysql_library_end();
}

int SqlConnPool::GetFreeConnCount(){
    lock_guard<mutex>locker(mtx_);
    return connQue_.size();
}

