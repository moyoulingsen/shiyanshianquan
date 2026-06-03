# LabGuard Dashboard

功能说明见：

```text
FRONTEND_FEATURES.md
```

网页端支持两种实时数据源：

1. `MQTT WebSocket`：正式方案，ESP32-P4 连接 Wi‑Fi/MQTT 后，网页直接订阅 broker。
2. `本地串口桥`：调试/演示方案，电脑从 `/dev/ttyACM0` 读取固件串口日志，再转成浏览器 WebSocket。

## 一键启动（推荐）

现在目录里已经提供了一键启动脚本，会自动完成下面两件事：

- 启动本地 MQTT broker
- 启动 Dashboard 网页服务

运行方式：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
./run_dashboard_stack.sh
```

如果第一次运行时还没有安装依赖，脚本会自动执行 `npm install`。

启动成功后终端会打印类似下面的信息：

```text
LabGuard Dashboard 已启动
网页地址:  http://localhost:5173
局域网访问: http://你的电脑IP:5173
MQTT WS:   ws://你的电脑IP:9001
板子 MQTT: mqtt://你的电脑IP:1884
```

浏览器打开：

```text
http://localhost:5173
```

如果你要用手机或另一台电脑访问 Dashboard，就打开终端里显示的局域网地址，例如：

```text
http://172.20.10.14:5173
```

停止服务时，在脚本运行的终端按 `Ctrl+C`，会同时关闭 broker 和网页服务。

## 端口说明

一键启动脚本默认使用：

```text
5173 = Dashboard 网页地址
9001 = 浏览器连接的 MQTT WebSocket 端口
1884 = ESP32-P4 固件连接的 MQTT TCP 端口
```

如果端口被占用，可以启动前临时指定：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
WEB_PORT=5175 MQTT_WS_PORT=9002 MQTT_TCP_PORT=1885 ./run_dashboard_stack.sh
```

如果你修改了 MQTT 端口，记得网页里填写的地址和固件里的 MQTT 地址也要同步修改。

## MQTT WebSocket 连接方式

ESP32-P4 固件里要填运行脚本那台电脑的局域网 IP，例如：

```text
CONFIG_LABGUARD_WIFI_SSID="你的WiFi"
CONFIG_LABGUARD_WIFI_PASSWORD="你的密码"
CONFIG_LABGUARD_MQTT_URI="mqtt://电脑IP:1884"
```

网页里选择 `MQTT WebSocket`，地址填：

```text
ws://电脑IP:9001
```

例如：

```text
ws://172.20.10.14:9001
```

订阅 topic：

```text
labguard/indoor/sensor
labguard/indoor/risk
labguard/indoor/status
labguard/event
```

## 手动启动（备用）

如果一键脚本暂时不能用，也可以手动启动。

先启动 broker：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
npm install
npm run broker
```

再开第二个终端启动网页：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
npm run dev
```

浏览器默认打开：

```text
http://localhost:5173
```

网页里选择 `MQTT WebSocket`，地址填：

```text
ws://电脑IP:9001
```

如果只在本机调试，也可以填：

```text
ws://localhost:9001
```

## 串口桥调试方式

如果你只是想本地调试串口桥数据流，可以继续使用旧的串口桥方式：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
npm install
npm run start
```

打开默认地址：

```text
http://localhost:5173
```

这个模式下网页默认连接：

```text
ws://localhost:8787
```

如果串口不是 `/dev/ttyACM0`，可以这样指定：

```bash
LABGUARD_PORT=/dev/ttyACM1 npm run start
```
