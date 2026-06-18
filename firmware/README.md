# LabGuard SingleP4 Firmware

`firmware/` 现在只保留**已经明确接硬件链路**的单板固件代码。

## 当前保留内容

```text
firmware/
├─ CMakeLists.txt
├─ main/                     单板主流程入口
├─ components/
│  ├─ labguard_common/       公共数据结构与 MQTT 消息
│  ├─ labguard_net/          Wi‑Fi / MQTT 通信
│  ├─ event_log/             事件日志
│  ├─ sensor_reader/         SHT3x / ENS160 / MQ-2 硬件采集
│  ├─ camera_capture/        SC2336 + CSI + ISP + EK79007 硬件链路
│  ├─ actuator_ctrl/         风扇 / 水泵 GPIO + PWM 控制
│  └─ audio_prompt/          SD 卡 WAV + MAX98357A I2S 播放
├─ README.md
├─ 项目介绍.md
└─ B_DELIVERY_NOTES.md
```

## 已删除内容

- 所有演示型/模拟型危险识别代码。
- 所有视觉推理 POC、导出脚本、演示工具脚本。
- 所有 `force_*`、`selftest`、mock/profile 相关主流程依赖。
- `esp_outdoor/`、`esp_indoor/` 旧工程。
- `espdl_probe` 探针组件。

## 当前系统含义

现在这套固件只表示：

- 摄像头链路已接通，可以采集并发布预览帧。
- 传感器链路已接通，可以读取温湿度、VOC、MQ-2。
- 风扇 / 水泵执行器链路已接通，可以按风险等级联动。
- 音频链路已接通，可以从 SD 卡播放告警音。
- 网络链路已接通，可以通过 MQTT 发布状态并接收手动控制命令。

不再宣称板端已经具备真实火焰/烟雾/PPE 推理能力。
