#include "auth_controller.h"
#include <string.h>

namespace buct {
void Controller::begin() {
    state_ = State::WIFI_CONNECTING;
    previousLink_ = Link::DOWN;
    restartPending_ = false;
    backoffIndex_ = 0;
    portalChecked_ = false;
    replacementAttempted_ = false;
    port_.beginWiFi();
    wifiSince_ = port_.now();
    port_.log("WiFi initial connection started");
}

void Controller::schedule(uint32_t waitMs) {
    scheduledAt_ = port_.now();
    waitMs_ = waitMs;
}

void Controller::retry() {
    // Defensive fallback; firmware Config.h is also checked at compile time.
    uint32_t seconds = cfg_.backoffCount ? cfg_.backoffSeconds[backoffIndex_] : 60;
    if (backoffIndex_ + 1 < cfg_.backoffCount) ++backoffIndex_;
    schedule(seconds * 1000UL);
}

void Controller::tick() {
    // A fatal credential error survives WiFi loss and does not submit more requests.
    if (state_ == State::FATAL_CREDENTIALS) return;

    uint32_t now = port_.now();
    Link link = port_.link();
    if (link != Link::READY) {
        if (state_ != State::WIFI_CONNECTING) {
            state_ = State::WIFI_CONNECTING;
            wifiSince_ = now;
            portalChecked_ = false;
            port_.log("WiFi/IP lost -> reconnect recovery");
        }
        if (restartPending_) {
            // Separate disconnect and begin: don't reconfigure during disconnect.
            if (elapsed(now, wifiSince_, 250)) {
                port_.beginWiFi();
                wifiSince_ = port_.now();
                previousLink_ = Link::DOWN;
                restartPending_ = false;
                port_.log("WiFi new connection attempt");
            }
            return;
        }
        if (link == Link::WAIT_ADDRESS && previousLink_ != Link::WAIT_ADDRESS) {
            addressSince_ = now;
            port_.log("WiFi associated -> waiting for DHCP/IP");
        }
        if (link == Link::DOWN && previousLink_ == Link::WAIT_ADDRESS)
            wifiSince_ = now;
        bool timedOut = link == Link::WAIT_ADDRESS
            ? elapsed(now, addressSince_, cfg_.dhcpTimeoutMs)
            : elapsed(now, wifiSince_, cfg_.wifiRetryMs);
        previousLink_ = link;
        if (timedOut) {
            port_.log(link == Link::WAIT_ADDRESS ? "DHCP timeout -> retry" : "WiFi association timeout -> retry");
            port_.disconnectWiFi();
            wifiSince_ = port_.now();
            restartPending_ = true;
        }
        return;
    }

    previousLink_ = Link::READY;
    restartPending_ = false;
    if (state_ == State::WIFI_CONNECTING) {
        state_ = State::ROUTER_SETTLING;
        backoffIndex_ = 0;
        portalChecked_ = false;
        schedule(cfg_.settleMs);
        port_.log("WiFi/IP ready -> waiting briefly for router");
        return;
    }
    if (!elapsed(now, scheduledAt_, waitMs_)) return;
    if (state_ == State::VERIFY_LOGIN) {
        // Release loop() during the original 1500ms session-settle interval.
        if (port_.probe(cfg_.probeTimeoutMs)) markOnline();
        else {
            port_.log("login accepted but probe failed -> retry");
            state_ = State::CHECKING;
            retry();
        }
        return;
    }
    check();
}

void Controller::markOnline() {
    if (state_ != State::ONLINE) port_.log("probe 204 -> ONLINE");
    state_ = State::ONLINE;
    backoffIndex_ = 0;
    schedule(cfg_.probeIntervalMs);
}

void Controller::check() {
    if (port_.probe(cfg_.probeTimeoutMs)) {
        markOnline();
        // Independent cadence: a failed replacement must not force a new check
        // on every probe via a 'newlyOnline' state transition.
        if (!portalChecked_ || (cfg_.portalVerifyMs &&
            elapsed(port_.now(), lastPortalCheck_, cfg_.portalVerifyMs))) {
            verifySession();
        }
        return;
    }
    state_ = State::CHECKING;
    if (port_.link() != Link::READY) return; // recover link on the next tick
    srun::OnlineInfo info{};
    bool known = port_.onlineInfo(info);
    if (known && info.online) {
        port_.log("portal says online, probes failed -> wait; no automatic logout");
        schedule(10000); // Proven release behavior; no speculative session repair.
        return;
    }
    port_.log(known ? "portal says offline -> login" : "portal status unknown -> try release login path");
    // Preserve release fallback: status endpoint failure does not imply that
    // challenge/login endpoints are unusable. No logout is allowed here.
    login();
}

void Controller::login() {
    if (port_.link() != Link::READY) return;
    state_ = State::CHECKING;
    switch (port_.login()) {
    case srun::AuthResult::OK:
    case srun::AuthResult::ALREADY_ONLINE:
        state_ = State::VERIFY_LOGIN;
        schedule(1500);
        port_.log("login accepted -> settling before probe");
        break;
    case srun::AuthResult::BAD_CREDENTIALS:
        state_ = State::FATAL_CREDENTIALS;
        port_.log("E2901: credentials rejected; authentication STOPPED until restart");
        break;
    case srun::AuthResult::PORTAL_UNREACHABLE:
        port_.log("portal unreachable -> retry with backoff");
        retry();
        break;
    case srun::AuthResult::PROTOCOL_ERROR:
        port_.log("unexpected portal response -> retry with backoff");
        retry();
        break;
    }
}

void Controller::verifySession() {
    portalChecked_ = true;
    srun::OnlineInfo info{};
    bool known = port_.onlineInfo(info);
    lastPortalCheck_ = port_.now();
    if (!known || !info.online) {
        port_.log(known ? "probe online; portal says offline -> preserve connectivity"
                        : "probe online; portal status unknown -> preserve connectivity");
        schedule(cfg_.probeIntervalMs);
        return;
    }
    if (strcmp(info.userName, cfg_.username) == 0) {
        port_.log("portal account verified");
        schedule(cfg_.probeIntervalMs);
        return;
    }
    if (!cfg_.replaceAccount) {
        port_.log("portal account mismatch -> warning only (replacement disabled)");
        schedule(cfg_.probeIntervalMs);
        return;
    }
    if (replacementAttempted_ && !elapsed(port_.now(), lastReplacement_, cfg_.replacementCooldownMs)) {
        port_.log("account replacement cooling down -> preserve connectivity");
        schedule(cfg_.probeIntervalMs);
        return;
    }
    if (port_.link() != Link::READY) return;
    replacementAttempted_ = true;
    lastReplacement_ = port_.now(); // Includes failed logout, survives link changes.
    port_.log("account replacement explicitly enabled -> logout attempt");
    if (!port_.logout()) {
        port_.log("logout not confirmed -> no replacement login, cooling down");
        schedule(cfg_.probeIntervalMs);
        return;
    }
    login();
}
} // namespace buct
