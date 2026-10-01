#pragma once
// Srun portal protocol layer for tree.buct.edu.cn
// Endpoints verified against the live portal (V1.18 B20210610).
#include <stdint.h>

namespace srun {

enum class AuthResult {
    OK,               // logged in / online
    ALREADY_ONLINE,
    BAD_CREDENTIALS,  // E2901 - do not retry
    PORTAL_UNREACHABLE,
    PROTOCOL_ERROR,   // unexpected response
};

struct OnlineInfo {
    bool online;
    char userName[32];
    char onlineIp[24];
    uint64_t bytesIn, bytesOut;   // this session
    double remainBytes;           // package remain (0 = unlimited)
};

// Query current online status via rad_user_info.
// Fills info; ok=false when the portal can't be reached at all.
bool getOnlineInfo(OnlineInfo &info);

// Perform full login: get_challenge -> srun_portal login.
AuthResult login(const char *username, const char *password, const char *acId);

// Logout current IP session (used only for explicitly enabled account replacement).
// False means status could not be determined or logout was not acknowledged.
bool logout();

// Probe connectivity through the public-internet detection endpoints.
// Returns true only when an endpoint answers HTTP 204 (portal hijack
// answers 200+HTML, so 2xx in general is NOT proof of connectivity).
bool probeOnline(uint32_t timeoutMs);

} // namespace srun
