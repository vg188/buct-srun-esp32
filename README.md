# BUCT Srun Auto-Auth · ESP32-C3

北京化工大学校园网（`tree.buct.edu.cn`）深澜认证客户端。ESP32-C3 连接 NAT 路由器，通过原有 Srun 协议检查网络并在需要时尝试认证。

**当前版本：2026.10.01。** 本次更新侧重路由器晚启动时的持续重试、错误状态处理和可观测性，默认不主动登出已有会话。实际验证范围见下文，不承诺所有网络故障均可自动恢复。

## 工作方式

```text
校园网 ── NAT 路由器 ──┬── 电脑、手机
                       └── ESP32-C3 自动认证客户端
```

校园网侧通常看到路由器 WAN 地址，因此 ESP32 发起的认证可为同一 NAT 下设备提供上网会话。桥接/AP 模式下，每台设备可能有独立校园网地址，不适用这一共享前提。

- 未关联 AP：每 30 秒超时重试，持续等待路由器启动。
- 已关联 AP：独立等待 DHCP/IP 最多 90 秒，避免过早打断取地址过程。
- 地址与网关就绪：等待 10 秒，再检查公网和门户。
- 认证失败：按 5/10/30/60 秒退避，持续使用最后一个间隔；E2901 停止认证直到重启。
- 在线监测：每 30 秒轮换探测站，只认 HTTP 204。每轮第一个成功就结束，全部失败才检查门户。
- 门户核对：连接后及默认每 30 分钟只读核对一次。账号不一致默认只告警。
- 低功耗设置：保留 WiFi modem-sleep；不等同于整机深度睡眠，实际温度取决于板型、供电、无线环境和散热。

## 使用

### 配置凭据

保留整个 `BUCT_AutoAuth` 文件夹，包括 `auth_controller.h/.cpp`。

推荐复制 `Config.local.example.h` 为同目录的 **`Config.local.h`**，只在本地填写 WiFi 和校园网凭据。该文件已加入 `.gitignore`，仓库中的 `Config.h` 保留占位符。也可以手动填写 `Config.h`，但不要提交填写后的文件。

> `.gitignore` 不能保护被强制提交或曾经跟踪过的文件。编译出的固件包含真实凭据，不要上传到 GitHub 或公开分享。详见 [隐私说明](docs/privacy.md)。

### 编译与上传

1. Arduino IDE 安装 Espressif ESP32 支持包，打开 `BUCT_AutoAuth/BUCT_AutoAuth.ino`。
2. 选择 **ESP32C3 Dev Module**。
3. 将 **USB CDC On Boot → Enabled**。本项目使用 ESP32-C3 原生 USB Serial/JTAG；关闭 CDC 时编译会直接提示错误，避免上传后 COM 口无日志。
4. 上传后打开对应串口，波特率 **115200**。不要让两个串口工具同时占用该端口。
5. 脱离电脑后可通过 USB 电源独立运行，不要求串口监视器在线。

外置 USB-UART 或不同板型需自行调整日志适配与编译检查，不能直接套用原生 USB 配置。

## 诊断输出

串口打开时打印当前状态快照，此后约每 15 秒打印一次；同步网络请求可能推迟心跳。日志只读取状态和计数，不额外发送网络探测或认证请求。

典型正常流程（示意，不是原始实机日志）：

```text
WiFi initial connection started
WiFi associated -> waiting for DHCP/IP
WiFi/IP ready -> waiting briefly for router
probe 204 -> ONLINE
portal account verified
```

- `wifi=6`：此刻尚未连接，启动瞬间出现正常；`wifi=3` 表示已连接。
- `link=WAIT_ADDRESS`：已关联 AP，等待 IP/网关；`READY`：本地网络条件就绪。
- `probe`、`portal`、`login`、`logout`：本次运行控制器通过适配器调用相应操作的次数，不等于底层 HTTP 请求总数。
- 探测 `last=1` 成功，`0` 失败，`-1` 未执行。
- 门户 `last=1` 在线，`0` 离线，`-2` 未知，`-1` 未执行。
- 登录 `last=-1` 未执行；其余依次为 `0` 成功、`1` 已在线、`2` 凭据错误、`3` 门户不可达、`4` 协议异常。
- `login=0(last=-1)` 在已有会话下正常，不表示认证失败。
- `MONITOR attached` 表示串口连接重新被识别；判断是否重启要结合运行时间和计数器是否归零。

不打印 URL 查询参数或原始登录响应，但 LAN 状态仍包含本地 IP/网关/DNS，分享日志前仍应脱敏。LED：3 秒一短闪=在线、1Hz=连接 WiFi、4Hz=等待/检查、8Hz=凭据错误。

## 安全默认值

`ACCOUNT_REPLACEMENT_ENABLED=0` 时不会自动登出。公网探测失败但门户说在线时，只等待后复查，不擅自重建会话。

只有主动设为 `1` 才允许账号不一致时登出换号；每次尝试（含失败）至少间隔 30 分钟，冷却跨 WiFi 重连保留，不跨设备重启保留。**换号会影响同一 NAT 下所有设备，应在维护窗口操作。**

真正的残留会话可能仍需人工处理。这是避免把局部网络故障扩大为全网断线的保守选择。

## 验证范围

- 基线：已验证发布提交 `beaaa44`。保留 challenge、HMAC-MD5、xencode、SRBX1、SHA-1、登录参数和原有 HTTP 路径。
- 主机测试：25 个调度场景、13 个协议场景、12 个集成场景，包括模拟时钟跨回绕及默认不登出约束；不是长期实机老化测试。
- ESP32-C3：使用 Arduino-ESP32 3.3.12 对正式版四个源文件完成目标编译检查（未进行完整链接）；用户已上传此前诊断版并提供实际运行日志。
- 已实测：约 1 秒取得 IP、等待 10 秒、三个探测站均返回 204、门户账号核对成功、约 30 秒轮询、监视器重连后输出；提供的日志覆盖约 161 秒，期间没有登录或登出。
- 尚未实测：当前版本在未认证状态下的完整登录、路由器晚启动/重启与 WAN 中断恢复、长时间内存和温升稳定性。
- 本次正式版整理调整了版本标识、日志脱敏和本地配置方式，未再次对运行中的设备执行上传。

## 离线测试

需要 Python、Git 和 g++，在仓库根目录执行：

```powershell
python tools/tests/test_recovery.py
python tools/tests/test_protocol.py
python tools/tests/test_integration.py
python tools/tests/verify_release.py
```

所有模拟请求均留在内存，不会访问真实门户或串口。主机检查不替代 Arduino IDE 完整编译和实机验收。

更多信息：[架构与限制](docs/recovery-architecture.md) · [更新记录](CHANGELOG.md) · [隐私说明](docs/privacy.md)

## 使用约束

请遵守学校网络使用规定。设备不能保证校园网服务、供电或路由器故障时始终在线。项目使用 MIT License。
