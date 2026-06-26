# LabGuard Mobile

手机端 PWA，用来和电脑端 dashboard、ESP32-P4 indoor 节点共用同一个 MQTT broker。

## 功能

- 实时显示 `labguard/indoor/sensor` 的温度、湿度、VOC、MQ-2 状态。
- 实时显示 `labguard/indoor/risk` 的风险等级、风险文本、风扇、水泵、报警状态。
- 实时显示 `labguard/indoor/status` 的运行时间、Wi-Fi RSSI、固件版本。
- 通过 `labguard/cmd/test` 发送 `reset`、`force_normal`、`force_warning`、`force_alarm`、`force_emergency` 命令。
- 通过 `labguard/event` 发布手机端上线和控制事件，电脑端 dashboard 的消息流也能看到。
- 支持添加到手机桌面，以 standalone PWA 方式运行。

## 数据链路

```text
ESP32-P4 indoor
        -> MQTT TCP 1884
        -> 电脑 Mosquitto broker
        -> MQTT WebSocket 9001
        -> 手机端 LabGuard Mobile
        -> labguard/cmd/test 控制命令
        -> ESP32-P4 indoor
```

电脑端 dashboard 和手机端 app 使用同一个 broker、同一组 topic，因此两边看到的是同一份实时数据。

## 运行

先启动电脑端 broker：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
npm run broker
```

再启动手机端页面：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/mobile
npm install
npm run dev
```

手机和电脑连接同一个 Wi-Fi，手机浏览器打开：

```text
http://电脑IP:5174/
```

例如当前电脑 IP 如果是 `172.20.10.14`：

```text
http://172.20.10.14:5174/
```

页面默认会连接：

```text
ws://电脑IP:9001
```

如果默认地址不对，可以在手机页面顶部手动修改 `MQTT WebSocket` 地址。

## 安装到桌面

Android Chrome：

```text
右上角菜单 -> 添加到主屏幕
```

iPhone Safari：

```text
分享 -> 添加到主屏幕
```

安装后打开时会以接近原生 App 的全屏方式运行。
