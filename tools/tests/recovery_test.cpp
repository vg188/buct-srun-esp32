#include "auth_controller.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
using namespace buct;

struct Fake : Port {
    uint32_t clock = 0, duration = 0;
    Link network = Link::DOWN;
    bool internet = false, infoOK = true, online = false, logoutOK = true;
    bool disconnectDuringProbe = false, disconnectDuringInfo = false;
    srun::AuthResult result = srun::AuthResult::PORTAL_UNREACHABLE;
    const char* user = "test-user";
    int starts = 0, disconnects = 0, probes = 0, infos = 0, logins = 0, logouts = 0;
    std::vector<std::string> logs;
    uint32_t now() const override { return clock; }
    Link link() const override { return network; }
    void beginWiFi() override { ++starts; }
    void disconnectWiFi() override { ++disconnects; network = Link::DOWN; }
    bool probe(uint32_t) override {
        ++probes; clock += duration;
        if (disconnectDuringProbe) network = Link::DOWN;
        return internet;
    }
    bool onlineInfo(srun::OnlineInfo& i) override {
        ++infos; clock += duration; i = {}; i.online = online;
        strcpy(i.userName, user);
        if (disconnectDuringInfo) network = Link::DOWN;
        return infoOK;
    }
    srun::AuthResult login() override { ++logins; clock += duration; return result; }
    bool logout() override { ++logouts; clock += duration; return logoutOK; }
    void log(const char* s) override { logs.emplace_back(s); }
};
static const uint32_t BACKOFF[] = {5, 10, 30, 60};
Settings defaults() {
    return {30000, 90000, 10000, 30000, 8000, 1800000, false, 1800000, BACKOFF, 4, "test-user"};
}
struct Test {
    Fake p;
    Controller c;
    explicit Test(Settings s = defaults()) : c(p, s) { c.begin(); }
    void advance(uint32_t ms) { p.clock += ms; c.tick(); }
    void ready() { p.network = Link::READY; c.tick(); }
    void check() { ready(); advance(10000); }
};

int main(int argc, char** argv) {
    assert(argc == 2);
    std::string name = argv[1];
    if (name == "late_router") {
        Test t;
        for (int i=0; i<12; ++i) {
            t.advance(30000); assert(t.p.starts == i+1);
            t.advance(249); assert(t.p.starts == i+1);
            t.advance(1); assert(t.p.starts == i+2);
        }
        assert(t.p.logins == 0);
        t.check(); assert(t.p.logins == 1);
    } else if (name == "dhcp_window") {
        Test t;
        t.advance(29000); t.p.network=Link::WAIT_ADDRESS; t.c.tick();
        t.advance(89999); assert(t.p.disconnects==0 && t.p.probes==0);
        t.advance(1); assert(t.p.disconnects==1);
        t.advance(250); assert(t.p.starts==2);
    } else if (name == "dhcp_success") {
        Test t;
        t.p.network=Link::WAIT_ADDRESS; t.c.tick(); t.advance(85000);
        t.ready(); t.advance(9999); assert(t.p.probes==0);
        t.advance(1); assert(t.p.logins==1 && t.p.disconnects==0);
    } else if (name == "ap_lost_during_dhcp") {
        Test t; t.p.network=Link::WAIT_ADDRESS; t.c.tick(); t.advance(85000);
        t.p.network=Link::DOWN; t.c.tick(); t.advance(29999);
        assert(t.p.disconnects==0); t.advance(1); assert(t.p.disconnects==1);
    } else if (name == "settle_disconnect") {
        Test t; t.ready(); t.advance(5000); t.p.network=Link::DOWN; t.c.tick();
        t.advance(20000); assert(t.p.probes==0);
        t.ready(); t.advance(9999); assert(t.p.probes==0);
        t.advance(1); assert(t.p.probes==1);
    } else if (name == "backoff") {
        Test t; t.check();
        for (uint32_t ms : {5000,10000,30000,60000,60000}) {
            int n=t.p.logins; t.advance(ms-1); assert(t.p.logins==n);
            t.advance(1); assert(t.p.logins==n+1);
        }
    } else if (name == "wan_recovers") {
        Test t; t.check(); t.advance(5000);
        t.p.result=srun::AuthResult::OK; t.advance(10000);
        assert(t.c.state()==State::VERIFY_LOGIN);
        int n=t.p.probes; t.advance(1499); assert(t.p.probes==n);
        t.p.internet=true; t.advance(1); assert(t.c.state()==State::ONLINE);
    } else if (name == "already_online") {
        Test t; t.p.result=srun::AuthResult::ALREADY_ONLINE; t.check();
        assert(t.c.state()==State::VERIFY_LOGIN);
        t.p.internet=true; t.advance(1500); assert(t.c.state()==State::ONLINE);
    } else if (name == "login_probe_fails") {
        Test t; t.p.result=srun::AuthResult::OK; t.check(); t.advance(1500);
        assert(t.c.state()==State::CHECKING); t.advance(4999); assert(t.p.logins==1);
        t.advance(1); assert(t.p.logins==2);
    } else if (name == "online_reconnect") {
        Test t; t.p.internet=true; t.check(); assert(t.c.state()==State::ONLINE);
        t.p.network=Link::DOWN; t.c.tick(); t.advance(30000); t.advance(250);
        t.check(); assert(t.c.state()==State::ONLINE && t.p.starts==2);
    } else if (name == "fatal") {
        Test t; t.p.result=srun::AuthResult::BAD_CREDENTIALS; t.check();
        assert(t.c.state()==State::FATAL_CREDENTIALS);
        t.advance(3600000); t.p.network=Link::DOWN; t.c.tick();
        t.ready(); t.advance(3600000); assert(t.p.logins==1 && t.p.logouts==0);
    } else if (name == "unknown_status") {
        Test t; t.p.infoOK=false; t.check(); assert(t.p.logins==1 && t.p.logouts==0);
    } else if (name == "online_probe_failure_safe") {
        Test t; t.p.online=true; t.check();
        for(int i=0;i<1000;++i) t.advance(10000);
        assert(t.p.logouts==0 && t.p.logins==0);
    } else if (name == "mismatch_default_safe") {
        Test t; t.p.internet=true; t.p.online=true; t.p.user="other-user";
        t.check(); for(int i=0;i<150;++i) t.advance(30000);
        assert(t.p.logouts==0 && t.p.logins==0 && t.c.state()==State::ONLINE);
    } else if (name == "verify_disabled") {
        auto cfg=defaults(); cfg.portalVerifyMs=0; Test t(cfg);
        t.p.internet=true; t.check();
        for(int i=0;i<100;++i) t.advance(30000);
        assert(t.p.infos==1);
    } else if (name == "verify_cadence") {
        Test t; t.p.internet=true; t.check();
        for(int i=0;i<59;++i) t.advance(30000);
        assert(t.p.infos==1); t.advance(30000); assert(t.p.infos==2);
    } else if (name == "replacement_cooldown") {
        auto cfg=defaults(); cfg.replaceAccount=true; cfg.portalVerifyMs=30000;
        Test t(cfg); t.p.internet=true; t.p.online=true; t.p.user="other-user";
        t.p.logoutOK=false; t.check(); assert(t.p.logouts==1 && t.p.logins==0);
        for(int i=0;i<59;++i) t.advance(30000);
        assert(t.p.logouts==1); t.advance(30000); assert(t.p.logouts==2);
    } else if (name == "replacement_reconnect_cooldown") {
        auto cfg=defaults(); cfg.replaceAccount=true;
        Test t(cfg); t.p.internet=true; t.p.online=true; t.p.user="other-user";
        t.p.logoutOK=false; t.check(); assert(t.p.logouts==1);
        t.p.network=Link::DOWN; t.c.tick(); t.check(); assert(t.p.logouts==1);
    } else if (name == "replacement_fatal") {
        auto cfg=defaults(); cfg.replaceAccount=true;
        Test t(cfg); t.p.internet=true; t.p.online=true; t.p.user="other-user";
        t.p.result=srun::AuthResult::BAD_CREDENTIALS; t.check();
        assert(t.p.logouts==1 && t.p.logins==1 && t.c.state()==State::FATAL_CREDENTIALS);
        t.advance(3600000); assert(t.p.logins==1);
    } else if (name == "drop_during_probe" || name == "drop_during_info") {
        Test t;
        t.p.disconnectDuringProbe=name=="drop_during_probe";
        t.p.disconnectDuringInfo=name=="drop_during_info";
        t.check(); assert(t.p.logins==0); t.c.tick();
        assert(t.c.state()==State::WIFI_CONNECTING);
    } else if (name == "blocking_call_timing") {
        Test t; t.p.duration=12000; t.check(); assert(t.p.logins==1);
        t.advance(4999); assert(t.p.logins==1);
        t.advance(1); assert(t.p.logins==2);
    } else if (name == "wrap_wifi") {
        Test t; t.p.clock=UINT32_MAX-1000; t.c.begin();
        t.advance(30000); assert(t.p.disconnects==1);
        t.advance(250); assert(t.p.starts==3);
    } else if (name == "wrap_schedule") {
        Test t; t.p.clock=UINT32_MAX-5000; t.ready();
        t.advance(9999); assert(t.p.probes==0); t.advance(1); assert(t.p.logins==1);
    } else if (name == "wrap_cooldown") {
        auto cfg=defaults(); cfg.replaceAccount=true; cfg.portalVerifyMs=30000;
        Test t(cfg); t.p.clock=UINT32_MAX-20000;
        t.p.internet=true; t.p.online=true; t.p.user="other-user"; t.p.logoutOK=false;
        t.check(); assert(t.p.logouts==1);
        t.advance(30000); assert(t.p.logouts==1);
        t.advance(1800000); assert(t.p.logouts==2);
    } else { assert(false); }
    printf("PASS %s\n",name.c_str());
}
