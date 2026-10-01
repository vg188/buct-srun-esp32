#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cstdarg>
#include <type_traits>
using std::min;
class String : public std::string {
public:
    using std::string::string;
    using std::string::operator=;
    String() = default;
    String(const std::string& s) : std::string(s) {}
    template<typename T, typename std::enable_if<std::is_integral<T>::value,int>::type = 0>
    String(T n) : std::string(std::to_string(n)) {}
    bool startsWith(const char* p) const { return compare(0,strlen(p),p)==0; }
    bool endsWith(const char* p) const {
        size_t n=strlen(p); return size()>=n && compare(size()-n,n,p)==0;
    }
    int indexOf(const String& p, size_t from=0) const {
        size_t n=find(p,from); return n==npos ? -1 : int(n);
    }
    int indexOf(char p, size_t from=0) const {
        size_t n=find(p,from); return n==npos ? -1 : int(n);
    }
    String substring(size_t from, size_t end) const { return substr(from,end-from); }
    double toDouble() const { return strtod(c_str(),nullptr); }
    void concat(const char* p, size_t n) { append(p,n); }
};
inline size_t strlcpy(char* dst, const char* src, size_t n) {
    size_t len=strlen(src); if(n) { size_t k=min(n-1,len); memcpy(dst,src,k); dst[k]=0; } return len;
}
extern uint32_t testClock;
inline uint32_t millis() { return testClock; }
inline void delay(uint32_t ms) { testClock+=ms; }
#define OUTPUT 1
inline void pinMode(int,int) {}
inline void digitalWrite(int,bool) {}
struct SerialMock {
    bool attached=true;
    int writes=0, begins=0;
    void begin(int) { ++begins; }
    std::string output;
    int printf(const char* fmt,...) {
        ++writes;
        char buffer[1024]; va_list args; va_start(args,fmt);
        int n=vsnprintf(buffer,sizeof(buffer),fmt,args); va_end(args);
        output += buffer; return n;
    }
    operator bool() const { return attached; }
};
extern SerialMock Serial;
