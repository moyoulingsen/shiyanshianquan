# B 组交付说明：仅保留硬件链路

## 当前结果

本次已经按“只保留明确接上线的代码”进行了清理。

## 已保留

- 摄像头硬件链路：`SC2336 -> CSI -> ISP -> EK79007`
- 传感器硬件链路：`SHT3x / ENS160 / MQ-2`
- 执行器硬件链路：风扇 / 水泵 GPIO + PWM
- 音频硬件链路：SD 卡 WAV + MAX98357A I2S
- 网络链路：Wi‑Fi / MQTT

## 已删除

- `esp_outdoor/`
- `esp_indoor/`
- 危险识别 mock / 演示 / placeholder 推理代码
- `vision/` 资料区
- `tools/` 脚本区
- 主流程里的 `force_*` / `selftest` / profile 切换逻辑

## 当前仓库含义

当前代码只代表“硬件已经接通并能跑链路”，不再代表“AI 识别能力已经完成”。

当前 `firmware/` 已收敛为顶层单板工程。
