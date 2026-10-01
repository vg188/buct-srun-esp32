#pragma once
// Copy Config.local.example.h to ignored Config.local.h for real credentials.
#if __has_include("Config.local.h")
#include "Config.local.h"
#endif
// ============ 用户配置：改成你自己的，重新上传即可 ============

// 路由器的 WiFi（仅 2.4GHz）
#ifndef WIFI_SSID
#define WIFI_SSID     "YOUR_WIFI_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

// 校园网账号（纯学号，无后缀）
#ifndef CAMPUS_USERNAME
#define CAMPUS_USERNAME "YOUR_STUDENT_ID"
#endif
#ifndef CAMPUS_PASSWORD
#define CAMPUS_PASSWORD "YOUR_CAMPUS_PASSWORD"
#endif

// 深澜认证参数（tree.buct.edu.cn 实测默认, 一般不用改）
#define SRUN_PORTAL_HOST "tree.buct.edu.cn"
#define SRUN_AC_ID "1"

// ============ 行为参数 ============
#define PROBE_INTERVAL_S 30
#define PROBE_TIMEOUT_MS 8000
#define AUTH_RETRY_BACKOFF_S {5, 10, 30, 60}
#define PORTAL_VERIFY_INTERVAL_S 1800  // 0=关闭周期核对，连接后仍核对一次
#define WIFI_RETRY_INTERVAL_S 30       // 未关联 AP 的尝试窗口
#define WIFI_DHCP_TIMEOUT_S 90         // 已关联 AP 后独立的取地址窗口
#define WIFI_SETTLE_S 10               // 取得 IP/网关后等待（非阻塞）
#define ACCOUNT_REPLACEMENT_ENABLED 0  // 默认只告警，不登出当前账号
#define ACCOUNT_REPLACEMENT_COOLDOWN_S 1800 // 启用换号后所有尝试（含失败）的冷却
