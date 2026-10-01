// All network/serial functions are in-memory fakes; production sketch and
// production HTTP/controller implementations are compiled unchanged.
#include "WiFiClient.h"
#include "WiFi.h"
#include "srun_crypto.h"
#include <cassert>
uint32_t testClock=0;
bool testAssociated=false;
SerialMock Serial;
WiFiMock WiFi;
std::deque<Reply> replies;
std::vector<std::string> requests;
namespace srun {
void hmacMd5Hex(const char*,size_t,const char*,size_t,char out[33]) { strcpy(out,"00000000000000000000000000000000"); }
void sha1Hex(const char*,size_t,char out[41]) { strcpy(out,"0000000000000000000000000000000000000000"); }
bool buildInfo(const char*,const char*,char* out,size_t) { strcpy(out,"{SRBX1}test"); return true; }
}
#include "BUCT_AutoAuth.ino"

const char* ONLINE="{\"error\":\"ok\",\"online_ip\":\"192.0.2.7\",\"user_name\":\"test-user\"}";
const char* CHALLENGE="{\"challenge\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"client_ip\":\"192.0.2.7\"}";
void runAfter(uint32_t ms) { testClock+=ms; loop(); }
void ready() {
    testAssociated=true; WiFi.statusValue=WL_CONNECTED;
    WiFi.ip.value=1; WiFi.gateway.value=1; loop();
}
void queueFailedProbes() { for(int i=0;i<3;++i) replies.emplace_back("",false); }
void queueOnline() { replies.emplace_back("",true,204); replies.emplace_back(ONLINE); }
void assertNoLogout() {
    for(const auto& req:requests) assert(req.find("action=logout")==std::string::npos);
}
int main(int argc,char** argv) {
    assert(argc==2); std::string name=argv[1];
    Serial.attached=false;
    setup();
    assert(testClock==0 && WiFi.starts==1 && Serial.writes==0);
    assert(!WiFi.persistentEnabled && !WiFi.autoReconnect && WiFi.sleep && WiFi.modeValue==WIFI_STA);
    if(name=="diagnostics_monitor") {
        diagnosticsTick(); assert(Serial.output.empty() && requests.empty());
        Serial.attached=true; diagnosticsTick();
        assert(Serial.output.find("MONITOR attached")!=std::string::npos);
        assert(Serial.output.find("logout=0")!=std::string::npos && requests.empty());
        Serial.output.clear(); testClock=14999; diagnosticsTick(); assert(Serial.output.empty());
        testClock=15000; diagnosticsTick(); assert(!Serial.output.empty() && requests.empty());
        Serial.output.clear(); Serial.attached=false; diagnosticsTick();
        Serial.attached=true; diagnosticsTick();
        assert(Serial.output.find("MONITOR attached")!=std::string::npos && requests.empty());
        queueOnline(); ready(); runAfter(10000);
        Serial.output.clear(); testClock+=15000; diagnosticsTick();
        assert(Serial.output.find("STATUS state=ONLINE")!=std::string::npos);
        assert(Serial.output.find("probe=1(last=1)")!=std::string::npos);
        assert(Serial.output.find("login=0(last=-1) logout=0")!=std::string::npos);
        assert(requests.size()==2);
    } else if(name=="usb_power_only") {
        queueOnline(); ready(); runAfter(10000);
        assert(controller.state()==buct::State::ONLINE && Serial.writes==0);
    } else if(name=="serial_attach_later") {
        runAfter(30000); Serial.attached=true; runAfter(250);
        assert(Serial.writes>0 && WiFi.starts==2);
        queueOnline(); ready(); runAfter(10000); assert(controller.state()==buct::State::ONLINE);
    } else if(name=="late_ap_end_to_end") {
        for(int i=0;i<20;++i) { runAfter(30000); runAfter(250); }
        assert(WiFi.starts==21 && requests.empty());
        testAssociated=true; loop(); runAfter(85000);
        assert(WiFi.disconnects==20 && requests.empty());
        queueFailedProbes(); replies.emplace_back("{\"error\":\"not_online_error\"}");
        replies.emplace_back(CHALLENGE); replies.emplace_back("{\"res\":\"ok\"}");
        ready(); runAfter(10000); assert(controller.state()==buct::State::VERIFY_LOGIN);
        replies.emplace_back("",true,204); runAfter(1500);
        assert(controller.state()==buct::State::ONLINE);
    } else if(name=="dhcp_independent_window") {
        runAfter(29000); testAssociated=true; loop();
        runAfter(85000); assert(WiFi.disconnects==0 && requests.empty());
        runAfter(5000); assert(WiFi.disconnects==1);
        runAfter(250); assert(WiFi.starts==2);
    } else if(name=="gateway_required") {
        testAssociated=true; WiFi.statusValue=WL_CONNECTED; WiFi.ip.value=1;
        loop(); runAfter(40000); assert(requests.empty() && WiFi.disconnects==0);
        queueOnline(); WiFi.gateway.value=1; loop(); runAfter(10000);
        assert(controller.state()==buct::State::ONLINE);
    } else if(name=="wifi_status_required") {
        testAssociated=true; WiFi.ip.value=1; WiFi.gateway.value=1;
        loop(); runAfter(40000); assert(requests.empty());
        queueOnline(); WiFi.statusValue=WL_CONNECTED; loop(); runAfter(10000);
        assert(controller.state()==buct::State::ONLINE);
    } else if(name=="disconnect_rejected") {
        WiFi.disconnectOK=false;
        runAfter(30000); runAfter(250); assert(WiFi.starts==2);
        queueOnline(); ready(); runAfter(10000);
        assert(controller.state()==buct::State::ONLINE);
    } else if(name=="fatal_end_to_end") {
        queueFailedProbes(); replies.emplace_back("{\"error\":\"not_online_error\"}");
        replies.emplace_back(CHALLENGE); replies.emplace_back("{\"ecode\":\"E2901\"}");
        ready(); runAfter(10000); assert(controller.state()==buct::State::FATAL_CREDENTIALS);
        size_t n=requests.size();
        WiFi.statusValue=WL_DISCONNECTED; testAssociated=false;
        for(int i=0;i<100;++i) runAfter(60000);
        ready(); runAfter(60000); assert(requests.size()==n);
    } else if(name=="mismatch_no_logout") {
        replies.emplace_back("",true,204);
        replies.emplace_back("{\"error\":\"ok\",\"online_ip\":\"192.0.2.7\",\"user_name\":\"other-user\"}");
        ready(); runAfter(10000); assert(controller.state()==buct::State::ONLINE);
        assert(requests.size()==2);
    } else if(name=="portal_online_no_logout") {
        queueFailedProbes(); replies.emplace_back(ONLINE); ready(); runAfter(10000);
        for(int i=0;i<500;++i) { queueFailedProbes(); replies.emplace_back(ONLINE); runAfter(10000); }
        assert(controller.state()==buct::State::CHECKING);
        for(const auto& req:requests) assert(req.find("action=login")==std::string::npos);
    } else if(name=="daily_cycles_120_days") {
        uint64_t elapsedTotal=0;
        for(int day=0;day<120;++day) {
            WiFi.statusValue=WL_DISCONNECTED; WiFi.ip.value=0; WiFi.gateway.value=0; testAssociated=false;
            loop(); runAfter(30000); runAfter(250);
            testAssociated=true; loop(); runAfter(65000);
            queueOnline(); ready(); runAfter(10000);
            assert(controller.state()==buct::State::ONLINE);
            // One long online interval per day; unsigned clock crosses twice.
            queueOnline(); runAfter(86000000); elapsedTotal+=86105250ULL;
            assert(controller.state()==buct::State::ONLINE);
        }
        assert(elapsedTotal>2ULL*UINT32_MAX && WiFi.starts==121);
    } else { assert(false); }
    assertNoLogout(); assert(replies.empty());
    assert(!WiFi.radioOffRequested && !WiFi.eraseRequested);
    printf("PASS integration %s\n",name.c_str());
}
