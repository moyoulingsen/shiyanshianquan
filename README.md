# shiyanshianquan

实验室安全大作业。

## 当前方向

本项目已调整为**单块 ESP32-P4 板子完成完整闭环**：同一块板同时负责视觉识别、环境感知、风险判断、声光联动、排风控制和云端告警，不再强调双板协同。

## 常用启动命令（放这里，优先看这个）

### 1. 全部联调一键启动（最常用）

同时启动电脑端 Dashboard、MQTT broker 和手机 Expo：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan
./run_labguard_stack.sh
```

这个脚本会自动启动：

- [web/dashboard](web/dashboard/)：电脑端监控网页
- 本地 MQTT broker：单板和网页/手机共用
- 本地串口桥：USB 串口调试用，默认会占用 `LABGUARD_PORT`
- [mobile/LabGuard](mobile/LabGuard/)：Expo 手机 App 服务

启动成功后终端会打印这些地址，照着填就行：

```text
电脑网页端: http://localhost:5173
Dashboard: http://你的电脑IP:5173
手机 MQTT WS: ws://你的电脑IP:9001
板子 MQTT: mqtt://你的电脑IP:1884
Expo: exp://你的电脑IP:8081
```

如果只使用 MQTT，或者还要用 `idf.py flash monitor` 烧录/看串口日志，可以不启动串口桥，避免占用 `/dev/ttyACM0`：

```bash
ENABLE_SERIAL_BRIDGE=0 ./run_labguard_stack.sh
```

脚本默认会先尝试释放这些常用端口（1884 / 9001 / 5173 / 8081），避免旧的 broker、Vite、Expo 进程占着不放。只有启用串口桥时才会释放串口桥 WebSocket 端口 8787。
如果你不想自动强制清端口，可以这样启动：

```bash
FORCE_FREE_PORTS=0 ./run_labguard_stack.sh
```

停止全部服务：在运行脚本的终端按 `Ctrl+C`。

### 2. 只启动电脑端 Dashboard + MQTT broker

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
./run_dashboard_stack.sh
```

如果只想用 MQTT，不想让串口桥占用板子的 USB 串口：

```bash
ENABLE_SERIAL_BRIDGE=0 ./run_dashboard_stack.sh
```

如果不想自动释放 1884 / 9001 / 5173 端口，可以这样运行：

```bash
FORCE_FREE_PORTS=0 ./run_dashboard_stack.sh
```

### 3. 只启动手机 Expo App

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/mobile/LabGuard
./run_mobile_app.sh
```

如果不想自动释放 Expo 默认端口，可以这样运行：

```bash
FORCE_FREE_PORTS=0 ./run_mobile_app.sh
```

### 4. 单板 MQTT 配置

烧录前在 `idf.py menuconfig` 里确认：

```text
CONFIG_LABGUARD_WIFI_SSID="你的WiFi"
CONFIG_LABGUARD_WIFI_PASSWORD="你的密码"
CONFIG_LABGUARD_MQTT_URI="mqtt://你的电脑IP:1884"
```

`你的电脑IP` 直接看 `./run_labguard_stack.sh` 或 `./run_dashboard_stack.sh` 启动时打印的 `板子 MQTT` 地址。

### 5. 单板烧录和监视

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/firmware
. /home/lijiaolong/esp/esp-idf/export.sh
idf.py set-target esp32p4
idf.py menuconfig
idf.py -p <串口> flash monitor
```

常见串口一般是 `/dev/ttyACM0` 或 `/dev/ttyUSB0`，可以先看：

```bash
ls /dev/ttyACM* /dev/ttyUSB*
```

例如：

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

### 6. 改默认端口

全部联调脚本可以临时改端口：

```bash
WEB_PORT=5175 EXPO_PORT=8082 MQTT_WS_PORT=9002 MQTT_TCP_PORT=1885 ./run_labguard_stack.sh
```

只启动 Dashboard 时也可以改端口：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
WEB_PORT=5175 MQTT_WS_PORT=9002 MQTT_TCP_PORT=1885 ./run_dashboard_stack.sh
```

注意：`MQTT_TCP_PORT` 改了以后，板子的 `CONFIG_LABGUARD_MQTT_URI` 也要对应改。

## 网页展示怎么打开

项目里现在保留的前端页面是：

- [web/dashboard](web/dashboard/)：电脑端监控大屏

### 1. 电脑端 Dashboard 一键启动（推荐）

现在电脑端已经有一键启动脚本，会自动启动本地 MQTT broker 和 Dashboard 网页服务：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
./run_dashboard_stack.sh
```

第一次运行时，如果 [web/dashboard/node_modules](web/dashboard/node_modules/) 不存在，脚本会自动执行 `npm install` 安装依赖。

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

如果用手机或另一台电脑访问 Dashboard，要保证设备和运行脚本的电脑在同一个局域网，然后打开终端里打印的局域网地址，例如：

```text
http://172.20.10.14:5173
```

停止服务时，在运行脚本的终端按 `Ctrl+C`，脚本会同时关闭 broker 和网页服务。

### 2. Dashboard 端口说明

一键启动脚本默认使用这些端口：

- `5173`：Dashboard 网页地址
- `9001`：网页连接 MQTT 的 WebSocket 地址
- `1884`：ESP32 固件连接 MQTT 的 TCP 地址

如果端口被占用，可以启动前临时改端口：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
WEB_PORT=5175 MQTT_WS_PORT=9002 MQTT_TCP_PORT=1885 ./run_dashboard_stack.sh
```

注意：MQTT 端口改了以后，固件和网页里的连接地址也要对应修改。

### 3. 单板怎么连网页

如果要让单板的数据出现在网页和手机 App 上，需要把固件里的 MQTT 地址指向运行一键启动脚本的电脑：

```text
CONFIG_LABGUARD_MQTT_URI="mqtt://你的电脑IP:1884"
```

同时还要配置 Wi-Fi：

```text
CONFIG_LABGUARD_WIFI_SSID="你的WiFi"
CONFIG_LABGUARD_WIFI_PASSWORD="你的密码"
```

启动 Dashboard 后，终端会打印当前电脑 IP 和应该填入板子的 MQTT 地址，可以直接照着终端输出配置。

### 4. 单板烧录命令

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/firmware
idf.py set-target esp32p4
idf.py menuconfig
idf.py -p <串口> flash monitor
```

`menuconfig` 里重点确认：

```text
CONFIG_LABGUARD_WIFI_SSID="你的WiFi"
CONFIG_LABGUARD_WIFI_PASSWORD="你的密码"
CONFIG_LABGUARD_MQTT_URI="mqtt://你的电脑IP:1884"
```

### 5. 手动启动 Dashboard（备用）

如果一键脚本不能用，也可以手动启动。

先启动 MQTT broker：

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

浏览器打开：

```text
http://localhost:5173
```

网页里 MQTT WebSocket 地址填：

```text
ws://你的电脑IP:9001
```

如果只在本机看，可以填：

```text
ws://localhost:9001
```
