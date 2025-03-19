#include "buffer.h"

Buffer::Buffer(int initBuffSize):buffer_(initBuffSize),readPos_(0),writePos_(0){};

size_t Buffer::WritableBytes()const{
    return buffer_.size() - writePos_;
}

size_t Buffer::ReadableBytes()const{
    return writePos_ - readPos_;
}

size_t Buffer::PrependableBytes()const{
    return readPos_;
}

const char* Buffer::Peek()const{
    return &buffer_[readPos_];
}

void Buffer::MakeSpace_(size_t len){
    if(PrependableBytes() + WritableBytes() < len){//重新分配
        buffer_.resize(writePos_ + len + 1);
    }else{//前移
        size_t readable = ReadableBytes();
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_ , BeginPtr_());
        readPos_ = 0;
        writePos_ = readable;
        assert(readable == ReadableBytes());
    }
}

void Buffer::EnsureWriteable(size_t len){
    if(len > WritableBytes()){
        MakeSpace_(len);
    }
    assert(len <= WritableBytes());
}

void Buffer::HasWritten(size_t len){
    writePos_ += len;
}

void Buffer::Retrieve(size_t len){
    assert(len <= ReadableBytes());
    readPos_ += len;
}

//读到end位置
void Buffer::RetrieveUntil(const char* end){
    assert(Peek() <= end);
    Retrieve(end - Peek());
}

//清空所有数据，全部归0
void Buffer::RetrieveAll(){
    bzero(&buffer_[0], buffer_.size());
    readPos_ = 0, writePos_ = 0;
}

std::string Buffer::RetrieveAllToStr(){
    std::string s(Peek(), ReadableBytes());
    RetrieveAll();
    return s;
}

const char* Buffer::BeginWriteConst() const{
    return &buffer_[writePos_];
}

char* Buffer::BeginWrite(){
    return &buffer_[writePos_];
}

void Buffer::Append(const char* str, size_t len){
    assert(str);
    EnsureWriteable(len);
    std::copy(str, str+ len, BeginWrite());
    HasWritten(len);
}

void Buffer::Append(const std::string& str){
    Append(str.c_str(),str.size());
}

void Buffer::Append(const void* data, size_t len){
    assert(data);
    Append(static_cast<const char*>(data), len);
}

//把另外一个buffer可读的数据写入当前buffer中
void Buffer::Append(const Buffer& buff){
    Append(buff.Peek(), buff.ReadableBytes());
}

ssize_t Buffer::ReadFd(int fd, int* Errno){
    char buff[65535];
    struct iovec iov[2];
    size_t writeable = WritableBytes();

//分散读，优先将数据读到iov[0]，空间不够再用iov[1]
    iov[0].iov_base = BeginWrite();
    iov[0].iov_len = writeable;
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);

//readv 可以读到多个缓冲区
    ssize_t len = readv(fd, iov, 2);
    if(len < 0){
        *Errno = errno;
    }else if(static_cast<size_t>(len) <= writeable){//当前buffer就够写下
        writePos_ += len;
    }else{//当前buffer写不下，用了iov[1]的buffer
        writePos_ = buffer_.size();
        Append(buff,static_cast<size_t>(len - writeable));
    }
    return len;
}

ssize_t Buffer::WriteFd(int fd, int* Errno){
    ssize_t len = write(fd, Peek(), ReadableBytes());
    if(len < 0){
        *Errno = errno;
        return len;
    }
    Retrieve(len);
    return len;
}

char* Buffer::BeginPtr_(){
    return &buffer_[0];
}

const char* Buffer::BeginPtr_() const{
    return &buffer_[0];
}
