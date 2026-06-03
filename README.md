# shiyanshianquan

实验室安全大作业。

## 网页展示怎么打开

项目里有两个前端页面：

- [web/dashboard](web/dashboard/)：电脑端监控大屏
- [web/mobile](web/mobile/)：手机端页面

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

### 3. ESP32 固件怎么连网页

如果要让 `esp_indoor` 的数据出现在网页上，需要把固件里的 MQTT 地址指向运行一键启动脚本的电脑：

```text
CONFIG_LABGUARD_MQTT_URI="mqtt://你的电脑IP:1884"
```

同时还要配置 Wi-Fi：

```text
CONFIG_LABGUARD_WIFI_SSID="你的WiFi"
CONFIG_LABGUARD_WIFI_PASSWORD="你的密码"
```

启动 Dashboard 后，终端会打印当前电脑 IP 和应该填入板子的 MQTT 地址，可以直接照着终端输出配置。

### 4. 手动启动 Dashboard（备用）

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

### 5. 手机端页面

手机端页面单独启动：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/mobile
npm install
npm run dev
```

浏览器打开：

```text
http://localhost:5174
```

如果要用手机访问，手机和电脑必须连同一个 Wi‑Fi，然后在手机浏览器打开：

```text
http://你的电脑IP:5174/
```

例如：

```text
http://172.20.10.14:5174/
```

手机端默认连接运行在 `9001` 端口的 MQTT WebSocket。如果要看到实时数据，电脑端还需要先运行 Dashboard 的一键启动脚本，或者至少启动 [web/dashboard](web/dashboard/) 里的 MQTT broker。
