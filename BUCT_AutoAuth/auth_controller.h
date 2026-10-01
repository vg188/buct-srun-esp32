#pragma once
#include <stddef.h>
#include <stdint.h>
#include "srun_auth.h"

namespace buct {

enum class Link { DOWN, WAIT_ADDRESS, READY };
enum class State { WIFI_CONNECTING, ROUTER_SETTLING, CHECKING, VERIFY_LOGIN, ONLINE, FATAL_CREDENTIALS };

// Milliseconds unless explicitly marked. Parameters are fixed for controller lifetime.
struct Settings {
    uint32_t wifiRetryMs, dhcpTimeoutMs, settleMs;
    uint32_t probeIntervalMs, probeTimeoutMs, portalVerifyMs;
    bool replaceAccount;
    uint32_t replacementCooldownMs;
    const uint32_t* backoffSeconds;
    size_t backoffCount;
    const char* username;
};

// Only the adapter touches Arduino/network APIs. Calls may block; scheduling uses
// the clock AFTER they complete. Controller state is owned solely by loop().
struct Port {
    virtual ~Port() = default;
    virtual uint32_t now() const = 0;
    virtual Link link() const = 0;
    virtual void beginWiFi() = 0;
    virtual void disconnectWiFi() = 0;
    virtual bool probe(uint32_t timeoutMs) = 0;
    virtual bool onlineInfo(srun::OnlineInfo& info) = 0;
    virtual srun::AuthResult login() = 0;
    virtual bool logout() = 0;
    virtual void log(const char* message) = 0;
};

class Controller {
public:
    Controller(Port& port, const Settings& settings) : port_(port), cfg_(settings) {}
    void begin();
    void tick();
    State state() const { return state_; }
private:
    Port& port_;
    const Settings cfg_;
    State state_ = State::WIFI_CONNECTING;
    Link previousLink_ = Link::DOWN;
    uint32_t wifiSince_ = 0, addressSince_ = 0;
    bool restartPending_ = false;
    uint32_t scheduledAt_ = 0, waitMs_ = 0;
    size_t backoffIndex_ = 0;
    bool portalChecked_ = false;
    uint32_t lastPortalCheck_ = 0;
    bool replacementAttempted_ = false;
    uint32_t lastReplacement_ = 0;

    void schedule(uint32_t waitMs);
    void retry();
    void check();
    void login();
    void markOnline();
    void verifySession();
    static bool elapsed(uint32_t now, uint32_t since, uint32_t duration) {
        return uint32_t(now - since) >= duration;
    }
};
} // namespace buct
