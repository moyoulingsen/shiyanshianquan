# LabGuard Indoor 前端功能说明

## 1. 前端定位

`LabGuard Indoor Dashboard` 是室内节点的实时监控网页，用来把 ESP32-P4 室内板子采集到的环境数据、风险判断结果和节点运行状态可视化展示出来。

它不是一个静态展示页，而是一个可以实时接收数据的操作面板。当前前端支持两种数据来源：

1. `MQTT WebSocket`：正式联网方案。ESP32-P4 连接 Wi-Fi 后，把数据发布到 MQTT broker，网页通过 WebSocket 订阅 MQTT topic。
2. `本地串口桥`：调试方案。电脑读取 `/dev/ttyACM0` 的串口日志，把固件打印的 `local publish ... payload=...` 转成浏览器能接收的 WebSocket 数据。

这两个模式的显示效果一致，区别只在数据是从 Wi-Fi/MQTT 来，还是从本机串口日志来。

## 2. 页面主要功能

### 2.1 连接状态显示

页面右上角有连接状态指示：

- `未连接`：网页还没有连接任何数据源。
- `连接本地串口桥...`：正在连接本机串口桥。
- `本地串口桥已连接`：网页已经连上 `ws://localhost:8787`，可以接收串口桥转发的数据。
- `连接 MQTT...`：正在连接 MQTT WebSocket broker。
- `MQTT 已连接`：网页已经连上 MQTT broker，并开始订阅室内节点 topic。
- `MQTT 重连中...`、`MQTT 已断开`、`MQTT 连接错误`：网络连接异常或 broker 不可用。

状态点颜色也会变化：

- 绿色：连接正常。
- 黄色：正在连接或重连。
- 红色：未连接或连接失败。

### 2.2 数据源切换

页面顶部的 `数据源` 下拉框可以切换两种模式。

`本地串口桥` 模式：

```text
WebSocket = ws://localhost:8787
```

适合在板子还没有配置 Wi-Fi/MQTT 时使用。只要电脑能从 `/dev/ttyACM0` 读到固件日志，网页就能显示数据。

`MQTT WebSocket` 模式：

```text
MQTT WS = ws://电脑IP:9001
```

适合正式联网运行。ESP32-P4 通过 Wi-Fi 把数据发到 MQTT broker，网页直接订阅 broker。

## 3. 实时环境指标

前端首页第一排是 4 个核心传感器指标卡片。

### 3.1 温度

字段来源：

```json
temperature_c
```

显示单位：

```text
°C
```

对应你的 SHT3x-DIS 温湿度传感器。

### 3.2 湿度

字段来源：

```json
humidity_rh
```

显示单位：

```text
%RH
```

同样来自 SHT3x-DIS。

### 3.3 VOC 指标

字段来源：

```json
voc_index
```

对应 ENS160 空气质量传感器输出的 VOC 指标。

### 3.4 MQ-2 报警状态

字段来源：

```json
mq2_alarm
```

显示逻辑：

- `false` 显示为 `正常`
- `true` 显示为 `报警`

如果 `mq2_alarm=true`，页面会把 MQ-2 状态显示为红色，便于快速发现烟雾或可燃气体报警。

## 4. 风险融合显示

页面中部左侧是 `风险融合` 面板，用来展示固件根据多传感器数据计算出的风险状态。

### 4.1 风险等级

字段来源：

```json
risk_level
```

前端映射关系：

```text
0 = normal
1 = warning
2 = alarm
3 = emergency
```

不同风险等级会显示不同颜色的标签：

- `normal`：绿色，正常。
- `warning`：黄色，预警。
- `alarm`：橙红色，报警。
- `emergency`：深红色，紧急。

### 4.2 风险文本

字段来源：

```json
risk_text
```

例如：

```json
{
  "risk_text": "normal"
}
```

前端会把它显示在 `风险文本` 一栏。

### 4.3 执行动作状态

字段来源：

```json
actions
```

前端会根据 `actions` 数组判断风扇、水泵、报警器是否开启。

映射关系：

```text
fan_on   -> 风扇：开启
pump_on  -> 水泵：开启
alarm_on -> 报警：开启
```

如果动作不存在，则显示为 `关闭`。

示例：

```json
{
  "actions": ["fan_on", "pump_on"]
}
```

页面显示：

```text
风扇：开启
水泵：开启
报警：关闭
```

## 5. 节点状态显示

页面中部右侧是 `节点状态` 面板，用来显示 ESP32-P4 室内节点本身的运行状态。

### 5.1 传感器健康状态

字段来源：

```json
sensor_ok
```

显示逻辑：

- `true` 显示 `传感器正常`
- `false` 显示 `传感器异常`

### 5.2 运行时间

字段来源：

```json
uptime_s
```

前端会把秒数格式化成更容易看的形式：

```text
35s
2m 10s
1h 3m 20s
```

### 5.3 Wi-Fi RSSI

字段来源：

```json
wifi_rssi
```

显示单位：

```text
dBm
```

如果当前是本地串口模式，或者固件还没有连上 Wi-Fi，可能显示为 `0 dBm` 或 `--`。

### 5.4 固件版本

字段来源：

```json
version
```

用于确认当前板子运行的是哪个固件版本，例如：

```json
{
  "version": "0.3.0"
}
```

### 5.5 最后更新时间

每次收到新的 sensor、risk 或 status 消息时，页面都会更新 `最后更新` 时间。这个字段可以用来判断网页是否还在持续收到实时数据。

## 6. 演示控制

页面提供一组 `演示控制` 按钮：

```text
重置
正常
预警
报警
紧急
```

这些按钮会向命令 topic 发送测试命令：

```text
labguard/cmd/test
```

命令 payload 格式：

```json
{
  "node": "dashboard",
  "type": "command",
  "command": "force_alarm",
  "target_node": "indoor",
  "timestamp": 1779210000
}
```

当前按钮的用途是演示和测试。如果固件端订阅并处理了 `labguard/cmd/test`，就可以通过网页触发不同风险状态，验证风扇、水泵、报警器联动显示是否正确。

## 7. 消息流

页面底部是 `消息流` 面板，用来查看最近收到的原始消息。

每条消息包含：

- 接收时间
- MQTT topic 或串口桥转发 topic
- JSON payload

前端最多保留最近 80 条消息，避免页面无限增长。

点击 `清空` 按钮可以清除当前页面上的消息记录，不会影响板子或 MQTT broker。

## 8. 当前订阅的 Topic

前端在 MQTT 模式下会自动订阅以下 topic：

```text
labguard/indoor/sensor
labguard/indoor/risk
labguard/indoor/status
labguard/event
```

### 8.1 传感器数据 topic

```text
labguard/indoor/sensor
```

典型 payload：

```json
{
  "node": "indoor",
  "type": "sensor",
  "temperature_c": 26.65,
  "humidity_rh": 67.25,
  "voc_index": 50,
  "mq2_alarm": false,
  "sensor_ok": true,
  "timestamp": 235
}
```

这个 topic 会更新：

- 温度
- 湿度
- VOC 指标
- MQ-2 状态
- 传感器健康状态
- 最后更新时间

### 8.2 风险状态 topic

```text
labguard/indoor/risk
```

典型 payload：

```json
{
  "node": "indoor",
  "type": "risk_state",
  "risk_level": 0,
  "risk_text": "normal",
  "smoke": false,
  "flame": false,
  "gas_alarm": false,
  "temperature_c": 26.65,
  "actions": [],
  "model": "hazard_sim_v1",
  "timestamp": 235
}
```

这个 topic 会更新：

- 风险等级
- 风险文本
- 风扇状态
- 水泵状态
- 报警状态
- 最后更新时间

### 8.3 节点状态 topic

```text
labguard/indoor/status
```

典型 payload：

```json
{
  "node": "indoor",
  "type": "status",
  "online": true,
  "uptime_s": 235,
  "wifi_rssi": -52,
  "version": "0.3.0",
  "timestamp": 235
}
```

这个 topic 会更新：

- 运行时间
- Wi-Fi 信号强度
- 固件版本
- 最后更新时间

## 9. 两种数据链路说明

### 9.1 本地串口桥链路

数据流：

```text
ESP32-P4 串口日志
        -> /dev/ttyACM0
        -> serial-bridge.mjs
        -> ws://localhost:8787
        -> 浏览器前端
```

固件日志中需要出现类似内容：

```text
local publish topic=labguard/indoor/sensor qos=1 retain=0 payload={...}
```

串口桥会解析 `topic` 和 `payload`，转发给网页。

优点：

- 不需要配置板子 Wi-Fi。
- 适合快速确认传感器接线是否成功。
- 适合调试阶段。

限制：

- 必须有 USB 线连接电脑。
- `/dev/ttyACM0` 不能同时被 `idf.py monitor` 和串口桥占用。
- 这不是最终联网运行方式。

### 9.2 MQTT WebSocket 链路

数据流：

```text
ESP32-P4
        -> Wi-Fi
        -> MQTT broker TCP 1884
        -> MQTT WebSocket 9001
        -> 浏览器前端
```

优点：

- 板子和网页通过网络通信。
- 不需要网页电脑直接占用串口。
- 更接近正式演示和部署方式。

限制：

- ESP32-P4 固件必须正确配置 Wi-Fi SSID、密码和 MQTT broker URI。
- 电脑、ESP32-P4 和浏览器需要在同一局域网中。
- MQTT broker 必须运行，并开放 `1884` 和 `9001` 端口。

## 10. 运行方式

### 10.1 启动网页

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
npm run dev
```

默认访问：

```text
http://localhost:5173/
```

局域网访问示例：

```text
http://电脑IP:5173/
```

### 10.2 启动本地串口桥

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
npm run bridge
```

默认读取：

```text
/dev/ttyACM0
```

默认 WebSocket：

```text
ws://localhost:8787
```

如果串口不是 `/dev/ttyACM0`：

```bash
LABGUARD_PORT=/dev/ttyACM1 npm run bridge
```

### 10.3 启动 MQTT broker

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
npm run broker
```

端口：

```text
1884 = ESP32-P4 固件连接
9001 = 浏览器 MQTT WebSocket 连接
```

ESP32-P4 固件里 MQTT URI 示例：

```text
mqtt://电脑IP:1884
```

网页里 MQTT WebSocket 示例：

```text
ws://电脑IP:9001
```

## 11. 成功运行时的判断标准

### 11.1 串口桥模式成功

满足以下现象说明成功：

- `npm run bridge` 显示正在读取 `/dev/ttyACM0`。
- 网页连接 `ws://localhost:8787` 后显示 `本地串口桥已连接`。
- 页面温度、湿度、VOC、MQ-2 等数据持续刷新。
- 消息流中能看到 `labguard/indoor/sensor`、`labguard/indoor/risk`、`labguard/indoor/status`。

### 11.2 MQTT 模式成功

满足以下现象说明成功：

- 固件串口日志出现 `Wi-Fi connected and got IP`。
- 固件串口日志出现 `MQTT connected`。
- 网页连接 `ws://电脑IP:9001` 后显示 `MQTT 已连接`。
- 页面数据持续刷新。
- MQTT broker 日志中能看到室内节点发布消息。

## 12. 当前前端文件结构

```text
web/dashboard/
├── index.html                  # 页面结构
├── src/main.js                 # 数据连接、消息解析、页面更新逻辑
├── src/styles.css              # 页面样式
├── serial-bridge.mjs           # 串口日志到 WebSocket 的桥接服务
├── mosquitto-labguard.conf     # MQTT TCP + WebSocket broker 配置
├── package.json                # 前端和辅助服务命令
├── README.md                   # 快速运行说明
└── FRONTEND_FEATURES.md        # 本功能说明文档
```

## 13. 适合展示时说明的一句话

这个前端用于实时展示室内实验安全节点的环境数据和风险状态，支持串口调试模式和 Wi-Fi/MQTT 正式联网模式，可以显示温湿度、VOC、MQ-2 报警、风险等级、执行动作、节点状态和原始消息流，并提供演示控制按钮验证联动逻辑。
