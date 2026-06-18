# LabGuard Dashboard 前端功能说明

## 1. 当前定位

`LabGuard Dashboard` 是当前仓库保留的**电脑端监控网页**，用于显示单板设备的实时状态，并提供基础执行器控制。

当前页面只面向现有单板硬件链路：

- 传感器状态查看
- 风险状态查看
- 摄像头预览帧显示
- 风扇 / 水泵 / 报警控制
- MQTT 消息流查看

不再承担旧的演示场景切换与双板 indoor/outdoor 展示职责。

## 2. 支持的数据源

网页端支持两种实时数据源：

1. `MQTT WebSocket`：正式方案，固件连入 Wi‑Fi 后，网页直接订阅 broker。
2. `本地串口桥`：调试方案，电脑从串口日志读取消息并转成浏览器 WebSocket。

## 3. 当前订阅的 Topic

Dashboard 在 MQTT 模式下订阅：

```text
labguard/device/sensor
labguard/device/risk
labguard/device/status
labguard/device/camera
labguard/event
```

## 4. 当前可发送的控制命令

Dashboard 当前保留这些控制能力：

- `reset`
- `fan_on` / `fan_off`
- `pump_on` / `pump_off`
- `alarm_on` / `alarm_off`

命令通过 `labguard/cmd/test` 发送到单板设备。

## 5. 页面主要面板

- 传感器卡片：温度、湿度、VOC、MQ-2
- 风险卡片：当前风险等级与说明
- 摄像头面板：MQTT Base64 预览帧
- 执行器控制：风扇 / 水泵 / 报警
- 消息流：最近收到的原始消息

## 6. 运行方式

### 6.1 一键启动（推荐）

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
./run_dashboard_stack.sh
```

### 6.2 手动启动

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
npm install
npm run broker
```

第二个终端：

```bash
cd /home/lijiaolong/labguard/shiyanshianquan/web/dashboard
npm run dev
```

## 7. 当前说明

这个前端页面现在已经与仓库主线一致：

- 不再使用 `labguard/indoor/*`
- 不再包含 `force_*` 风险演示按钮
- 不再强调双板 indoor/outdoor 架构
