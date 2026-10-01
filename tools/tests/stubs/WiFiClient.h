#pragma once
#include "Arduino.h"
#include <deque>
#include <vector>
struct Reply {
    bool connected;
    std::string json;
    int code;
    Reply(const char* s, bool c=true, int status=200) : connected(c),json(s),code(status) {}
};
extern std::deque<Reply> replies;
extern std::vector<std::string> requests;
class WiFiClient {
    Reply reply{"",false};
    std::string path, data;
    size_t cursor=0;
    bool open=false;
public:
    void setTimeout(uint32_t) {}
    bool connect(const char*, int, int32_t) {
        if(replies.empty()) { fprintf(stderr,"Unscripted mock HTTP connection\n"); abort(); }
        reply=replies.front(); replies.pop_front(); open=reply.connected; return open;
    }
    int printf(const char* fmt,...) {
        char text[4096]; va_list args; va_start(args,fmt);
        int n=vsnprintf(text,sizeof(text),fmt,args); va_end(args);
        if(strncmp(text,"GET ",4)==0) {
            path=text+4; path=path.substr(0,path.find(' ')); requests.push_back(path);
        }
        return n;
    }
    void print(const char*) {
        auto start=path.find("callback="); std::string body=reply.json;
        if(start!=std::string::npos) {
            auto cb=path.substr(start+9); cb=cb.substr(0,cb.find('&'));
            body=cb+"("+body+")";
        }
        data="HTTP/1.0 "+std::to_string(reply.code)+" OK\r\nContent-Length: "+
             std::to_string(body.size())+"\r\n\r\n"+body;
    }
    String readStringUntil(char sep) {
        auto end=data.find(sep,cursor); if(end==std::string::npos) end=data.size();
        auto out=data.substr(cursor,end-cursor); cursor=min(end+1,data.size()); return out;
    }
    int read(uint8_t* out,size_t n) {
        n=min(n,data.size()-cursor); memcpy(out,data.data()+cursor,n); cursor+=n; return int(n);
    }
    int read() { return cursor<data.size() ? static_cast<unsigned char>(data[cursor++]) : -1; }
    int available() { return int(data.size()-cursor); }
    bool connected() { return open && cursor<data.size(); }
    void stop() { open=false; }
};
