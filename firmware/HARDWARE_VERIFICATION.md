# 实物到货后的代码验证流程

这份文档用于指导拿到 ESP32-P4-Function-EV-Board、摄像头、屏幕、传感器和执行器之后，如何一步一步验证代码。原则是先验证开发板，再验证通信，再验证单模块，最后做双板联调。不要一开始把所有硬件都接上。

## 0. 验证前准备

### 工具

- ESP-IDF v5.5.2。
- VS Code 或终端。
- 两根质量可靠的 Type-C 数据线。
- 5V/4A 电源。
- 万用表。
- MQTTX 或 `mosquitto_sub` / `mosquitto_pub`。
- 路由器，要求 2.4GHz Wi-Fi 可用。

### 代码目录

门外板：

```bash
cd firmware/esp_outdoor
```

门内板：

```bash
cd firmware/esp_indoor
```

### 串口确认

插上一块板后查看串口：

```bash
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

建议给两块板贴标签：

- `P4-A`：门外板。
- `P4-B`：门内板。

## 1. 上电前硬件检查

先不要接风扇、水泵、舵机、灯带、MQ-2 等负载。

检查项：

- [ ] 开发板没有明显损坏。
- [ ] Type-C 线可以传数据，不只是充电线。
- [ ] 电源正负极确认正确。
- [ ] 5V 和 GND 没有短路。
- [ ] 3.3V 和 GND 没有短路。
- [ ] 摄像头和屏幕 FPC 方向确认正确。
- [ ] MQ-2 的 AO 暂时不接。
- [ ] 风扇、水泵、舵机、灯带暂时不接开发板 5V。

通过标准：

- 万用表测量无短路。
- 单独给 P4 上电后，板载电源指示正常。

失败先查：

- Type-C 线。
- 供电口是否正确。
- 是否有杜邦线误插。
- 是否有金属物压在板子背面。

## 2. ESP-IDF 基础编译验证

先只编译，不烧录。

门外板：

```bash
cd firmware/esp_outdoor
idf.py set-target esp32p4
idf.py build
```

门内板：

```bash
cd firmware/esp_indoor
idf.py set-target esp32p4
idf.py build
```

通过标准：

- 两个工程都能编译通过。
- `build/` 目录生成固件。

失败先查：

- `IDF_PATH` 是否设置。
- ESP-IDF 是否为 v5.5.2。
- 是否执行过 ESP-IDF 的 `export.sh`。
- 当前路径是否在 `firmware/esp_outdoor` 或 `firmware/esp_indoor`。

常用环境加载命令示例：

```bash
. $HOME/esp/esp-idf/export.sh
```

实际路径按你的 ESP-IDF 安装位置修改。

## 3. 单板烧录和串口日志验证

门外板：

```bash
cd firmware/esp_outdoor
idf.py -p /dev/ttyACM0 flash monitor
```

门内板：

```bash
cd firmware/esp_indoor
idf.py -p /dev/ttyACM1 flash monitor
```

串口号按实际情况修改。

当前代码的预期日志：

门外板应看到类似：

```text
LabGuard outdoor node starting
outdoor camera abstraction initialized
PPE inference simulator initialized
door FSM initialized
LVGL summary UI initialized
audio prompt initialized
LED status initialized
```

门内板应看到类似：

```text
LabGuard indoor node starting
indoor camera abstraction initialized
sensor reader simulator initialized
hazard inference simulator initialized
risk fusion initialized
actuator controller initialized
```

通过标准：

- 能烧录成功。
- 串口持续输出日志。
- 没有反复重启。
- 没有 Guru Meditation、abort、栈溢出等错误。

失败先查：

- 串口号是否选错。
- 是否有另一个 monitor 占用串口。
- USB 线是否稳定。
- 供电是否不足。

## 4. 当前业务逻辑验证

在还没有完全接入真实硬件驱动时，当前代码会使用内置演示场景生成 PPE、传感器和风险数据。先确认状态机、JSON、事件日志和双节点逻辑正确。

门外板预期：

- 每秒生成 PPE 假结果。
- 每秒生成准入决策。
- 发布 `labguard/outdoor/ppe`。
- 发布 `labguard/outdoor/access`。
- UI、LED、音频模块打印状态日志。

门内板预期：

- 每秒生成传感器假数据。
- 每秒生成风险状态。
- 发布 `labguard/indoor/sensor`。
- 发布 `labguard/indoor/risk`。
- 执行器模块打印联动日志。

通过标准：

- JSON 字段与 `components/labguard_common/include/labguard_common.h` 定义一致。
- 自动场景下会周期性出现 `pass / missing_goggles / indoor_warning` 等不同状态。
- 门内自动场景下会周期性出现 `normal / warning / alarm / emergency` 的切换。

失败先查：

- `labguard_common` 组件是否被正确引用。
- 是否出现 JSON 内存分配失败。
- 主任务栈是否不足。

## 5. 替换真实 Wi-Fi 和 MQTT

优先替换：

```text
firmware/components/labguard_net/labguard_net.c
```

目标：

- 连接 2.4GHz Wi-Fi。
- 连接 MQTT broker。
- 支持发布。
- 支持订阅。
- 断线自动重连。

建议先固定 MQTT broker 地址，例如：

```text
mqtt://192.168.1.10
```

验证命令：

```bash
mosquitto_sub -h 192.168.1.10 -t 'labguard/#' -v
```

通过标准：

- 门内板能看到 `labguard/indoor/sensor`。
- 门内板能看到 `labguard/indoor/risk`。
- 门外板能看到 `labguard/outdoor/ppe`。
- 门外板能看到 `labguard/outdoor/access`。
- 断开路由器再恢复后，节点能重连。

失败先查：

- 路由器是否为 2.4GHz。
- SSID 和密码是否正确。
- MQTT broker IP 是否正确。
- 电脑和两块 P4 是否在同一局域网。
- 防火墙是否阻止 1883 端口。

## 6. 门内板传感器验证

优先替换：

```text
firmware/esp_indoor/components/sensor_reader/
```

建议顺序：

1. 只接 SHT30。
2. 再接 ENS160 或 SGP40。
3. 最后接 MQ-2 DO。

I2C 检查：

- SDA/SCL 线序正确。
- 模块供电为 3.3V，除非模块说明明确要求其他电压。
- I2C 上拉不要拉到 5V。
- SHT30 和 ENS160/SGP40 地址不冲突。

MQ-2 检查：

- VCC 接 5V。
- GND 接 GND。
- DO 接 P4 GPIO。
- AO 不接 P4。

通过标准：

- `labguard/indoor/sensor` 中温湿度有真实变化。
- VOC 指数有真实变化。
- MQ-2 触发时 `mq2_alarm` 从 `false` 变为 `true`。
- 传感器断开时 `sensor_ok` 能进入异常状态。

失败先查：

- I2C 地址。
- SDA/SCL 是否接反。
- GND 是否共地。
- MQ-2 是否预热。
- MQ-2 模块电位器阈值是否合适。

## 7. 门内板执行器验证

优先替换：

```text
firmware/esp_indoor/components/actuator_ctrl/
```

先不要接水泵，先用 LED 或万用表验证 GPIO 输出。

建议顺序：

1. GPIO 输出验证。
2. 继电器/MOSFET 空载验证。
3. 接风扇验证。
4. 接水泵验证。
5. 接声光报警验证。

供电要求：

- 风扇、水泵独立 5V 供电。
- P4 和外部 5V 电源共地。
- 水泵水路远离开发板。

通过标准：

- `risk_level = 1` 时只提示，不启动水泵。
- `risk_level = 2` 时启动报警和风扇。
- `risk_level = 3` 时启动报警、风扇和水泵。
- 执行器动作时 P4 不重启。

失败先查：

- 外部 5V 电源电流是否足够。
- 是否共地。
- MOSFET/继电器控制电平是否匹配。
- 水泵是否堵转。
- 负载动作瞬间是否造成压降。

## 8. 门外板门锁、灯光和提示音验证

优先替换：

```text
firmware/esp_outdoor/components/led_status/
firmware/esp_outdoor/components/audio_prompt/
```

门锁逻辑在：

```text
firmware/esp_outdoor/components/door_fsm/
```

建议顺序：

1. 先用串口日志验证 `door_fsm` 判断。
2. 再接舵机信号线。
3. 舵机独立 5V 供电，并与 P4 共地。
4. 接 WS2812B 灯环。
5. 接蜂鸣器或喇叭。

通过标准：

- PPE 合格 + 门内正常时舵机开锁。
- PPE 不合格时舵机保持锁定。
- 门内 `risk_level >= 2` 时舵机保持锁定。
- 允许进入时灯光显示绿色。
- 拒绝进入时灯光显示红色或黄色。

失败先查：

- 舵机是否独立供电。
- PWM 频率和脉宽是否正确。
- WS2812B 是否共地。
- WS2812B 数据线是否需要串联电阻。

## 9. 屏幕和 LVGL 验证

优先替换：

```text
firmware/esp_outdoor/components/ui_lvgl/
```

建议先跑官方或板厂屏幕示例，再迁入本项目组件。

验证顺序：

1. 点亮屏幕背光。
2. 显示纯色画面。
3. 显示 LVGL label。
4. 验证触摸坐标。
5. 显示准入主界面。
6. 显示门内风险状态。
7. 添加复位和测试按钮。

通过标准：

- 屏幕无花屏。
- 触摸方向正确。
- UI 更新不会明显卡顿。
- MQTT/传感器任务运行时 UI 不阻塞。

失败先查：

- FPC 方向。
- 屏幕型号和驱动配置。
- 背光控制引脚。
- LVGL tick 和 flush 回调。
- 任务栈大小。

## 10. 摄像头验证

优先替换：

```text
firmware/esp_outdoor/components/camera_capture/
firmware/esp_indoor/components/camera_capture/
```

建议先跑官方或板厂 OV2740 示例，再迁入本项目。

验证顺序：

1. 摄像头初始化。
2. 抓取一帧。
3. 保存一帧到 TF 卡。
4. 转换成模型输入尺寸。
5. 低帧率连续采集。
6. 与 UI 或 MQTT 同时运行。

通过标准：

- 两块板各自能稳定采集。
- 连续采集 30 分钟无崩溃。
- 图像方向和视角符合沙盘安装需求。
- 采集任务不会阻塞网络和 UI。

失败先查：

- 摄像头 FPC 方向。
- 摄像头型号是否匹配。
- MIPI CSI 配置。
- 图像缓冲区大小。
- PSRAM/内存配置。

## 11. AI 推理验证

优先替换：

```text
firmware/esp_outdoor/components/ppe_infer/
firmware/esp_indoor/components/hazard_infer/
```

验证顺序：

1. 加载 `.espdl` 模型。
2. 用固定测试图片推理。
3. 用摄像头实时帧推理。
4. 记录单次推理耗时。
5. 推理结果接入状态机。
6. 连续运行稳定性测试。

通过标准：

- PPE 模型能输出实验服和护目镜状态。
- 烟雾/火焰模型能输出 normal/smoke/flame。
- 推理结果能发布到 MQTT。
- 推理期间 UI 和网络不断连。
- 推理失败时进入保守策略，不直接崩溃。

失败先查：

- 模型文件路径。
- 模型输入尺寸。
- 图像颜色格式。
- 量化方式。
- 内存不足。
- 不支持的算子。

## 12. 双板联调

联调顺序：

1. 门内板单独运行，确认发布 `sensor` 和 `risk`。
2. 门外板单独运行，确认发布 `ppe` 和 `access`。
3. 门外板订阅门内 `risk`。
4. 门内切换到二级告警，门外显示禁入。
5. 门内切换到三级应急，门外锁死准入。
6. 管理员复位，两块板状态同步恢复。

通过标准：

- 门内风险变化后，门外 1 秒内更新。
- 门内离线时，门外禁止入内。
- 恢复网络后，双板能重新同步。
- 事件日志完整。

失败先查：

- MQTT topic 是否一致。
- JSON 字段是否一致。
- 门外是否处理了门内状态消息。
- 时间戳是否异常。
- 网络重连逻辑是否生效。

## 13. 稳定性测试

正式演示前至少做：

- [ ] 双板空载连续运行 1 小时。
- [ ] 双板带传感器连续运行 1 小时。
- [ ] 摄像头连续采集 30 分钟。
- [ ] 屏幕和 LVGL 连续运行 1 小时。
- [ ] 执行器重复动作 20 次。
- [ ] 断网恢复测试 5 次。
- [ ] 断电重启恢复测试 5 次。

通过标准：

- 无反复重启。
- 无明显内存泄漏。
- 无执行器误动作。
- 日志能定位每次状态变化。

## 14. 记录模板

每次验证建议记录：

```text
日期：
板子：P4-A / P4-B
固件版本：
模块：
接线：
测试命令：
预期结果：
实际结果：
问题：
处理：
是否通过：
```

## 15. 最推荐的实物验证顺序

1. 两块 P4 烧录和串口日志。
2. 当前演示场景逻辑运行。
3. 真实 Wi-Fi + MQTT。
4. 门内 SHT30 / ENS160 或 SGP40 / MQ-2。
5. 门内风扇、水泵、报警。
6. 门外舵机、灯环、提示音。
7. 门外屏幕和 LVGL。
8. 两块 OV2740 摄像头。
9. PPE 和烟雾/火焰模型。
10. 双板完整联调。
11. 沙盘稳定性测试。
