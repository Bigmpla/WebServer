#include "httprequest.h"
using namespace std;

const unordered_set<string>HttpRequest::DEFAULT_HTML{
    "/index", "/register", "/login", "/welcome", "/video", "/picture"
};

const unordered_map<string, int>HttpRequest::DEFAULT_HTML_TAG{
    {"/register.html", 0}, {"/login.html", 1}
};

void HttpRequest::Init(){
    method_ = path_ = version_ = body_ = "";
    state_ = REQUEST_LINE;
    header_.clear();
    post_.clear();
}

bool HttpRequest::IsKeepAlive()const{
    if(header_.count("Connection")){
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}

bool HttpRequest::parse(Buffer& buff){
    const char CRLF[] = "\r\n";
    if(buff.ReadableBytes() <= 0){
        return false;
    }
    while(buff.ReadableBytes() && state_ != FINISH){
        //查找 可读 范围内第一个 crlf的 子序列， 返回的是crlf的位置
        const char* lineEnd = search(buff.Peek(), buff.BeginWriteConst(),CRLF, CRLF + 2);
        std::string line(buff.Peek(), lineEnd);
        switch (state_)
        {
        case REQUEST_LINE:
            if(!ParseRequestLine_(line)){
                return false;
            }
            parsePath_();
            break;
        case HEADERS:
            ParseHeader_(line);
            if(buff.ReadableBytes() <= 2){
                state_ = FINISH;
            }
            break;
        case BODY:
            parseBody_(line);
            break;
        default:
            break;
        }
        if(lineEnd == buff.BeginWrite()){break;}
        buff.RetrieveUntil(lineEnd + 2);
    }
    LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(),path_.c_str(), version_.c_str());
    return true;
}

void HttpRequest::parsePath_(){
    if(path_ == "/"){
        path_ = "/index.html";
    }else{
        for(auto& item: DEFAULT_HTML){
            if(item == path_){
                path_ += ".html";
                break;
            }
        }
    }
}

bool HttpRequest::ParseRequestLine_(const string& line){
    //http请求行格式为：<Method> <Path> HTTP/<Version>
    regex pattern("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    smatch subMatch;
    if(regex_match(line, subMatch, pattern)){
        method_ = subMatch[1];
        path_ = subMatch[2];
        version_ = subMatch[3];
        state_ = HEADERS;
        return true;
    }
    LOG_ERROR("RequestLine Error");
    return false;
}

void HttpRequest::ParseHeader_(const string& line){
    //如：Content-Type: text/html
    //subMatch[1] 是 "Content-Type"。
    //subMatch[2] 是 " text/html"
    regex patten("^([^:]*): ?(.*)$");
    smatch subMatch;
    if(regex_match(line, subMatch, patten)){
        header_[subMatch[1]] = subMatch[2];
    }else{
        state_ = BODY;
    }
}

void HttpRequest::parseBody_(const string& line){
    body_ = line;
    ParsePost_();
    state_ = FINISH;
    LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());
}

int HttpRequest::ConvertHex(char ch){
    if(ch >= 'A' && ch <= 'F')return ch -'A' + 10;
    if(ch >= 'a' && ch <= 'f')return ch -'a' + 10;
    return ch;
}

void HttpRequest::ParsePost_(){
    //Content-Type: application/x-www-form-urlencoded,表示请求体中的数据是以 URL 编码的键值对形式提交的
    if(method_ == "POST" && header_["Content-Type"]== "application/x-www-form-urlencoded"){
        ParseFromUrlEncoded_();
        if(DEFAULT_HTML_TAG.count(path_)){
            // unordered_map 是 const的，必须使用 find 方法，因为 operator[] 不能用于 const 容器
            int tag = DEFAULT_HTML_TAG.find(path_)->second;
            LOG_DEBUG("Tag:%d",tag);
            if(tag == 0 || tag == 1){//tag = 1表示登陆，为0表示注册
                bool isLogin = (tag == 1);
                if(UserVerify(post_["username"], post_["password"], isLogin)){
                    path_ = "/welcome.html";
                }else{
                    path_ = "/error.html";
                }
            }
        }
    }
}

void HttpRequest::ParseFromUrlEncoded_(){
    if(body_.size() == 0){return ;}
    string key,value;
    int num = 0;
    int n = body_.size();
    int i = 0, j = 0;

    for(;i < n; i++){
        char ch = body_[i];
        switch (ch){
        // = 是键值对的分隔符，= 左边的部分是键，右边的部分是值。
        case '=':
            key = body_.substr(j, i - j);
            j = i + 1;
            break;
        // + 在 URL 编码中表示空格，将其替换为空格字符 ' '
        case '+':
            body_[i] = ' ';
            break;
        // % 是 URL 编码的标志，后面跟随两个十六进制字符（例如 %20 表示空格）
        case '%':
            num = ConvertHex(body_[i + 1]) * 16 + ConvertHex(body_[i + 2]);
            body_[i + 2] = num % 10 + '0';
            body_[i + 1] = num / 10 + '0';
            i += 2;
            break;
        // & 是键值对的分隔符，表示一个键值对的结束
        case '&':
            value = body_.substr(j, i - j);
            j = i + 1;
            post_[key] = value;
            LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
            break;
        default:
            break;
        }
    }
    assert(j <= i);
    if(!post_.count(key) && j < i){//处理最后一个键值对
        value = body_.substr(j, i - j);
        post_[key] = value;
    }
}

bool HttpRequest::UserVerify(const string& name, const string &pwd, bool isLogin){
    
    if(name == "" || pwd == "")return false;
    LOG_INFO("Verify name:%s, pwd:%s", name.c_str(),pwd.c_str());
    MYSQL* sql;
    SqlConnRAII(&sql, SqlConnPool::Instance());
    assert(sql);

    bool flag = false;//标志能否注册
    unsigned int j = 0;
    char order[256] = {0};
    MYSQL_FIELD* fields = nullptr;//存储查询结果
    MYSQL_RES* res = nullptr;//存储查询结构集

    if(!isLogin)flag = true;

    snprintf(order, 256, "SELECT username, password From user WHERE username='%s' LIMIT1",name.c_str());
    LOG_DEBUG("%s",order);

    if(mysql_query(sql,order)){//执行成功返回0
        mysql_free_result(res);
        return false;
    }

    res = mysql_store_result(sql);//查询结果集
    j = mysql_num_fields(res);//列数，2, username 和 password
    fields = mysql_fetch_field(res);//字段信息,

    while(MYSQL_ROW row = mysql_fetch_row(res)){
        LOG_DEBUG("MYSQL ROW: %s %s",row[0], row[1]);
        string password(row[1]);

        if(isLogin){//登陆
            if(pwd == password)flag = true;
            else {
                flag = false;
                LOG_INFO("pwd error!");
            }
        }else{//注册， 但是重名了
            flag = false;
            LOG_INFO("user used!");
        }
    }

    mysql_free_result(res);
    //注册， 且没有重名
    if(!isLogin && flag == true){
        LOG_DEBUG("register!");
        bzero(order, 256);
        snprintf(order, 256, "INSERT INTO user(username, password) VALUES('%s','%s')", name.c_str(), pwd.c_str());
        LOG_DEBUG("%s", order);
        if(mysql_query(sql,order)){
            LOG_DEBUG("Insert error!");
            flag = false;
        }
        flag = true;
    }
    SqlConnPool::Instance()->FreeConn(sql);
    LOG_DEBUG("UserVerify success!");
    return flag;
}

std::string HttpRequest::path()const{
    return path_;
}

std::string& HttpRequest::path(){
    return path_;
}

std::string HttpRequest::method()const{
    return method_;
}

std::string HttpRequest::version()const{
    return version_;
}

std::string HttpRequest::GetPost(const std::string& key)const{
    assert(key != "");
    if(post_.count(key)){
        return post_.find(key)->second;
    }
    return "";
}

std::string HttpRequest::GetPost(const char* key)const{
    assert(key != nullptr);
    if(post_.count(key)){
        return post_.find(key)->second;
    }
    return "";
}



