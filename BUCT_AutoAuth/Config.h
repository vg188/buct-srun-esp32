#pragma once
// ============ 用户配置：改成你自己的，重新上传即可 ============

// 路由器的 WiFi（仅 2.4GHz）
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// 校园网账号（纯学号，无后缀）
#define CAMPUS_USERNAME "YOUR_STUDENT_ID"
#define CAMPUS_PASSWORD "YOUR_CAMPUS_PASSWORD"

// 深澜认证参数（tree.buct.edu.cn 实测默认, 一般不用改）
#define SRUN_PORTAL_HOST "tree.buct.edu.cn"
#define SRUN_AC_ID "1"

// ============ 行为参数 ============
#define PROBE_INTERVAL_S   30     // 在线时探测周期(秒)
#define PROBE_TIMEOUT_MS   8000
#define AUTH_RETRY_BACKOFF_S {5, 10, 30, 60}  // 认证失败退避(秒), 用完循环最后一个
#define PORTAL_VERIFY_INTERVAL_S 1800  // 强制核对门户会话周期(秒), 0=关闭
                                       // (探测通过但会话不是自己时, 登出重登)
#define WIFI_RETRY_INTERVAL_S 15  // WiFi 断开重连等待(秒)
