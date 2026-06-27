# LabGuard React Native App

基于 Expo SDK 54 的 React Native 手机端应用，用于在 iOS / Android 上实现 LabGuard 现有网页端核心功能。

## 当前已实现

- MQTT WebSocket 连接 / 断开
- 风险卡片
- 温度 / 湿度 / VOC / MQ-2 指标
- 节点状态
- 与网页端一致的板子控制：重置 / 声音 / 灯带 / 报警 / 风扇 / 水泵
- 风扇 / 水泵有效档位调节，风扇最低 60%，水泵最低 55%
- 风扇 / 水泵最终 / 自动 / 手动接管状态显示
- 摄像头 MQTT Base64 画面预览
- 消息流日志
- 风险等级震动提醒

## 当前入口说明

- 默认入口是 [index.ts](index.ts)
- 当前加载的是 [App.tsx](App.tsx)
- [App.tsx](App.tsx) 是项目版完整界面

## 运行

推荐直接使用仓库根目录的联合调试脚本：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan
./run_labguard_stack.sh
```

它会同时启动：

- [web/dashboard](../web/dashboard/) 的 MQTT broker + Dashboard
- [mobile/LabGuard](.) 的 Expo 服务

如果只想单独启动手机端，也可以继续使用当前脚本：

```bash
./run_mobile_app.sh
```

### Android

```bash
npm run android
```

### iOS

```bash
npm run ios
```

> iOS 原生构建通常需要 macOS。若只是在 iPhone 上联调，推荐使用 Expo Go。

## MQTT 地址

通过 `./run_mobile_app.sh` 或仓库根目录的 `./run_labguard_stack.sh` 启动时，脚本会把当前电脑局域网地址注入 App。默认格式是：

```text
ws://电脑IP:9001
```

如果电脑 IP 改了，也可以在 App 顶部输入框手动改成新的地址。

## 控制命令

手机端与网页端统一通过 `labguard/cmd/test` 向单板发送命令：

- `reset`
- `audio_on` / `audio_off`
- `light_on` / `light_off`
- `alarm_on` / `alarm_off`
- `fan_on` / `fan_off`
- `pump_on` / `pump_off`

风扇和水泵开启或调节档位时会携带 `level_pct`，并等待板子通过 `labguard/device/risk` 回传最终状态。声音和灯带状态通过 `labguard/device/status` 的 `audio_looping`、`light_on` 字段同步。

## 注意

- 当前首版只支持 MQTT，不支持 dashboard 的本地串口桥模式。
- 摄像头使用 MQTT 传 Base64 图片，适合演示；高帧率场景后续应改成更适合视频流的方案。
