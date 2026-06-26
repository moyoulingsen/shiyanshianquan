# LabGuard Dashboard

功能说明见：

```text
FRONTEND_FEATURES.md
```

网页端支持两种实时数据源：

1. `MQTT WebSocket`：正式方案，ESP32-P4 连接 Wi-Fi/MQTT 后，网页直接订阅 broker。
2. `本地串口桥`：调试/演示方案，电脑从 `/dev/ttyACM0` 读取固件串口日志，再转成浏览器 WebSocket。

## 串口桥快速运行

```bash
cd web/dashboard
npm install
npm run start
```

打开 Vite 输出的地址，默认是：

```text
http://localhost:5173
```

网页默认连接：

```text
ws://localhost:8787
```

如果串口不是 `/dev/ttyACM0`：

```bash
LABGUARD_PORT=/dev/ttyACM1 npm run start
```

## MQTT WebSocket 运行

先启动项目自带的 Mosquitto 配置，它会开两个端口：

```bash
cd web/dashboard
npm run broker
```

端口用途：

```text
1884 = ESP32-P4 固件连接的 MQTT TCP 端口
9001 = 浏览器连接的 MQTT WebSocket 端口
```

电脑当前局域网 IP 可以用下面命令看：

```bash
hostname -I
```

ESP32-P4 固件里填同一个电脑 IP，例如：

```text
CONFIG_LABGUARD_WIFI_SSID="你的WiFi"
CONFIG_LABGUARD_WIFI_PASSWORD="你的密码"
CONFIG_LABGUARD_MQTT_URI="mqtt://电脑IP:1884"
```

网页里选择 `MQTT WebSocket`，地址填：

```text
ws://电脑IP:9001
```

订阅 topic：

```text
labguard/indoor/sensor
labguard/indoor/risk
labguard/indoor/status
labguard/event
```
