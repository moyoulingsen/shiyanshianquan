# LabGuard React Native App

基于 Expo SDK 54 的 React Native 手机端应用，用于在 iOS / Android 上实现 LabGuard 现有网页端核心功能。

## 当前已实现

- MQTT WebSocket 连接 / 断开
- 风险卡片
- 温度 / 湿度 / VOC / MQ-2 指标
- 节点状态
- 风扇 / 水泵 / 喇叭状态与控制
- 风扇 / 水泵 0-100 滑杆调节
- 风险演示命令按钮
- 摄像头 MQTT Base64 画面预览
- 消息流日志
- 风险等级震动提醒

## 当前入口说明

- 默认入口是 [index.ts](index.ts)
- 当前加载的是 [App.tsx](App.tsx)
- [App.tsx](App.tsx) 是项目版完整界面

## 运行

推荐直接使用启动脚本：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/mobile/LabGuard
./run_mobile_app.sh
```

如果需要，也可以直接使用 Expo 命令：

```bash
npm start
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

当前应用默认地址写在 [App.tsx](App.tsx) 里：

```ts
ws://192.168.1.100:9001
```

启动后可以在 App 顶部输入框改成你电脑的局域网地址，例如：

```text
ws://172.20.10.14:9001
```

建议后续再把默认地址提取到配置文件。

## 注意

- 当前首版只支持 MQTT，不支持 dashboard 的本地串口桥模式。
- 摄像头使用 MQTT 传 Base64 图片，适合演示；高帧率场景后续应改成更适合视频流的方案。
- `npm run web` 目前未配置完成，因为 Expo web 依赖 `react-dom` 和 `react-native-web` 尚未安装。
