# shiyanshianquan

实验室安全大作业。

## 网页展示怎么打开

### 快速启动（推荐先这样跑）

先启动 MQTT broker：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
npm run broker
```

再开第二个终端启动网页：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
npm install
npm run dev
```

浏览器打开：

```text
http://localhost:5173
```

网页里 MQTT WebSocket 地址填：

```text
ws://localhost:9001
```

如果你是用手机或别的电脑访问网页，不要填 `localhost`，要改成运行 broker 那台电脑的局域网 IP，例如：

```text
ws://172.20.10.14:9001
```

项目里有两个前端页面：

- `web/dashboard`：电脑端监控大屏
- `web/mobile`：手机端页面

### 1. 打开电脑端 Dashboard

先进入目录并安装依赖：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
npm install
```

#### 方式 A：本地调试展示

这个方式适合先把网页跑起来看界面。

```bash
npm run start
```

它会同时启动：

- Vite 网页服务
- 串口桥服务

浏览器打开：

```text
http://localhost:5173
```

默认网页会连接本地串口桥：

```text
ws://localhost:8787
```

如果你的串口不是 `/dev/ttyACM0`，可以这样启动：

```bash
LABGUARD_PORT=/dev/ttyACM1 npm run start
```

#### 方式 B：正式 MQTT 展示

先启动 broker：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
npm run broker
```

这个 broker 会打开：

- `1884`：给 ESP32 固件连接
- `9001`：给网页 WebSocket 连接

再单独启动网页：

```bash
npm run dev
```

浏览器打开：

```text
http://localhost:5173
```

网页里把 MQTT WebSocket 地址填成：

```text
ws://你的电脑IP:9001
```

比如你的电脑 IP 是 `172.20.10.14`，那就是：

```text
ws://172.20.10.14:9001
```

电脑 IP 可以这样查看：

```bash
hostname -I
```

### 2. 打开手机端页面

先启动手机端页面：

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

手机端默认连接：

```text
ws://你的电脑IP:9001
```

所以如果你要让手机端看到实时数据，电脑端还要先启动：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
npm run broker
```

### 3. ESP32 固件怎么连网页

如果你要让 `esp_indoor` 的数据出现在网页上，需要把固件里的 MQTT 地址指向运行 broker 的电脑：

```text
CONFIG_LABGUARD_MQTT_URI="mqtt://你的电脑IP:1884"
```

同时还要配置：

```text
CONFIG_LABGUARD_WIFI_SSID="你的WiFi"
CONFIG_LABGUARD_WIFI_PASSWORD="你的密码"
```

### 4. 最简单的打开方法

如果你现在只是想先看到网页界面，直接执行：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
npm install
npm run start
```

然后浏览器打开：

```text
http://localhost:5173
```

如果你愿意，我下一步可以继续帮你把“网页展示启动步骤”也补到 [web/dashboard/README.md](web/dashboard/README.md) 里，并顺手整理成更适合答辩演示的版本。
