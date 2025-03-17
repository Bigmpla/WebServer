#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <regex>
#include <errno.h>
#include <mysql/mysql.h>

#include "../buffer/buffer.h"
#include "../log/log.h"
#include "../pool/sqlconnpool.h"

class HttpRequest{
public:
    enum PARSE_STATE{
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH,
    };

    HttpRequest(){Init();}
    ~HttpRequest()=default;

    void Init();
    bool parse(Buffer& buff);

    std::string path() const;
    std::string& path();
    std::string method() const;
    std::string version() const;
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;

    bool IsKeepAlive() const;

private:
    bool ParseRequestLine_(const std::string& line);
    void ParseHeader_(const std::string& line);
    void parseBody_(const std::string& line);

    void parsePath_();
    void ParsePost_();
    void ParseFromUrlEncoded_();

    static bool UserVerify(const std::string&name, const std::string& pwd, bool isLogin);
    
    PARSE_STATE state_;
    std::string method_, path_, version_, body_;
    std::unordered_map<std::string,std::string>header_;
    std::unordered_map<std::string,std::string>post_;

    static const std::unordered_set<std::string>DEFAULT_HTML;
    static const std::unordered_map<std::string, int>DEFAULT_HTML_TAG;
    static int ConvertHex(char ch);
};

#endif