#include "WiFiClient.h"
#include "srun_auth.h"
#include "srun_crypto.h"
#include <cassert>
uint32_t testClock=0;
std::deque<Reply> replies;
std::vector<std::string> requests;
void srunDebugLog(const char*,...) {}
// Protocol parsing/flow tests only: real crypto code is checked against release
// byte-for-byte separately. These fake primitives never produce real credentials.
namespace srun {
void hmacMd5Hex(const char*,size_t,const char*,size_t,char out[33]) { strcpy(out,"00000000000000000000000000000000"); }
void sha1Hex(const char*,size_t,char out[41]) { strcpy(out,"0000000000000000000000000000000000000000"); }
bool buildInfo(const char*,const char*,char* out,size_t) { strcpy(out,"{SRBX1}test"); return true; }
}
const char* online="{\"error\":\"ok\",\"online_ip\":\"192.0.2.7\",\"user_name\":\"test-user\",\"bytes_in\":123,\"bytes_out\":456}";
const char* challenge="{\"challenge\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"client_ip\":\"192.0.2.7\"}";
int main(int argc,char** argv) {
    assert(argc==2); std::string name=argv[1];
    if(name=="offline") {
        replies.emplace_back("{\"error\":\"not_online_error\"}");
        srun::OnlineInfo i{}; assert(srun::getOnlineInfo(i) && !i.online);
    } else if(name=="online") {
        replies.emplace_back(online); srun::OnlineInfo i{};
        assert(srun::getOnlineInfo(i) && i.online && i.bytesIn==123 && i.bytesOut==456);
    } else if(name=="unknown") {
        replies.emplace_back("{\"error\":\"backend_busy\"}");
        srun::OnlineInfo i{}; assert(!srun::getOnlineInfo(i) && !i.online);
    } else if(name=="logout_unknown") {
        replies.emplace_back("{\"error\":\"backend_busy\"}");
        assert(!srun::logout()); assert(requests.size()==1);
    } else if(name=="logout_unreachable") {
        replies.emplace_back("",false); replies.emplace_back("",false);
        assert(!srun::logout()); assert(requests.empty());
    } else if(name=="logout_offline") {
        replies.emplace_back("{\"error\":\"not_online_error\"}");
        assert(srun::logout()); assert(requests.size()==1);
    } else if(name=="logout_ack" || name=="logout_rejected") {
        replies.emplace_back(online);
        replies.emplace_back(name=="logout_ack" ? "{\"error\":\"ok\"}" : "{\"error\":\"failed\"}");
        assert(srun::logout()==(name=="logout_ack"));
        assert(requests.size()==2 && requests.back().find("action=logout")!=std::string::npos);
    } else if(name=="login_ok" || name=="login_error_ok" || name=="login_bad" || name=="login_already") {
        replies.emplace_back(challenge);
        const char* response=name=="login_ok" ? "{\"res\":\"ok\"}" : name=="login_error_ok" ? "{\"error\":\"ok\"}" :
            name=="login_bad" ? "{\"ecode\":\"E2901\"}" : "{\"error\":\"ip_already_online_error\"}";
        replies.emplace_back(response);
        auto expected=name=="login_bad" ? srun::AuthResult::BAD_CREDENTIALS : name=="login_already" ?
            srun::AuthResult::ALREADY_ONLINE : srun::AuthResult::OK;
        assert(srun::login("test-user","fake-password","1")==expected);
        assert(requests.size()==2 && requests.back().find("action=login")!=std::string::npos);
    } else if(name=="challenge_ip_fallback") {
        replies.emplace_back("",false); replies.emplace_back(challenge);
        replies.emplace_back("{\"res\":\"ok\"}");
        assert(srun::login("test-user","fake-password","1")==srun::AuthResult::OK);
    } else { assert(false); }
    assert(replies.empty()); printf("PASS protocol %s\n",name.c_str());
}
