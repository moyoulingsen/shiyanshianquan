# LabGuard TwinP4 Firmware

`firmware/` 是实验室安全系统的双节点固件工程，面向两块 `ESP32-P4-Function-EV-Board`：

- `esp_outdoor/`：门外准入节点，负责 PPE 检测、门禁决策、语音/灯效提示、触摸屏展示。
- `esp_indoor/`：门内监测节点，负责环境传感、烟雾/火焰检测、风险融合、风扇/水泵联动。
- `components/`：两块板共用的数据模型、事件日志、网络通信组件。

## 当前代码能力

这版不是空骨架了，已经补成一套可联调的完整业务固件：

- 双节点都具备主循环、状态发布、心跳上报、事件日志。
- 支持 MQTT 发布/订阅，未配置 Wi-Fi/MQTT 时自动退化为本地日志模式。
- 室内节点会生成传感器数据、视觉风险结果和融合后的风险等级。
- 室外节点会根据 PPE 结果和室内风险做准入判定。
- 支持通过 MQTT 命令切换演示场景，例如 `force_warning`、`force_alarm`、`reset`。
- 所有摄像头、传感器、执行器、LVGL、音频模块都保留了清晰的替换接口，后续可以逐个接入真实驱动。

## 工程结构

```text
firmware/
├─ components/
│  ├─ event_log/          公共事件日志
│  ├─ labguard_common/    消息结构体与 JSON 编解码
│  └─ labguard_net/       Wi‑Fi/MQTT 封装
├─ esp_indoor/            门内风险监测节点
└─ esp_outdoor/           门外准入交互节点
```

## 主要 Topic

- `labguard/outdoor/status`
- `labguard/outdoor/ppe`
- `labguard/outdoor/access`
- `labguard/indoor/status`
- `labguard/indoor/sensor`
- `labguard/indoor/risk`
- `labguard/event`
- `labguard/cmd/reset`
- `labguard/cmd/test`

命令载荷示例：

```json
{"type":"command","command":"force_alarm","target_node":"indoor","timestamp":1715000000}
```

可用命令：

- `reset`
- `selftest`
- `admin_lock`
- `admin_unlock`
- `force_normal`
- `force_warning`
- `force_alarm`
- `force_emergency`

## 构建

先加载 ESP-IDF 环境，再分别编译两个工程。

室外节点：

```powershell
cd firmware/esp_outdoor
idf.py set-target esp32p4
idf.py build
```

室内节点：

```powershell
cd firmware/esp_indoor
idf.py set-target esp32p4
idf.py build
```

如果本机没有 `idf.py`，先按 ESP-IDF v5.5.x 完成环境安装和导出。

## 配置建议

当前 `app_main.c` 里默认使用：

- Wi‑Fi SSID：`CONFIGURE_ME`
- Wi‑Fi Password：`CONFIGURE_ME`
- MQTT Broker：`mqtt://192.168.1.10`

建议你后续把这些参数迁到：

- `Kconfig.projbuild`
- `menuconfig`
- 或 NVS 持久化配置

这样比赛现场改网络会更方便。

## 文档

- [项目介绍.md](/d:/实验室安全/firmware/项目介绍.md)
- [硬件组装说明.md](/d:/实验室安全/firmware/硬件组装说明.md)
- [HARDWARE_VERIFICATION.md](/d:/实验室安全/firmware/HARDWARE_VERIFICATION.md)
