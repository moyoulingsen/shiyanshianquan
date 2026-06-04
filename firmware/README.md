# LabGuard TwinP4 Firmware

`firmware/` 是实验室安全系统的双节点固件工程，面向两块 `ESP32-P4-Function-EV-Board`：

- `esp_outdoor/`：门外准入节点，负责 PPE 检测、门禁决策、语音/灯效提示、触摸屏展示。
- `esp_indoor/`：门内监测节点，负责环境传感、烟雾/火焰检测、风险融合、风扇/水泵联动。
- `components/`：两块板共用的数据模型、事件日志、网络通信组件，以及隔离 ESP-DL 探针组件。
- `vision/视觉推理_POC_ESPDL/`：视觉推理资料区，保存 ESP-DL 交接文档、导出脚本和参考 POC。

## 当前代码能力

这版不是空骨架了，已经补成一套可联调的完整业务固件：

- 双节点都具备主循环、状态发布、心跳上报、事件日志。
- 支持 MQTT 发布/订阅，未配置 Wi-Fi/MQTT 时自动退化为本地日志模式。
- 室内节点会生成传感器数据、视觉风险结果和融合后的风险等级。
- 室内传感器链路已加入卡尔曼滤波，并在 MQTT 中同时上报原始值和滤波值。
- 室外节点会根据 PPE 结果和室内风险做准入判定。
- 室外 UI 状态面板会汇总门禁、PPE、室内风险、传感器值、联动动作和最近事件。
- 支持通过 MQTT 命令切换演示场景，例如 `force_warning`、`force_alarm`、`reset`。
- 所有摄像头、传感器、执行器、LVGL、音频模块都保留了清晰的替换接口，后续可以逐个接入真实驱动。

## 工程结构

```text
firmware/
├─ components/
│  ├─ espdl_probe/        隔离 ESP-DL 模型加载/测试探针
│  ├─ event_log/          公共事件日志
│  ├─ labguard_common/    消息结构体与 JSON 编解码
│  └─ labguard_net/       Wi‑Fi/MQTT 封装
├─ esp_indoor/            门内风险监测节点
├─ esp_outdoor/           门外准入交互节点
└─ vision/视觉推理_POC_ESPDL/ 视觉资料、脚本与参考 POC
```

## 室内视觉推理与 ESP-DL

室内节点的火焰/烟雾入口是 [esp_indoor/components/hazard_infer](esp_indoor/components/hazard_infer)。当前主线实现会优先尝试从 microSD 加载 `.espdl` 模型：

```text
/sdcard/models/p4/lab_fire_smoke.espdl
```

如果模型不存在、ESP-DL 后端不可用，系统会自动回退到占位图像启发式或 mock 输出，保证演示流程不会因为模型缺失而中断。

公共组件 [components/espdl_probe](components/espdl_probe) 是隔离探针，用于单独验证 `.espdl` 能否加载并打印 `model->test()`、`profile_memory()`、`profile_module()` 日志。它默认关闭，不直接参与 PPE、hazard、door 或 risk-fusion 业务链路。

视觉相关交接文档、导出脚本和 POC 配置集中放在 [vision/视觉推理_POC_ESPDL](vision/视觉推理_POC_ESPDL)。

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

## 构建与烧录

先加载 ESP-IDF 环境，再分别编译或烧录两个工程。

### 门外语音播报（microSD + MAX98357A）

门外节点现在支持从板载 microSD 读取固定 WAV 文件，通过 MAX98357A I2S 功放播报语音。

**接线建议：**

- `MAX98357A VIN` -> `5V`
- `MAX98357A GND` -> `GND`
- `MAX98357A DIN` -> `GPIO20`
- `MAX98357A BCLK` -> `GPIO21`
- `MAX98357A LRC/WS` -> `GPIO22`
- 喇叭两根线 -> `MAX98357A SPK+ / SPK-`

注意：喇叭不要直接接 ESP32 GPIO 或 GND。

**SD 卡准备：**

- 文件系统：`FAT32`
- 创建目录：`/audio`
- 放入 16-bit PCM、16 kHz、单声道的 WAV 文件：
  - `/audio/0001.wav`：允许进入
  - `/audio/0002.wav`：请佩戴护目镜
  - `/audio/0003.wav`：请穿实验服
  - `/audio/0004.wav`：室内危险/警告/离线
  - `/audio/0005.wav`：拒绝进入

如果你的源文件是 mp3/m4a，可以用 ffmpeg 转换：

```bash
ffmpeg -i input.mp3 -ac 1 -ar 16000 -sample_fmt s16 0001.wav
```

## 构建与烧录

先加载 ESP-IDF 环境，再分别编译或烧录两个工程。

室外节点：

```bash
cd firmware/esp_outdoor
idf.py set-target esp32p4
idf.py build
```

室内节点：

```bash
cd firmware/esp_indoor
idf.py set-target esp32p4
idf.py build
```

### 推荐烧录命令

室外节点：

```bash
cd firmware/esp_outdoor
idf.py set-target esp32p4
idf.py menuconfig
idf.py -p <串口> flash monitor
```

室内节点：

```bash
cd firmware/esp_indoor
idf.py set-target esp32p4
idf.py menuconfig
idf.py -p <串口> flash monitor
```

在 `menuconfig` 里重点确认：

- `CONFIG_LABGUARD_WIFI_SSID`
- `CONFIG_LABGUARD_WIFI_PASSWORD`
- `CONFIG_LABGUARD_MQTT_URI=mqtt://你的电脑IP:1884`

如果本机没有 `idf.py`，先按 ESP-IDF v5.5.x 完成环境安装和导出。

## 配置建议

当前默认配置建议通过以下入口修改，而不是直接改源码：

- `idf.py menuconfig`
- `sdkconfig.local`
- [esp_indoor/main/Kconfig.projbuild](esp_indoor/main/Kconfig.projbuild)
- [esp_outdoor/main/Kconfig.projbuild](esp_outdoor/main/Kconfig.projbuild)

联合联调时推荐统一使用：

- 手机端 WebSocket：`ws://你的电脑IP:9001`
- 板子 MQTT TCP：`mqtt://你的电脑IP:1884`

这样和 dashboard / mobile 端的一键脚本保持一致。

## 文档

- [项目介绍.md](项目介绍.md)
- [B_DELIVERY_NOTES.md](B_DELIVERY_NOTES.md)
- [硬件组装说明.md](硬件组装说明.md)
- [HARDWARE_VERIFICATION.md](HARDWARE_VERIFICATION.md)
- [vision/视觉推理_POC_ESPDL/README.md](vision/视觉推理_POC_ESPDL/README.md)
- [components/espdl_probe/README.md](components/espdl_probe/README.md)
