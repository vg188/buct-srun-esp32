#include "srun_auth.h"
#include "srun_crypto.h"
#include "Config.h"
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <time.h>

// Defined in BUCT_AutoAuth.ino (global scope — must NOT be inside namespace srun)
void srunDebugLog(const char *fmt, ...);


namespace srun {

static const char *PORTAL_HOST = SRUN_PORTAL_HOST;
// Fallback portal IP in case DNS is broken while logged out.
static const char *PORTAL_IP = "202.4.130.95";
static const char *UA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";

// Connectivity-detection endpoints (return 204 on open internet).
static const char *PROBE_HOSTS[] = {
    "connect.rom.miui.com",
    "wifi.vivo.com.cn",
    "www.qualcomm.cn",
};
static const char *PROBE_PATHS[] = {
    "/generate_204",
    "/generate_204",
    "/generate_204",
};

// --- JSONP helpers ----------------------------------------------------

// "jQuery1124081234567890123_1757740000000"
static void makeCallback(char *buf, size_t cap) {
    unsigned long ms = (unsigned long)(millis() % 1000000000UL);
    snprintf(buf, cap, "jQuery11240%09lu_%llu",
             ms, (unsigned long long)time(nullptr) * 1000ULL + 1);
}

// Strip "callback(" ... ")" wrapper; returns false on shape mismatch.
static bool unwrapJsonp(const String &body, const char *callback, String &out) {
    if (!body.startsWith(callback)) return false;
    int start = strlen(callback);
    if (body.length() < (size_t)start + 2) return false;
    if (body[start] != '(' || !body.endsWith(")")) return false;
    out = body.substring(start + 1, body.length() - 1);
    return true;
}

// Extract string value for a JSON key from a flat JSON object.
// Returns false if the key is absent. Empty string is a valid value.
static bool jsonStr(const String &json, const char *key, String &out) {
    String pat = "\"" + String(key) + "\":";
    int k = json.indexOf(pat);
    if (k < 0) return false;
    k += pat.length();
    if (k < (int)json.length() && json[k] == '"') {
        int e = json.indexOf('"', k + 1);
        if (e < 0) return false;
        out = json.substring(k + 1, e);
        return true;
    }
    int e = k;
    while (e < (int)json.length() && json[e] != ',' && json[e] != '}') e++;
    out = json.substring(k, e);
    return true;
}

// Percent-encode everything outside the unreserved set (the portal's own
// JS uses encodeURIComponent for these query values).
static String urlEncode(const char *s) {
    static const char *hex = "0123456789ABCDEF";
    String out;
    for (const char *p = s; *p; p++) {
        uint8_t c = (uint8_t)*p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

// Raw HTTP/1.0 GET via WiFiClient — replaces HTTPClient, whose connect
// timeout can hang forever when the campus gateway silently drops SYNs to
// the DNS-hijacked probe IPs (observed on ESP32-C3 + arduino-esp32 3.3.11).
// All timeouts are enforced by WiFiClient.setTimeout on every read.
static bool httpGet(const char *host, const char *pathQ,
                    uint32_t timeoutMs, String &body, int *code) {
    srunDebugLog("HTTP GET -> %s%s", host, pathQ);
    body = "";  // callers reuse the same String across requests; stale data
                // here corrupts the next response (observed: challenge body
                // prepended to login body -> JSONP unwrap always failed)

    WiFiClient client;
    client.setTimeout(timeoutMs);
    uint32_t t0 = millis();
    // Explicit connect timeout (ms): the 2-arg overload may not honor
    // setTimeout in all core versions; this one is guaranteed.
    bool connected = client.connect(host, 80, (int32_t)timeoutMs);

    uint32_t tConn = millis() - t0;

    if (!connected) {
        srunDebugLog("TCP connect FAIL after %lums", (unsigned long)tConn);
        if (code) *code = 0;
        client.stop();
        return false;
    }

    // Minimal browser-like request
    client.printf("GET %s HTTP/1.0\r\n", pathQ);
    client.printf("Host: %s\r\n", host);
    client.printf("User-Agent: %s\r\n", UA);
    client.printf("Referer: http://%s/srun_portal_pc?ac_id=%s&theme=basic\r\n",
                  SRUN_PORTAL_HOST, SRUN_AC_ID);
    client.print("Accept: */*\r\nConnection: close\r\n\r\n");

    // Read status line
    String statusLine = client.readStringUntil('\n');
    int httpCode = 0;
    if (sscanf(statusLine.c_str(), "HTTP/%*d.%*d %d", &httpCode) != 1) {
        srunDebugLog("bad status line: %.60s", statusLine.c_str());
        client.stop();
        if (code) *code = 0;
        return false;
    }

    // Headers: capture Content-Length (probe 204 has none -> empty body)
    size_t contentLen = 0;
    while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line.length() == 0) break;
        if (line.startsWith("Content-Length:")) {
            contentLen = (size_t)strtoul(line.c_str() + 15, nullptr, 10);
        }
    }
    if (contentLen > 0 && contentLen < 4096) {
        // Read exactly contentLen bytes (hijack gateways ignore
        // Connection: close and never send EOF -> readString would block
        // until the full timeout).
        uint8_t buf[1024];
        size_t got = 0;
        uint32_t deadline = millis() + timeoutMs;
        while (got < contentLen && (int32_t)(millis() - deadline) < 0) {
            size_t n = client.read(buf, min(sizeof(buf), contentLen - got));
            if ((int)n <= 0) { delay(10); continue; }
            body.concat((const char *)buf, n);
            got += n;
        }
    } else if (contentLen == 0 && httpCode != 204 && httpCode != 304) {
        // No Content-Length but a body is possible (chunked/raw close):
        // bounded read of whatever arrives within a 300ms quiet window.
        uint32_t tRead = millis();
        while (millis() - tRead < 300) {
            int n = client.read();
            if (n < 0) { delay(5); continue; }
            body += (char)n;
            if (body.length() > 2048) break;
            tRead = millis();  // reset on data
        }
    }
    // 204/304: no body by definition — return immediately
    client.stop();

    uint32_t dt = millis() - t0;
    srunDebugLog("HTTP %d (conn %lums, total %lums) %d B", httpCode,
                 (unsigned long)tConn, (unsigned long)dt, body.length());
    if (code) *code = httpCode;
    return httpCode > 0;
}

// --- Public API -------------------------------------------------------

bool probeOnline(uint32_t timeoutMs) {
    static size_t idx = 0;  // rotate endpoints to avoid a single point of failure
    for (size_t attempt = 0; attempt < 3; attempt++) {
        const char *host = PROBE_HOSTS[idx % 3];
        const char *path = PROBE_PATHS[idx % 3];
        idx++;
        String body;
        int code = 0;
        if (httpGet(host, path, timeoutMs, body, &code)) {
            // Strict: only 204 proves end-to-end connectivity; the portal
            // hijacks these URLs and answers 200 with an HTML page.
            if (code == 204) return true;
        }
    }
    return false;
}

bool getOnlineInfo(OnlineInfo &info) {
    memset(&info, 0, sizeof(info));

    char cb[48];
    makeCallback(cb, sizeof(cb));
    char path[160];
    snprintf(path, sizeof(path),
             "/cgi-bin/rad_user_info?callback=%s&_=%llu",
             cb, (unsigned long long)time(nullptr) * 1000ULL);

    String body;
    int code;
    if (!httpGet(PORTAL_HOST, path, 8000, body, &code)) {
        // DNS may be dead while logged out; retry via portal IP.
        if (!httpGet(PORTAL_IP, path, 8000, body, &code)) return false;
    }

    String json;
    if (!unwrapJsonp(body, cb, json)) return false;

    String err;
    if (jsonStr(json, "error", err) && err == "not_online_error") {
        info.online = false;
        return true;
    }
    String ip, user;
    if (jsonStr(json, "online_ip", ip) && jsonStr(json, "user_name", user)) {
        info.online = true;
        strlcpy(info.onlineIp, ip.c_str(), sizeof(info.onlineIp));
        strlcpy(info.userName, user.c_str(), sizeof(info.userName));
        String n;
        if (jsonStr(json, "bytes_in", n))  info.bytesIn  = strtoull(n.c_str(), nullptr, 10);
        if (jsonStr(json, "bytes_out", n)) info.bytesOut = strtoull(n.c_str(), nullptr, 10);
        if (jsonStr(json, "remain_bytes", n)) info.remainBytes = n.toDouble();
        return true;
    }
    info.online = false;
    return true;  // reachable portal, just not online
}

bool logout() {
    OnlineInfo info;
    if (!getOnlineInfo(info) || !info.online) return true;

    char cb[48];
    makeCallback(cb, sizeof(cb));
    char path[160];
    snprintf(path, sizeof(path),
             "/cgi-bin/srun_portal?callback=%s&action=logout&ac_id=%s&ip=%s&_=%llu",
             cb, SRUN_AC_ID, info.onlineIp,
             (unsigned long long)time(nullptr) * 1000ULL);

    String body;
    int code;
    if (!httpGet(PORTAL_HOST, path, 8000, body, &code)) return false;
    String json;
    if (!unwrapJsonp(body, cb, json)) return false;
    String err;
    return jsonStr(json, "error", err) && err == "ok";
}

AuthResult login(const char *username, const char *password, const char *acId) {
    // 1. challenge
    char cb[48];
    makeCallback(cb, sizeof(cb));
    char path[192];
    snprintf(path, sizeof(path),
             "/cgi-bin/get_challenge?callback=%s&username=%s&ip=&_=%llu",
             cb, username, (unsigned long long)time(nullptr) * 1000ULL);

    String body;
    int code;
    if (!httpGet(PORTAL_HOST, path, 8000, body, &code)) {
        if (!httpGet(PORTAL_IP, path, 8000, body, &code))
            return AuthResult::PORTAL_UNREACHABLE;
    }
    String json;
    if (!unwrapJsonp(body, cb, json)) return AuthResult::PROTOCOL_ERROR;

    String challenge, err, clientIp;
    if (!jsonStr(json, "challenge", challenge) || challenge.length() != 64)
        return AuthResult::PROTOCOL_ERROR;
    // Portal-side view of our IP; must be used in info/chksum/login params.
    if (!jsonStr(json, "client_ip", clientIp)) clientIp = "";

    // 2. crypto pieces
    char hmd5[33];
    srun::hmacMd5Hex(challenge.c_str(), challenge.length(),
                     password, strlen(password), hmd5);
    char infoJson[256];
    snprintf(infoJson, sizeof(infoJson),
             "{\"username\":\"%s\",\"password\":\"%s\",\"ip\":\"%s\",\"acid\":\"%s\",\"enc_ver\":\"srun_bx1\"}",
             username, password, clientIp.c_str(), acId);
    // info = "{SRBX1}" + 4/3 * xEncodeLen; password can be long -> size generously
    char info[384];
    if (!srun::buildInfo(infoJson, challenge.c_str(), info, sizeof(info)))
        return AuthResult::PROTOCOL_ERROR;

    const char *n = "200", *type = "1";
    // chkstr = token+username token+hmd5 token+acid token+ip token+n token+type token+info
    String chkstr = challenge + username;
    chkstr += challenge + hmd5;
    chkstr += challenge + acId;
    chkstr += challenge + clientIp;  // must match the ip sent in login query
    chkstr += challenge + n;
    chkstr += challenge + type;
    chkstr += challenge + info;
    char chksum[41];
    srun::sha1Hex(chkstr.c_str(), chkstr.length(), chksum);

    // 3. login request — values URL-encoded (info contains + / and {SRBX1}
    // braces; password carries literal {MD5} prefix)
    makeCallback(cb, sizeof(cb));
    String query = String("/cgi-bin/srun_portal?"
        "callback=") + cb +
        "&action=login&username=" + username +
        "&password=" + urlEncode("{MD5}") + hmd5 +
        "&ac_id=" + acId +
        "&ip=" + clientIp +
        "&chksum=" + chksum +
        "&info=" + urlEncode(info) +
        "&n=200&type=1&os=Windows&name=Windows&double_stack=0&_=" +
        String((unsigned long long)time(nullptr) * 1000ULL);

    if (!httpGet(PORTAL_HOST, query.c_str(), 10000, body, &code)) {
        if (!httpGet(PORTAL_IP, query.c_str(), 10000, body, &code))
            return AuthResult::PORTAL_UNREACHABLE;
    }
    if (!unwrapJsonp(body, cb, json)) {
        srunDebugLog("login: bad JSONP wrap: %.80s", body.c_str());
        return AuthResult::PROTOCOL_ERROR;
    }

    // Log the raw response once — field layout differs across srun builds
    srunDebugLog("login resp: %.160s", json.c_str());

    if (jsonStr(json, "ecode", err) && err == "E2901")
        return AuthResult::BAD_CREDENTIALS;

    String res;
    if (jsonStr(json, "res", res) && res == "ok")
        return AuthResult::OK;
    if (jsonStr(json, "error", err) && err == "ip_already_online_error")
        return AuthResult::ALREADY_ONLINE;
    // "error":"ok" is also a success spelling on some builds
    if (jsonStr(json, "error", err) && err == "ok")
        return AuthResult::OK;

    return AuthResult::PROTOCOL_ERROR;
}

} // namespace srun
