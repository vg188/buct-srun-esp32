# BUCT Srun Auto-Auth (ESP32-C3)

北京化工大学校园网（tree.buct.edu.cn，深澜 Srun 认证）自动认证工具。ESP32-C3 开发板刷入后，挂在路由器旁即可在断电重启/掉线后自动完成认证，实现"来电即有网"。

## 原理

```
校园网 ──[深澜认证按 IP 生效]── 路由器(NAT模式) ──┬── 你的电脑/手机...
                                                   └── ESP32-C3 (本项目)
```

ESP32 以普通客户端身份连上路由器 WiFi。路由器是 NAT，校园网侧只看到路由器 WAN 口一个 IP——ESP32 替这个 IP 完成认证 = 路由器下**所有设备**一起上线。

认证流程完全模拟浏览器真实登录（深澜协议）：

1. `GET /cgi-bin/get_challenge` 获取 challenge token
2. 计算 `HMAC-MD5` 密码摘要、`XXTEA xencode + SRBX1 base64` 加密 info、`SHA-1` chksum
3. `GET /cgi-bin/srun_portal?action=login` 提交认证

在线时只轮询 `generate_204` 探测端点（不碰门户接口，避免高频请求被限流），检测到掉线才访问门户重登。

## 功能

- ⏱️ 掉线检测：轮换 3 个 `generate_204` 端点，严格只认 HTTP 204（门户劫持返回 200+HTML，必须严格判定才能识破）
- 🔁 自动重登：掉线 → 查门户状态 → 重新认证，失败按 5s→10s→30s→60s 退避
- 🛡️ 会话核对：每 30 分钟查一次门户，在线账号不是自己则登出重登（防 NAT 下会话被顶）
- 🚫 密码错误（E2901）自动停止重试，防止账号被锁
- 💡 板载 LED 状态：3s 一短闪=在线 / 1Hz=连WiFi / 4Hz=认证中 / 8Hz=密码错误
- 🌙 WiFi modem-sleep，长时间运行仅微温

## 使用

### 硬件

任意 ESP32-C3 开发板（Super Mini / PRO Mini / DevKitM-1 等）。

### 步骤

1. **Arduino IDE** 安装 esp32 board 支持包（开发板管理器搜 "esp32"）
2. 打开 `BUCT_AutoAuth/BUCT_AutoAuth.ino`
3. 编辑 `Config.h`，填入你的 WiFi 和校园网账号
4. 开发板选 **ESP32C3 Dev Module**，菜单设置：
   - **工具 → USB CDC On Boot → Enabled**（重要，否则串口无输出）
5. 上传 → 按复位键 → 串口监视器 115200 查看日志
6. 拔掉电脑，插到路由器旁任意 USB 供电口，完成

### 路由器要求

路由器必须工作在 **NAT 路由模式**（小米等家用路由器默认）。若是 AP/桥接模式，每台设备有独立校园网 IP，本工具只能认证 ESP32 自己。

## 日志示例

```
== BUCT srun auto-auth agent ==
[      2s] WiFi connected, ip=192.168.31.75
[      2s] HTTP 204 (conn 46ms, total 100ms) 0 B
[      2s] heartbeat: online as 20xxxxxx, in=0.7MB out=10.0MB
...
[     64s] probe failed -> checking portal
[     64s] not authenticated -> logging in
[     64s] HTTP GET -> tree.buct.edu.cn/cgi-bin/get_challenge?...
[     64s] HTTP GET -> tree.buct.edu.cn/cgi-bin/srun_portal?...action=login...
[     64s] login OK, probe 204 -> ONLINE
```

## 宿舍断电场景

断电 = 设备关机；来电 = 设备开机 → 自动连 WiFi → 检测 → 认证。断电本身就是定时器，无需额外定时逻辑。认证成功后持续守护（探测 + 会话核对），一天不掉线。

## 协议实现说明

深澜认证协议逆向自 tree.buct.edu.cn 门户前端 JS（`Portal.js`），加密链路（xencode/SRBX1/HMAC-MD5/SHA-1）经过与独立参考实现的字节级对拍验证。

实机调试中发现并规避的坑（详见代码注释）：

- 门户劫持 `generate_204` 时返回 **200+HTML 而非 302**，判定必须严格只认 204
- `HTTPClient` 库对劫持网关的 TCP 连接会**永久挂起**（超时失效），已改用裸 `WiFiClient` 手写 HTTP/1.0
- 劫持网关无视 `Connection: close`，必须按 `Content-Length` 精确读取，否则每个请求白等 8s 读超时
- arduino-esp32 3.x 的 `neopixelWrite`（若用 WS2812 LED）高频调用会耗尽 RMT 通道挂死系统

## 免责声明

仅供学习交流，请遵守学校网络使用规定。认证凭据明文编译进固件，请勿将已填入真实账号的代码公开分享。

## License

MIT
