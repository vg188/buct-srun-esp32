// ============================================================
// BUCT 校园网自动认证 (tree.buct.edu.cn, 深澜 Srun)
// for ESP32-C3 (Super Mini 等), Arduino IDE
//
// 用法:
//   1. 改下面的 WIFI_SSID / WIFI_PASSWORD / CAMPUS_USERNAME /
//      CAMPUS_PASSWORD
//   2. Arduino IDE: 板子选 "ESP32C3 Dev Module", 上传, 打开串口监视器
//      (115200) 看日志
//
// 原理: ESP32 连上小米路由器 WiFi(NAT 模式), 校园网侧只看到路由器
// 一个 IP -> ESP32 完成深澜认证 = 路由器下全部设备上网。
//
// LED: 常亮=在线 / 慢闪=连WiFi中 / 快闪=认证中 / 极快闪=账号密码错误
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <stdarg.h>
#include "Config.h"
#include "srun_auth.h"
#ifndef LED_BUILTIN
#define LED_BUILTIN 8  // 板载 WS2812 RGB LED 数据脚 (GPIO8, 商家例程确认)
#endif
enum class State { WIFI_CONNECTING, ONLINE, AUTHENTICATING, FATAL_CREDENTIALS };

static State state = State::WIFI_CONNECTING;
static uint32_t backoffIdx = 0;
static const uint32_t BACKOFF[] = AUTH_RETRY_BACKOFF_S;
static const size_t BACKOFF_N = sizeof(BACKOFF) / sizeof(BACKOFF[0]);
// 板载灯实测为普通单色蓝 LED (商家例程的 WS2812 是驱动外部灯带的)。
// 全部用"闪烁模式"表达状态 — 闪烁与 LED 极性无关, 接反也能识别。
static void setLedRaw(bool on) { digitalWrite(LED_BUILTIN, on); }
static void ledInit() {
    pinMode(LED_BUILTIN, OUTPUT);
    setLedRaw(false);
}
// pattern: on_ms, off_ms 循环; 传 (0,0) 表示常亮(未知极性时可能是常灭, 以串口为准)
static void ledTick() {
    static uint32_t last = 0;
    if (millis() - last < 25) return;
    last = millis();
    uint32_t onMs, offMs;
    switch (state) {
    case State::ONLINE:           onMs = 60;  offMs = 2940; break;  // 心跳闪: 3s 一闪
    case State::WIFI_CONNECTING:  onMs = 500; offMs = 500;  break;  // 1Hz
    case State::AUTHENTICATING:   onMs = 125; offMs = 125;  break;  // 4Hz
    default:                      onMs = 60;  offMs = 60;   break;  // 8Hz (密码错)
    }
    setLedRaw((millis() % (onMs + offMs)) < onMs);
}


// 本板 USB 实测为 ESP32-C3 片内 USB-Serial-JTAG (VID:PID=303A:1001)。
// 日志要走片内 USB, 必须在 Arduino IDE 菜单开启:
//   工具 -> USB CDC On Boot -> Enabled
// 开启后 (任一 USB 模式下) Serial 即 USB 口, 直接用 Serial。
// (若保持 Disabled, Serial 会映射到 GPIO20/21 的 UART0, USB 口无输出)
#define LOG_PORT Serial




static void logState(const char *msg) {
    char ts[16];
    snprintf(ts, sizeof(ts), "%lus", (unsigned long)(millis() / 1000));
    LOG_PORT.printf("[%8s] %s\n", ts, msg);
    LOG_PORT.flush();
}

// 协议层调试日志入口 (srun_auth.cpp 通过 extern 声明调用)
void srunDebugLog(const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    logState(buf);
}

// One full "check & heal" pass. Returns next wait (seconds).
static uint32_t runCheck() {
    // 1. 探测公网 (generate_204, 只认 204; 门户劫持时返回 200+HTML)
    if (srun::probeOnline(PROBE_TIMEOUT_MS)) {
        bool newlyOnline = (state != State::ONLINE);
        state = State::ONLINE;
        backoffIdx = 0;

        // 兜底: 探测通过只说明 HTTP 能出去, 不说明是我们的会话。
        // 每半小时强制问一次门户, 在线账号不对则登出重登。
        static uint32_t lastPortalVerifyMs = 0;
        bool verifyDue = (millis() - lastPortalVerifyMs >=
                          (uint32_t)PORTAL_VERIFY_INTERVAL_S * 1000UL);
        if (newlyOnline || verifyDue) {
            lastPortalVerifyMs = millis();
            srun::OnlineInfo info;
            if (srun::getOnlineInfo(info) && info.online) {
                char buf[112];
                snprintf(buf, sizeof(buf), "heartbeat: online as %s, in=%.1fMB out=%.1fMB",
                         info.userName,
                         info.bytesIn / 1048576.0, info.bytesOut / 1048576.0);
                logState(buf);
                if (strcmp(info.userName, CAMPUS_USERNAME) != 0) {
                    logState("online account mismatch -> logout & re-login");
                    srun::logout();
                    delay(1000);
                    srun::AuthResult r = srun::login(CAMPUS_USERNAME,
                                                     CAMPUS_PASSWORD, SRUN_AC_ID);
                    logState(r == srun::AuthResult::OK ? "re-login OK"
                                                       : "re-login FAILED");
                }
            } else {
                logState("heartbeat: probe 204 but portal says we're offline "
                         "(another NAT session keeps the network alive)");
            }
        } else {
            // 静默心跳: 间隔跟随门户核对周期(测试时可一起缩短)
            static uint32_t lastBeatMs = 0;
            if (millis() - lastBeatMs >=
                (uint32_t)PORTAL_VERIFY_INTERVAL_S * 500UL) {
                lastBeatMs = millis();
                logState("heartbeat: probe 204 (silent)");
            }
        }
        return PROBE_INTERVAL_S;
    }

    logState("probe failed -> checking portal");
    state = State::AUTHENTICATING;

    // 2. 查门户: 当前 IP 是否已有会话
    srun::OnlineInfo info;
    if (srun::getOnlineInfo(info) && info.online) {
        char buf[96];
        snprintf(buf, sizeof(buf), "portal says online as %s (%s)",
                 info.userName, info.onlineIp);
        logState(buf);
        // 门户说在线但探测失败: 可能刚上线, 等 10s 再看
        return 10;
    }

    // 3. 未认证 -> 登录
    logState("not authenticated -> logging in");
    srun::AuthResult r = srun::login(CAMPUS_USERNAME, CAMPUS_PASSWORD, SRUN_AC_ID);

    switch (r) {
    case srun::AuthResult::OK:
    case srun::AuthResult::ALREADY_ONLINE: {
        delay(1500);  // let the portal session settle
        if (srun::probeOnline(PROBE_TIMEOUT_MS)) {
            logState("login OK, probe 204 -> ONLINE");
            backoffIdx = 0;
            state = State::ONLINE;
            return PROBE_INTERVAL_S;
        }
        logState("login OK but probe still fails; will retry");
        break;
    }
    case srun::AuthResult::BAD_CREDENTIALS:
        state = State::FATAL_CREDENTIALS;
        logState("ERROR E2901: wrong username/password, STOPPED. "
                 "Fix Config.h and reflash.");
        return 60;
    case srun::AuthResult::PORTAL_UNREACHABLE:
        logState("portal unreachable (WiFi up?), retrying");
        break;
    case srun::AuthResult::PROTOCOL_ERROR:
        logState("unexpected portal response, retrying");
        break;
    }

    uint32_t wait = BACKOFF[backoffIdx < BACKOFF_N ? backoffIdx : BACKOFF_N - 1];
    if (backoffIdx < BACKOFF_N - 1) backoffIdx++;
    logState(("retry in " + String(wait) + "s").c_str());
    return wait;
}

void setup() {
    LOG_PORT.begin(115200);
    ledInit();

    // 原生 USB CDC: 芯片重启后, 主机(串口监视器)若未打开端口, 日志会丢。
    // 循环重打横幅最多 15s, 打开串口监视器即可看到启动信息。
    for (int i = 0; i < 15 && !LOG_PORT; i++) {
        delay(1000);
        LOG_PORT.begin(115200);
    }
    delay(200);
    LOG_PORT.println("\n== BUCT srun auto-auth agent ==");

    WiFi.mode(WIFI_STA);
    // Modem sleep: 无流量时 WiFi 射频休眠, 降功耗降发热。
    // 每 30s 的探测会自动唤醒射频; AP 的组播/管理帧按 DTIM 间隔唤醒接收。
    // (之前 setSleep(false) 导致射频常开 -> 芯片发烫)
    WiFi.setSleep(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop() {
    static uint32_t nextCheckAtMs = 0;

    ledTick();  // throttled LED refresh (RMT-safe)

    if (state == State::WIFI_CONNECTING && WiFi.status() == WL_CONNECTED) {
        char buf[80];
        snprintf(buf, sizeof(buf), "WiFi connected, ip=%s",
                 WiFi.localIP().toString().c_str());
        logState(buf);
        nextCheckAtMs = 0;  // check immediately
        state = State::AUTHENTICATING;
    }

    if (WiFi.status() == WL_CONNECTED &&
        (int32_t)(millis() - nextCheckAtMs) >= 0) {
        // 诊断: 分层验证网络 (首次或每轮都先跑, 直到问题定位)
        static bool diagDone = false;
        if (!diagDone) {
            diagDone = true;
            logState("DIAG: gateway ping via TCP connect...");
            WiFiClient gc;
            IPAddress gw = WiFi.gatewayIP();
            uint32_t t0 = millis();
            bool gwOk = gc.connect(gw, 80);
            logState(gwOk ? "DIAG: gateway TCP OK" : "DIAG: gateway TCP FAIL");
            if (gwOk) { gc.stop(); }
            srunDebugLog("DIAG: gateway=%s dns=%s gwTcp=%d",
                         gw.toString().c_str(),
                         WiFi.dnsIP().toString().c_str(), (int)gwOk);
            logState("DIAG: resolving connect.rom.miui.com ...");
            IPAddress resolved;
            t0 = millis();
            bool dnsOk = WiFi.hostByName("connect.rom.miui.com", resolved);
            srunDebugLog("DIAG: DNS %s -> %s (%lums)",
                         dnsOk ? "OK" : "FAIL",
                         dnsOk ? resolved.toString().c_str() : "-",
                         millis() - t0);
            logState("DIAG: calling probeOnline ...");
        }
        uint32_t waitS = runCheck();
        nextCheckAtMs = millis() + waitS * 1000UL;
    } else if (WiFi.status() != WL_CONNECTED &&
               state != State::WIFI_CONNECTING) {
        logState("WiFi lost");
        state = State::WIFI_CONNECTING;
    }
}
